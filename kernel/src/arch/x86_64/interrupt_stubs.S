.section .text
.extern interrupt_dispatch

.macro ISR_NOERR number
.global isr_stub_\number
.type isr_stub_\number, @function
isr_stub_\number:
    pushq $0
    pushq $\number
    jmp isr_common
.endm

.macro ISR_ERR number
.global isr_stub_\number
.type isr_stub_\number, @function
isr_stub_\number:
    pushq $\number
    jmp isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

.global isr_stub_timer
.type isr_stub_timer, @function
isr_stub_timer:
    pushq $0
    pushq $64
    jmp isr_common

.global isr_stub_keyboard
.type isr_stub_keyboard, @function
isr_stub_keyboard:
    pushq $0
    pushq $65
    jmp isr_common

.global isr_stub_tlb_shootdown
.type isr_stub_tlb_shootdown, @function
isr_stub_tlb_shootdown:
    pushq $0
    pushq $66
    jmp isr_common

.global isr_stub_mouse
.type isr_stub_mouse, @function
isr_stub_mouse:
    pushq $0
    pushq $67
    jmp isr_common

.global isr_stub_spurious
.type isr_stub_spurious, @function
isr_stub_spurious:
    pushq $0
    pushq $255
    jmp isr_common

/*
 * Restore the exact scheduler frame format used by isr_common.  This is the
 * one-way bootstrap path used when an AP leaves the Limine-provided stack and
 * enters its scheduler-owned idle frame for the first time.
 */
.global interrupt_enter_frame
.type interrupt_enter_frame, @function
interrupt_enter_frame:
    movq %rdi, %rsp
    jmp restore_interrupt_frame

.type isr_common, @function
isr_common:
    cld

    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %r12
    andq $-16, %rsp

    movq %r12, %rdi
    call interrupt_dispatch

    /*
     * RAX is the interrupt frame Aurora wants to resume. It may be the
     * original frame or a different thread's saved frame.
     */
    movq %rax, %rsp

restore_interrupt_frame:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp
    iretq

.section .rodata
.global isr_stub_table
.type isr_stub_table, @object
.align 8

isr_stub_table:
    .quad isr_stub_0
    .quad isr_stub_1
    .quad isr_stub_2
    .quad isr_stub_3
    .quad isr_stub_4
    .quad isr_stub_5
    .quad isr_stub_6
    .quad isr_stub_7
    .quad isr_stub_8
    .quad isr_stub_9
    .quad isr_stub_10
    .quad isr_stub_11
    .quad isr_stub_12
    .quad isr_stub_13
    .quad isr_stub_14
    .quad isr_stub_15
    .quad isr_stub_16
    .quad isr_stub_17
    .quad isr_stub_18
    .quad isr_stub_19
    .quad isr_stub_20
    .quad isr_stub_21
    .quad isr_stub_22
    .quad isr_stub_23
    .quad isr_stub_24
    .quad isr_stub_25
    .quad isr_stub_26
    .quad isr_stub_27
    .quad isr_stub_28
    .quad isr_stub_29
    .quad isr_stub_30
    .quad isr_stub_31

.size isr_stub_table, . - isr_stub_table

.section .note.GNU-stack,"",@progbits
