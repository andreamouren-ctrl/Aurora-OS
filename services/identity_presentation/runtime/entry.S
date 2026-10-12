.section .text.entry,"ax",@progbits
.global _start
.type _start,@function
.extern identity_ring3_main
_start:
    movq %rsp, %rdi
    call identity_ring3_main
    movq %rax, %rdi
    movq $3, %rax
    syscall
1:
    ud2
    jmp 1b
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
