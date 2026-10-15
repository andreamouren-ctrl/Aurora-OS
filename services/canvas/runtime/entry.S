.section .text.entry,"ax",@progbits
.global _start
.type _start,@function
.extern g5_canvas_runtime_main
_start:
    call g5_canvas_runtime_main
    movq %rax, %rdi
    movq $3, %rax
    syscall
1:
    ud2
    jmp 1b
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
