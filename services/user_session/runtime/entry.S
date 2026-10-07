.section .text.entry,"ax",@progbits
.global _start
.type _start,@function
.extern user_session_host_main

_start:
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

    leaq 96(%rsp), %rdi
    call user_session_host_main

    addq $160, %rsp

    movq %rax, %rdi
    movq $3, %rax
    syscall

1:
    ud2
    jmp 1b

.size _start, .-_start
.section .note.GNU-stack,"",@progbits
