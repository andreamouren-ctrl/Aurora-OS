.section .text

.extern syscall_dispatch
.extern syscall_kernel_rsp
.extern syscall_user_rsp_scratch

.global x86_64_syscall_entry
.type x86_64_syscall_entry, @function

x86_64_syscall_entry:
    /*
     * SYSCALL does not switch stacks. Save the untrusted user RSP without
     * dereferencing it, then move immediately to the scheduler-provided
     * kernel stack.
     */
    movq %rsp, syscall_user_rsp_scratch(%rip)
    movq syscall_kernel_rsp(%rip), %rsp

    testq %rsp, %rsp
    jz .Lfatal

    /*
     * Build struct syscall_frame. Only RAX is a defined return register;
     * the remaining general registers are preserved by Aurora's ABI.
     */
    pushq %rax
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r10
    pushq %r8
    pushq %r9
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    pushq %rcx
    pushq %r11

    movq syscall_user_rsp_scratch(%rip), %rax
    pushq %rax

    movq %rsp, %rdi
    call syscall_dispatch

    /*
     * Non-zero means the syscall terminated/faulted the current user thread.
     * The returned pointer is another thread's interrupt frame; restore it
     * with IRETQ instead of returning through SYSRET.
     */
    testq %rax, %rax
    jnz .Lswitch_frame

    movq 0(%rsp), %rax
    movq %rax, syscall_user_rsp_scratch(%rip)

    movq 8(%rsp), %r11
    movq 16(%rsp), %rcx

    addq $24, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popq %r9
    popq %r8
    popq %r10
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rax

    movq syscall_user_rsp_scratch(%rip), %rsp
    sysretq

.Lswitch_frame:
    movq %rax, %rsp

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

.Lfatal:
    cli
1:
    hlt
    jmp 1b

.section .note.GNU-stack,"",@progbits
