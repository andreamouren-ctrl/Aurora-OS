#ifndef AURORA_INTERRUPTS_H
#define AURORA_INTERRUPTS_H

#include <stdbool.h>
#include <stdint.h>

#define AURORA_VECTOR_TIMER         0x40u
#define AURORA_VECTOR_KEYBOARD      0x41u
#define AURORA_VECTOR_TLB_SHOOTDOWN 0x42u
#define AURORA_VECTOR_SPURIOUS      0xFFu

struct interrupt_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;

    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t vector;
    uint64_t error_code;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;

    /*
     * In 64-bit long mode Aurora keeps the interrupted RSP/SS slots in every
     * scheduler frame so the same layout can represent kernel and user work.
     */
    uint64_t rsp;
    uint64_t ss;
};

typedef struct interrupt_frame *(
    *interrupt_handler_fn
)(
    struct interrupt_frame *frame
);

bool interrupts_init(void);
void interrupts_load_current_cpu(void);

bool interrupt_register_handler(
    uint8_t vector,
    interrupt_handler_fn handler
);

struct interrupt_frame *interrupt_dispatch(
    struct interrupt_frame *frame
);

/*
 * Restore a scheduler interrupt frame exactly like the common ISR return path.
 * Used for the first AP bootstrap-stack -> scheduler-stack handoff.
 */
void interrupt_enter_frame(
    struct interrupt_frame *frame
) __attribute__((noreturn));

#endif
