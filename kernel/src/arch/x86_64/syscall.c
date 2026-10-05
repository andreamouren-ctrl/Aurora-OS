#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/cpu_local.h>
#include <aurora/gdt.h>
#include <aurora/ipc.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/syscall.h>
#include <aurora/usercopy.h>

#define IA32_EFER  0xC0000080u
#define IA32_STAR  0xC0000081u
#define IA32_LSTAR 0xC0000082u
#define IA32_FMASK 0xC0000084u

#define EFER_SCE (1ull << 0)
#define USER_TOP_EXCLUSIVE 0x0000800000000000ull

_Static_assert(AURORA_SYS_IPC_PAYLOAD_MAX == AURORA_IPC_PAYLOAD_MAX,
    "syscall and kernel IPC payload limits must match");
_Static_assert(AURORA_SYS_IPC_CAPS_MAX == AURORA_IPC_CAPS_MAX,
    "syscall and kernel IPC capability limits must match");

extern void x86_64_syscall_entry(void);

static void cpuid(
    uint32_t leaf,
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    __asm__ volatile (
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(0)
    );

    if (eax != NULL) *eax = a;
    if (ebx != NULL) *ebx = b;
    if (ecx != NULL) *ecx = c;
    if (edx != NULL) *edx = d;
}

static uint64_t rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile (
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr)
    );
    return ((uint64_t)high << 32) | low;
}

static void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile (
        "wrmsr"
        :
        : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32))
    );
}

static bool syscall_supported(void) {
    uint32_t max_extended = 0;
    cpuid(0x80000000u, &max_extended, NULL, NULL, NULL);
    if (max_extended < 0x80000001u) return false;

    uint32_t edx = 0;
    cpuid(0x80000001u, NULL, NULL, NULL, &edx);
    return (edx & (1u << 11)) != 0;
}

bool syscall_init(void) {
    if (!syscall_supported() || cpu_local_current() == NULL) return false;

    uint64_t efer = rdmsr(IA32_EFER);
    wrmsr(IA32_EFER, efer | EFER_SCE);

    /* SYSCALL loads CS=0x08/SS=0x10; SYSRET derives user selectors. */
    uint64_t star =
        ((uint64_t)AURORA_KERNEL_CODE_SELECTOR << 32) |
        ((uint64_t)AURORA_KERNEL_DATA_SELECTOR << 48);
    wrmsr(IA32_STAR, star);
    wrmsr(IA32_LSTAR, (uint64_t)(uintptr_t)x86_64_syscall_entry);

    /* IF, TF and DF are cleared on kernel entry. */
    wrmsr(IA32_FMASK, (1ull << 8) | (1ull << 9) | (1ull << 10));
    return true;
}

void syscall_set_kernel_stack(uint64_t stack_top) {
    cpu_local_set_syscall_kernel_rsp(stack_top);
}

static bool user_return_state_valid(const struct syscall_frame *frame) {
    return frame != NULL &&
        frame->user_rip < USER_TOP_EXCLUSIVE &&
        frame->user_rsp < USER_TOP_EXCLUSIVE;
}

static uint64_t dispatch_cap_check(
    struct aurora_process *process,
    uint64_t handle,
    uint64_t type,
    uint64_t rights
) {
    if (process == NULL || type >= AURORA_CAP_TYPE_COUNT) return 0;
    struct aurora_capability_view view;
    return cap_lookup(
        &process->capabilities,
        (aurora_cap_handle)handle,
        (enum aurora_cap_type)type,
        rights,
        &view
    ) ? 1ull : 0ull;
}

static struct aurora_ipc_endpoint *lookup_ipc_endpoint(
    struct aurora_process *process,
    uint64_t handle,
    uint64_t required_rights
) {
    struct aurora_capability_view view;
    if (process == NULL ||
        !cap_lookup(
            &process->capabilities,
            (aurora_cap_handle)handle,
            AURORA_CAP_IPC_ENDPOINT,
            required_rights,
            &view)) {
        return NULL;
    }
    return (struct aurora_ipc_endpoint *)view.object;
}

static uint64_t dispatch_ipc_send(
    struct aurora_process *process,
    uint64_t endpoint_handle,
    uint64_t user_data,
    uint64_t length,
    uint64_t user_transfers,
    uint64_t transfer_count
) {
    uint8_t payload[AURORA_SYS_IPC_PAYLOAD_MAX];
    struct aurora_sys_ipc_transfer user_transfer_buffer[AURORA_SYS_IPC_CAPS_MAX];
    struct aurora_ipc_transfer transfers[AURORA_SYS_IPC_CAPS_MAX];

    if (process == NULL ||
        length > AURORA_SYS_IPC_PAYLOAD_MAX ||
        transfer_count > AURORA_SYS_IPC_CAPS_MAX) {
        return AURORA_SYS_RESULT_ERROR;
    }

    struct aurora_ipc_endpoint *endpoint =
        lookup_ipc_endpoint(process, endpoint_handle, AURORA_RIGHT_WRITE);
    if (endpoint == NULL) return AURORA_SYS_RESULT_ERROR;

    if (length != 0u &&
        !copy_from_user(process, payload, user_data, (size_t)length)) {
        return AURORA_SYS_RESULT_ERROR;
    }

    if (transfer_count != 0u) {
        size_t transfer_bytes =
            (size_t)transfer_count * sizeof(user_transfer_buffer[0]);
        if (!copy_from_user(
                process,
                user_transfer_buffer,
                user_transfers,
                transfer_bytes)) {
            return AURORA_SYS_RESULT_ERROR;
        }

        for (uint32_t i = 0u; i < (uint32_t)transfer_count; ++i) {
            transfers[i].handle =
                (aurora_cap_handle)user_transfer_buffer[i].handle;
            transfers[i].rights = user_transfer_buffer[i].rights;
        }
    }

    return ipc_send(
        endpoint,
        &process->capabilities,
        length == 0u ? NULL : payload,
        (uint32_t)length,
        transfer_count == 0u ? NULL : transfers,
        (uint32_t)transfer_count
    ) ? 0ull : AURORA_SYS_RESULT_ERROR;
}

static uint64_t dispatch_ipc_receive(
    struct aurora_process *process,
    uint64_t endpoint_handle,
    uint64_t user_output
) {
    struct aurora_sys_ipc_received output = {0};
    struct aurora_ipc_received received = {0};

    if (process == NULL || user_output == 0u) {
        return AURORA_SYS_RESULT_ERROR;
    }

    struct aurora_ipc_endpoint *endpoint =
        lookup_ipc_endpoint(process, endpoint_handle, AURORA_RIGHT_READ);
    if (endpoint == NULL) return AURORA_SYS_RESULT_ERROR;

    /*
     * Preflight the complete destination before consuming the queued message.
     * Aurora currently has no Ring 3 unmap syscall, so the mapping cannot be
     * invalidated between this check and the final copy in the same syscall.
     */
    if (!copy_to_user(process, user_output, &output, sizeof(output))) {
        return AURORA_SYS_RESULT_ERROR;
    }

    if (!ipc_receive(endpoint, &process->capabilities, &received)) {
        return AURORA_SYS_RESULT_ERROR;
    }

    output.length = received.length;
    output.capability_count = received.capability_count;

    for (uint32_t i = 0u; i < received.length; ++i) {
        output.data[i] = received.data[i];
    }
    for (uint32_t i = 0u; i < received.capability_count; ++i) {
        output.capabilities[i] = received.capabilities[i];
    }

    return copy_to_user(process, user_output, &output, sizeof(output))
        ? 0ull
        : AURORA_SYS_RESULT_ERROR;
}

struct interrupt_frame *syscall_dispatch(struct syscall_frame *frame) {
    struct aurora_process *process = scheduler_current_process();
    if (process == NULL) return scheduler_terminate_current();

    if (!user_return_state_valid(frame)) {
        process_mark_faulted(process, UINT64_MAX);
        return scheduler_terminate_current();
    }

    uint64_t number = frame->rax;
    switch (number) {
        case AURORA_SYS_BOOTSTRAP_SIGNAL:
            process_set_bootstrap_signal(process, frame->rdi);
            frame->rax = 0;
            break;
        case AURORA_SYS_CLOCK_NS:
            frame->rax = clock_now_ns();
            break;
        case AURORA_SYS_CAP_CHECK:
            frame->rax = dispatch_cap_check(
                process, frame->rdi, frame->rsi, frame->rdx
            );
            break;
        case AURORA_SYS_EXIT:
            process_mark_exited(process, (int64_t)frame->rdi);
            return scheduler_terminate_current();
        case AURORA_SYS_IPC_SEND:
            frame->rax = dispatch_ipc_send(
                process,
                frame->rdi,
                frame->rsi,
                frame->rdx,
                frame->r10,
                frame->r8
            );
            break;
        case AURORA_SYS_IPC_RECEIVE:
            frame->rax = dispatch_ipc_receive(
                process,
                frame->rdi,
                frame->rsi
            );
            break;
        default:
            frame->rax = AURORA_SYS_RESULT_ERROR;
            break;
    }

    frame->user_rflags &=
        ~((3ull << 12) | (1ull << 14) | (1ull << 16) | (1ull << 17));
    frame->user_rflags |= 0x202ull;
    return NULL;
}
