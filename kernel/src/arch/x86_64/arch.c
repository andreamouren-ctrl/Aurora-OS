#include <aurora/arch.h>

#define COM1_PORT 0x3F8u

uint8_t arch_in8(uint16_t port) {
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

void arch_out8(uint16_t port, uint8_t value) {
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

void arch_serial_init(void) {
    arch_out8(COM1_PORT + 1, 0x00);
    arch_out8(COM1_PORT + 3, 0x80);
    arch_out8(COM1_PORT + 0, 0x03);
    arch_out8(COM1_PORT + 1, 0x00);
    arch_out8(COM1_PORT + 3, 0x03);
    arch_out8(COM1_PORT + 2, 0xC7);
    arch_out8(COM1_PORT + 4, 0x0B);
}

void arch_serial_putc(char c) {
    while ((arch_in8(COM1_PORT + 5) & 0x20u) == 0u) {
        __asm__ volatile ("pause");
    }

    arch_out8(COM1_PORT, (uint8_t)c);
}

void arch_early_init(void) {
    /*
     * Keep maskable interrupts disabled until Aurora installs its own IDT
     * and interrupt-controller policy.
     */
    __asm__ volatile ("cli; cld");
}

uint64_t arch_read_cr2(void) {
    uint64_t value;

    __asm__ volatile (
        "mov %%cr2, %0"
        : "=r"(value)
    );

    return value;
}

uint64_t arch_read_cr3(void) {
    uint64_t value;

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(value)
    );

    return value;
}

uint16_t arch_read_cs(void) {
    uint16_t value;

    __asm__ volatile (
        "mov %%cs, %0"
        : "=r"(value)
    );

    return value;
}

void arch_invalidate_page(uint64_t virtual_address) {
    __asm__ volatile (
        "invlpg (%0)"
        :
        : "r"((uintptr_t)virtual_address)
        : "memory"
    );
}

void arch_halt(void) {
    __asm__ volatile ("cli");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
