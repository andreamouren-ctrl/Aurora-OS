#ifndef AURORA_ARCH_H
#define AURORA_ARCH_H

#include <stdbool.h>
#include <stdint.h>

void arch_early_init(void);
bool arch_nx_enabled(void);

struct aurora_arch_hardening {
    bool write_protect;
    bool smep;
    bool smap;
    bool umip;
};

struct aurora_arch_hardening arch_enable_hardening(void);

void arch_enable_interrupts(void);
void arch_disable_interrupts(void);
void arch_idle(void);

void arch_halt(void) __attribute__((noreturn));

void arch_serial_init(void);
void arch_serial_putc(char c);

uint8_t arch_in8(uint16_t port);
uint16_t arch_in16(uint16_t port);
void arch_out8(uint16_t port, uint8_t value);
void arch_out16(uint16_t port, uint16_t value);

uint64_t arch_read_cr2(void);
uint64_t arch_read_cr3(void);
void arch_write_cr3(uint64_t physical_address);
uint16_t arch_read_cs(void);

void arch_invalidate_page(uint64_t virtual_address);

#endif
