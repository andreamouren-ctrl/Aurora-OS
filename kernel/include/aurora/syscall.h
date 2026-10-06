#ifndef AURORA_SYSCALL_H
#define AURORA_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/interrupts.h>
#include <aurora/syscall_abi.h>

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
