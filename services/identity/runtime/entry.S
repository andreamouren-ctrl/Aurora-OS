.section .text.entry,"ax",@progbits
.global _start
.type _start,@function
.extern identity_runtime_main

_start:
    /*
     * Preserve the complete 80-byte service startup ABI v4 block before
     * entering C; a C prologue may overwrite the historical stack area.
     */
    movq %rsp, %r11
    subq $176, %rsp

    movq -80(%r11), %rax
    movq %rax, 32(%rsp)
    movq -72(%r11), %rax
    movq %rax, 40(%rsp)
    movq -64(%r11), %rax
    movq %rax, 48(%rsp)
    movq -56(%r11), %rax
    movq %rax, 56(%rsp)
    movq -48(%r11), %rax
    movq %rax, 64(%rsp)
    movq -40(%r11), %rax
    movq %rax, 72(%rsp)
    movq -32(%r11), %rax
    movq %rax, 80(%rsp)
    movq -24(%r11), %rax
    movq %rax, 88(%rsp)
    movq -16(%r11), %rax
    movq %rax, 96(%rsp)
    movq -8(%r11), %rax
    movq %rax, 104(%rsp)

    leaq 112(%rsp), %rdi
    call identity_runtime_main

    addq $176, %rsp

    movq %rax, %rdi
    movq $3, %rax
    syscall

1:
    ud2
    jmp 1b

.size _start, .-_start
.section .note.GNU-stack,"",@progbits
