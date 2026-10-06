#ifndef AURORA_SYSCALL_H
#define AURORA_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/interrupts.h>

#define AURORA_SYS_IPC_PAYLOAD_MAX 256u
#define AURORA_SYS_IPC_CAPS_MAX 4u
#define AURORA_SYS_PROTECTED_STATE_NAME_MAX 64u
#define AURORA_SYS_PROTECTED_STATE_IO_MAX 512u

#define AURORA_SYS_RESULT_ERROR UINT64_MAX
#define AURORA_SYS_RESULT_NOT_FOUND (UINT64_MAX - 1ull)
#define AURORA_SYS_RESULT_EXISTS (UINT64_MAX - 2ull)

enum aurora_syscall_number {
    AURORA_SYS_BOOTSTRAP_SIGNAL = 0,
    AURORA_SYS_CLOCK_NS = 1,
    AURORA_SYS_CAP_CHECK = 2,
    AURORA_SYS_EXIT = 3,
    AURORA_SYS_IPC_SEND = 4,
    AURORA_SYS_IPC_RECEIVE = 5,
    AURORA_SYS_PROTECTED_STATE_READ = 6,
    AURORA_SYS_PROTECTED_STATE_CREATE_ONCE = 7,
    AURORA_SYS_IPC_WAIT = 8
};

struct aurora_sys_ipc_transfer {
    uint64_t handle;
    uint64_t rights;
};

struct aurora_sys_ipc_received {
    uint32_t length;
    uint32_t capability_count;
    uint8_t data[AURORA_SYS_IPC_PAYLOAD_MAX];
    uint64_t capabilities[AURORA_SYS_IPC_CAPS_MAX];
};

struct syscall_frame {
    uint64_t user_rsp;
    uint64_t user_rflags;
    uint64_t user_rip;

    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;

    uint64_t rbp;
    uint64_t rbx;

    uint64_t r9;
    uint64_t r8;
    uint64_t r10;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;

    uint64_t rax;
};

bool syscall_init(void);

void syscall_set_kernel_stack(
    uint64_t stack_top
);

struct interrupt_frame *syscall_dispatch(
    struct syscall_frame *frame
);

#endif