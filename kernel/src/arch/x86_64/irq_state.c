#include <stdint.h>

#include <aurora/arch.h>

#define X86_RFLAGS_IF (1ull << 9)

bool arch_interrupts_enabled(void) {
    uint64_t rflags;
    __asm__ volatile (
        "pushfq\n\t"
        "popq %0"
        : "=r"(rflags)
        :
        : "memory"
    );
    return (rflags & X86_RFLAGS_IF) != 0u;
}

uint64_t arch_irq_save(void) {
    uint64_t rflags;
    __asm__ volatile (
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(rflags)
        :
        : "memory"
    );
    return rflags;
}

void arch_irq_restore(uint64_t state) {
    if ((state & X86_RFLAGS_IF) != 0u) {
        arch_enable_interrupts();
    } else {
        arch_disable_interrupts();
    }
}
