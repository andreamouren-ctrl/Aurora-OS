#ifndef AURORA_ARCH_H
#define AURORA_ARCH_H

#include <stdint.h>

void arch_early_init(void);
void arch_halt(void) __attribute__((noreturn));

void arch_serial_init(void);
void arch_serial_putc(char c);

uint8_t arch_in8(uint16_t port);
void arch_out8(uint16_t port, uint8_t value);

uint64_t arch_read_cr3(void);
void arch_invalidate_page(uint64_t virtual_address);

#endif
