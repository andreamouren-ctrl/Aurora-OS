#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/gdt.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/syscall.h>

#define IA32_EFER  0xC0000080u
#define IA32_STAR  0xC0000081u
#define IA32_LSTAR 0xC0000082u
#define IA32_FMASK 0xC0000084u

#define EFER_SCE (1ull << 0)

#define USER_TOP_EXCLUSIVE 0x0000800000000000ull

extern void x86_64_syscall_entry(void);

volatile uint64_t syscall_kernel_rsp;
volatile uint64_t syscall_user_rsp_scratch;

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
        : "=a"(a), "=b"(b),
          "=c"(c), "=d"(d)
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

    return
        ((uint64_t)high << 32) |
        low;
}

static void wrmsr(
    uint32_t msr,
    uint64_t value
) {
    __asm__ volatile (
        "wrmsr"
        :
        : "c"(msr),
          "a"((uint32_t)value),
          "d"((uint32_t)(
              value >> 32))
    );
}

static bool syscall_supported(void) {
    uint32_t max_extended = 0;

    cpuid(
        0x80000000u,
        &max_extended,
        NULL,
        NULL,
        NULL
    );

    if (max_extended <
        0x80000001u) {
        return false;
    }

    uint32_t edx = 0;

    cpuid(
        0x80000001u,
        NULL,
        NULL,
        NULL,
        &edx
    );

    return
        (edx & (1u << 11)) != 0;
}

bool syscall_init(void) {
    if (!syscall_supported()) {
        return false;
    }

    uint64_t efer =
        rdmsr(IA32_EFER);

    wrmsr(
        IA32_EFER,
        efer | EFER_SCE
    );

    /*
     * SYSCALL loads CS=0x08 and SS=0x10.
     * SYSRET derives SS=0x18|3 and CS=0x20|3 from the upper STAR selector.
     */
    uint64_t star =
        ((uint64_t)AURORA_KERNEL_CODE_SELECTOR
            << 32) |
        ((uint64_t)AURORA_KERNEL_DATA_SELECTOR
            << 48);

    wrmsr(
        IA32_STAR,
        star
    );

    wrmsr(
        IA32_LSTAR,
        (uint64_t)(uintptr_t)
            x86_64_syscall_entry
    );

    /*
     * Enter the kernel with interrupts, single-step and direction flag
     * cleared. The dispatcher is deliberately non-preemptible for now.
     */
    wrmsr(
        IA32_FMASK,
        (1ull << 8) |
        (1ull << 9) |
        (1ull << 10)
    );

    return true;
}

void syscall_set_kernel_stack(
    uint64_t stack_top
) {
    __atomic_store_n(
        &syscall_kernel_rsp,
        stack_top,
        __ATOMIC_RELEASE
    );
}

static bool user_return_state_valid(
    const struct syscall_frame *frame
) {
    return frame != NULL &&
        frame->user_rip <
            USER_TOP_EXCLUSIVE &&
        frame->user_rsp <
            USER_TOP_EXCLUSIVE;
}

static uint64_t dispatch_cap_check(
    struct aurora_process *process,
    uint64_t handle,
    uint64_t type,
    uint64_t rights
) {
    if (process == NULL ||
        type > AURORA_CAP_SYSTEM) {
        return 0;
    }

    struct aurora_capability_view view;

    return cap_lookup(
        &process->capabilities,
        (aurora_cap_handle)handle,
        (enum aurora_cap_type)type,
        rights,
        &view
    ) ? 1ull : 0ull;
}

void syscall_dispatch(
    struct syscall_frame *frame
) {
    if (!user_return_state_valid(frame)) {
        for (;;) {
            __asm__ volatile (
                "cli; hlt"
            );
        }
    }

    struct aurora_process *process =
        scheduler_current_process();

    uint64_t number =
        frame->rax;

    switch (number) {
        case AURORA_SYS_BOOTSTRAP_SIGNAL:
            process_set_bootstrap_signal(
                process,
                frame->rdi
            );

            frame->rax = 0;
            break;

        case AURORA_SYS_CLOCK_NS:
            frame->rax =
                clock_now_ns();
            break;

        case AURORA_SYS_CAP_CHECK:
            frame->rax =
                dispatch_cap_check(
                    process,
                    frame->rdi,
                    frame->rsi,
                    frame->rdx
                );
            break;

        default:
            frame->rax =
                (uint64_t)-1;
            break;
    }

    /*
     * Never let SYSRET restore privileged IOPL/NT/VM/RF state. Keep IF and
     * architectural bit 1 set for normal preemptible user execution.
     */
    frame->user_rflags &=
        ~((3ull << 12) |
          (1ull << 14) |
          (1ull << 16) |
          (1ull << 17));

    frame->user_rflags |=
        0x202ull;
}
