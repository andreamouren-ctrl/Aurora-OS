#include <aurora/cpu_local_offsets.h>

.section .text

.extern syscall_dispatch

.global x86_64_syscall_entry
.type x86_64_syscall_entry, @function

x86_64_syscall_entry:
    /*
     * IA32_KERNEL_GS_BASE holds this CPU's kernel-local record while user GS
     * remains untrusted/empty. SWAPGS gives us a stable CPU-local base before
     * touching any stack state.
     */
    swapgs

    movq %rsp, %gs:AURORA_CPU_LOCAL_USER_RSP_OFFSET
    movq %gs:AURORA_CPU_LOCAL_SYSCALL_RSP_OFFSET, %rsp

    testq %rsp, %rsp
    jz .Lfatal

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

    movq %gs:AURORA_CPU_LOCAL_USER_RSP_OFFSET, %rax
    pushq %rax

    movq %rsp, %rdi
    call syscall_dispatch

    testq %rax, %rax
    jnz .Lswitch_frame

    movq 0(%rsp), %rax
    movq %rax, %gs:AURORA_CPU_LOCAL_USER_RSP_OFFSET

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

    movq %gs:AURORA_CPU_LOCAL_USER_RSP_OFFSET, %rsp
    swapgs
    sysretq

.Lswitch_frame:
    /* Restore the user-side GS base before returning through IRETQ too. */
    swapgs
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
