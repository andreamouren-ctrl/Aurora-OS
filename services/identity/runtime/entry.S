.section .text.entry,"ax",@progbits
.global _start
.type _start,@function
.extern identity_runtime_main

_start:
    /*
     * The kernel places the service startup block at the historical
     * initial-RSP - 64 location. A normal compiled C function may allocate a
     * stack frame large enough to overwrite that area before it reads it.
     *
     * Preserve the 32-byte ABI block in caller-owned stack storage before the
     * first call. identity_runtime_main still receives the same logical
     * initial-RSP contract: subtracting 64 from the synthetic value below
     * points at the preserved copy.
     */
    movq %rsp, %r11
    subq $128, %rsp

    movq -64(%r11), %rax
    movq %rax, 32(%rsp)
    movq -56(%r11), %rax
    movq %rax, 40(%rsp)
    movq -48(%r11), %rax
    movq %rax, 48(%rsp)
    movq -40(%r11), %rax
    movq %rax, 56(%rsp)

    leaq 96(%rsp), %rdi
    call identity_runtime_main

    addq $128, %rsp

    /* AURORA_SYS_EXIT = 3. identity_runtime_main returns the exit code. */
    movq %rax, %rdi
    movq $3, %rax
    syscall

1:
    ud2
    jmp 1b

.size _start, .-_start
.section .note.GNU-stack,"",@progbits
