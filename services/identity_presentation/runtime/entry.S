.section .text.entry,"ax",@progbits
.global _start
.type _start,@function
.extern identity_ring3_main

_start:
    /* ABI v1: the bootstrap record is at [initial RSP - 24, initial RSP).
     * A direct call clobbers the last 8 bytes with its return address.
     * Copy all three words above our call stack before entering C. */
    movq %rsp, %r11
    subq $128, %rsp
    movq -24(%r11), %rax
    movq %rax, 32(%rsp)
    movq -16(%r11), %rax
    movq %rax, 40(%rsp)
    movq -8(%r11), %rax
    movq %rax, 48(%rsp)

    leaq 56(%rsp), %rdi
    call identity_ring3_main

    addq $128, %rsp
    movq %rax, %rdi
    movq $3, %rax
    syscall
1:
    ud2
    jmp 1b
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
