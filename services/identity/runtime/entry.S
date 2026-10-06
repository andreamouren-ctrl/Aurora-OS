.section .text.entry,"ax",@progbits
.global _start
.type _start,@function
.extern identity_runtime_main

_start:
    movq %rsp, %rdi
    call identity_runtime_main

    /* AURORA_SYS_EXIT = 3. identity_runtime_main returns the exit code. */
    movq %rax, %rdi
    movq $3, %rax
    syscall

1:
    ud2
    jmp 1b

.size _start, .-_start
.section .note.GNU-stack,"",@progbits
