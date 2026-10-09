#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/capability_abi.h>
#include <aurora/syscall_abi.h>
#include <aurora/user_session_host_abi.h>
#include <aurora/g5_ipc_abi.h>

static void clear_bytes(void *buffer, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    if (buffer == NULL) return;
    while (size != 0u) {
        *bytes++ = 0u;
        --size;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    if (destination == NULL || source == NULL) return;
    for (size_t i = 0u; i < size; ++i) dst[i] = src[i];
}

static uint64_t syscall1(uint64_t number, uint64_t a1) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    __asm__ volatile(
        "syscall"
        : "+a"(rax)
        : "D"(rdi)
        : "rcx", "r11", "memory");
    return rax;
}

static uint64_t syscall2(uint64_t number, uint64_t a1, uint64_t a2) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    __asm__ volatile(
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi)
        : "rcx", "r11", "memory");
    return rax;
}

static uint64_t syscall3(
    uint64_t number,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3
) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    register uint64_t rdx __asm__("rdx") = a3;
    __asm__ volatile(
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx)
        : "rcx", "r11", "memory");
    return rax;
}

static uint64_t syscall5(
    uint64_t number,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5
) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    register uint64_t rdx __asm__("rdx") = a3;
    register uint64_t r10 __asm__("r10") = a4;
    register uint64_t r8 __asm__("r8") = a5;
    __asm__ volatile(
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory");
    return rax;
}

static bool capability_has(
    uint64_t handle,
    enum aurora_cap_type type,
    uint64_t rights
) {
    return handle != 0u &&
        syscall3(AURORA_SYS_CAP_CHECK, handle, (uint64_t)type, rights) == 1u;
}

static bool user_id_valid(
    const uint8_t user_id[AURORA_USER_SESSION_HOST_USER_ID_SIZE]
) {
    uint8_t combined = 0u;
    if (user_id == NULL) return false;
    for (size_t i = 0u; i < AURORA_USER_SESSION_HOST_USER_ID_SIZE; ++i) {
        combined |= user_id[i];
    }
    return combined != 0u;
}

static bool send_message(
    uint64_t endpoint,
    uint32_t type,
    uint64_t request_id
) {
    const struct aurora_user_session_host_message message = {
        .version = AURORA_USER_SESSION_HOST_PROTOCOL_VERSION,
        .type = type,
        .request_id = request_id
    };

    return syscall5(
        AURORA_SYS_IPC_SEND,
        endpoint,
        (uint64_t)(uintptr_t)&message,
        sizeof(message),
        0u,
        0u
    ) == 0u;
}

static bool send_g5_ready(uint64_t endpoint,uint64_t generation) {
    /* Fixed-width G5 v1 frame, little-endian, no capability transfer.
     * The kernel validates the sender only via exclusive endpoint grants. */
    uint8_t wire[G5_IPC_WIRE_HEADER_BYTES]={0};
    wire[0]='G';wire[1]='5';wire[2]='I';wire[3]='P';
    wire[4]=G5_IPC_WIRE_MAJOR;
    wire[8]=G5_IPC_WIRE_HEADER_BYTES;
    wire[10]=G5_IPC_EVENT;
    const uint32_t op=G5_OP_SHELL_READY;
    for(unsigned i=0;i<4u;++i)wire[12u+i]=(uint8_t)(op>>(8u*i));
    wire[24]=1u; /* request_id 1 */
    for(unsigned i=0;i<8u;++i)
        wire[32u+i]=(uint8_t)(generation>>(8u*i));
    return syscall5(AURORA_SYS_IPC_SEND,endpoint,
        (uint64_t)(uintptr_t)wire,sizeof(wire),0u,0u)==0u;
}
static bool send_g5_health(uint64_t endpoint,uint64_t generation) {
    /* A second authenticated Ring 3 IPC request, checked by kernel dispatch. */
    uint8_t wire[G5_IPC_WIRE_HEADER_BYTES]={0};
    wire[0]='G';wire[1]='5';wire[2]='I';wire[3]='P';
    wire[4]=G5_IPC_WIRE_MAJOR;
    wire[8]=G5_IPC_WIRE_HEADER_BYTES;
    wire[10]=G5_IPC_REQUEST;
    const uint32_t op=G5_OP_SHELL_HEALTH;
    for(unsigned i=0;i<4u;++i)wire[12u+i]=(uint8_t)(op>>(8u*i));
    wire[24]=2u; /* strictly increasing request_id */
    for(unsigned i=0;i<8u;++i)
        wire[32u+i]=(uint8_t)(generation>>(8u*i));
    return syscall5(AURORA_SYS_IPC_SEND,endpoint,
        (uint64_t)(uintptr_t)wire,sizeof(wire),0u,0u)==0u;
}
static bool wait_message(uint64_t endpoint) {
    return syscall2(AURORA_SYS_IPC_WAIT, endpoint, 0u) == 0u;
}

static bool receive_message(
    uint64_t endpoint,
    struct aurora_sys_ipc_received *received
) {
    clear_bytes(received, sizeof(*received));
    return syscall2(
        AURORA_SYS_IPC_RECEIVE,
        endpoint,
        (uint64_t)(uintptr_t)received
    ) == 0u;
}

int64_t user_session_host_main(uint64_t initial_rsp) {
    if (initial_rsp < AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET) return 1;

    const struct aurora_user_session_host_startup *startup =
        (const struct aurora_user_session_host_startup *)(uintptr_t)(
            initial_rsp - AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET);

    if (startup->abi_version != AURORA_USER_SESSION_HOST_ABI_VERSION ||
        startup->flags != 0u ||
        (startup->g5_endpoint != 0u &&
         (!capability_has(startup->g5_endpoint,AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_WRITE) ||
          capability_has(startup->g5_endpoint,AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_TRANSFER) ||
          capability_has(startup->g5_endpoint,AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_READ))) ||
        startup->reserved1 != 0u ||
        startup->control_endpoint == 0u ||
        startup->profile_handle == 0u ||
        startup->session_generation == 0u ||
        !user_id_valid(startup->user_id)) {
        return 1;
    }

    if (!capability_has(
            startup->control_endpoint,
            AURORA_CAP_IPC_ENDPOINT,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE) ||
        capability_has(
            startup->control_endpoint,
            AURORA_CAP_IPC_ENDPOINT,
            AURORA_RIGHT_TRANSFER) ||
        !capability_has(
            startup->profile_handle,
            AURORA_CAP_FILE,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_ENUMERATE) ||
        capability_has(
            startup->profile_handle,
            AURORA_CAP_FILE,
            AURORA_RIGHT_TRANSFER) ||
        capability_has(
            startup->profile_handle,
            AURORA_CAP_FILE,
            AURORA_RIGHT_CONTROL)) {
        return 1;
    }

    if (startup->g5_endpoint != 0u &&
        !send_g5_ready(startup->g5_endpoint,startup->session_generation)) return 1;

    /* Publish bootstrap proof before READY: the kernel can consume READY
     * immediately without racing a later BOOTSTRAP_SIGNAL syscall. */
    if (syscall1(AURORA_SYS_BOOTSTRAP_SIGNAL,
                 AURORA_USER_SESSION_HOST_READY_MAGIC) != 0u) return 1;

    if (!send_message(startup->control_endpoint, AURORA_USER_SESSION_HOST_READY, 0u)) {
        return 1;
    }

    if (startup->g5_endpoint != 0u &&
        !send_g5_health(startup->g5_endpoint,startup->session_generation))
        return 1;

    for (;;) {
        if (!wait_message(startup->control_endpoint)) return 1;

        struct aurora_sys_ipc_received received;
        if (!receive_message(startup->control_endpoint, &received) ||
            received.capability_count != 0u ||
            received.length != sizeof(struct aurora_user_session_host_message)) {
            clear_bytes(&received, sizeof(received));
            return 1;
        }

        struct aurora_user_session_host_message message;
        clear_bytes(&message, sizeof(message));
        copy_bytes(&message, received.data, sizeof(message));
        clear_bytes(&received, sizeof(received));

        if (message.version != AURORA_USER_SESSION_HOST_PROTOCOL_VERSION ||
            message.type != AURORA_USER_SESSION_HOST_SHUTDOWN ||
            message.request_id == 0u) {
            clear_bytes(&message, sizeof(message));
            return 1;
        }

        uint64_t request_id = message.request_id;
        clear_bytes(&message, sizeof(message));

        return send_message(
            startup->control_endpoint,
            AURORA_USER_SESSION_HOST_SHUTDOWN_ACK,
            request_id) ? 0 : 1;
    }
}
