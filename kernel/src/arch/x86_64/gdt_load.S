.section .text

.global x86_64_load_gdt_tss
.type x86_64_load_gdt_tss, @function

x86_64_load_gdt_tss:
    lgdt (%rdi)

    movw %si, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    pushq %rdx
    leaq 1f(%rip), %rax
    pushq %rax
    lretq

1:
    movw %cx, %ax
    ltr %ax

    ret

.section .note.GNU-stack,"",@progbits
