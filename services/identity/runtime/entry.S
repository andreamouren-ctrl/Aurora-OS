.section .text.entry,"ax",@progbits
.global _start
.type _start,@function
.extern identity_runtime_main

_start:
    /*
     * Preserve the complete 64-byte service startup block before entering C.
     * A compiled C prologue may otherwise overwrite the historical
     * initial-RSP - 64 area before identity_runtime_main reads it.
     */
    movq %rsp, %r11
    subq $160, %rsp

    movq -64(%r11), %rax
    movq %rax, 32(%rsp)
    movq -56(%r11), %rax
    movq %rax, 40(%rsp)
    movq -48(%r11), %rax
    movq %rax, 48(%rsp)
    movq -40(%r11), %rax
    movq %rax, 56(%rsp)
    movq -32(%r11), %rax
    movq %rax, 64(%rsp)
    movq -24(%r11), %rax
    movq %rax, 72(%rsp)
    movq -16(%r11), %rax
    movq %rax, 80(%rsp)
    movq -8(%r11), %rax
    movq %rax, 88(%rsp)

    /*
     * Synthetic initial RSP: subtracting 64 in identity_runtime_main points
     * to the preserved copy at 32(%rsp).
     */
    leaq 96(%rsp), %rdi
    call identity_runtime_main

    addq $160, %rsp

    /* AURORA_SYS_EXIT = 3. identity_runtime_main returns the exit code. */
    movq %rax, %rdi
    movq $3, %rax
    syscall

1:
    ud2
    jmp 1b

.size _start, .-_start
.section .note.GNU-stack,"",@progbits
