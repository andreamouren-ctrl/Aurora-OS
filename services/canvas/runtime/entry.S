.section .text.entry,"ax",@progbits
.global _start
.type _start,@function
.extern g5_canvas_runtime_main
_start:
    /* A kernel-created Ring3 stack need not contain a return address.
     * Align the call site to the x86-64 SysV ABI before invoking C.
     * The runtime is freestanding and must not return to a caller. */
    movq %rsp, %rdi
    /* The launch descriptor occupies [initial_rsp-24, initial_rsp).
     * Reserve space before CALL so its return address and the C prologue
     * cannot overwrite that descriptor before it is copied. */
    subq $4096, %rsp
    andq $-16, %rsp
    call g5_canvas_runtime_main
    movq %rax, %rdi
    movq $3, %rax
    syscall
1:
    ud2
    jmp 1b
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
