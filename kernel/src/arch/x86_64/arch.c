#include <aurora/arch.h>

#define COM1_PORT 0x3F8u

#define EFER_MSR 0xC0000080u
#define EFER_NXE (1ull << 11)

static bool nx_enabled;

#define CR0_WP   (1ull << 16)
#define CR4_UMIP (1ull << 11)
#define CR4_SMEP (1ull << 20)
#define CR4_SMAP (1ull << 21)

static void cpuid(
    uint32_t leaf,
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    __asm__ volatile (
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(0)
    );

    if (eax != 0) *eax = a;
    if (ebx != 0) *ebx = b;
    if (ecx != 0) *ecx = c;
    if (edx != 0) *edx = d;
}

static uint64_t rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile (
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr)
    );

    return ((uint64_t)high << 32) | low;
}

static void wrmsr(
    uint32_t msr,
    uint64_t value
) {
    __asm__ volatile (
        "wrmsr"
        :
        : "c"(msr),
          "a"((uint32_t)value),
          "d"((uint32_t)(value >> 32))
    );
}

static void enable_nx_if_supported(void) {
    uint32_t max_extended_leaf = 0;

    cpuid(
        0x80000000u,
        &max_extended_leaf,
        0,
        0,
        0
    );

    if (max_extended_leaf < 0x80000001u) {
        return;
    }

    uint32_t features_edx = 0;

    cpuid(
        0x80000001u,
        0,
        0,
        0,
        &features_edx
    );

    if ((features_edx & (1u << 20)) == 0) {
        return;
    }

    uint64_t efer = rdmsr(EFER_MSR);

    if ((efer & EFER_NXE) == 0) {
        wrmsr(
            EFER_MSR,
            efer | EFER_NXE
        );
    }

    nx_enabled = true;
}

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
    __asm__ volatile ("cli; cld");
    enable_nx_if_supported();
}

bool arch_nx_enabled(void) {
    return nx_enabled;
}

struct aurora_arch_hardening arch_enable_hardening(void) {
    struct aurora_arch_hardening result = { 0 };

    uint64_t cr0;

    __asm__ volatile (
        "mov %%cr0, %0"
        : "=r"(cr0)
    );

    cr0 |= CR0_WP;

    __asm__ volatile (
        "mov %0, %%cr0"
        :
        : "r"(cr0)
        : "memory"
    );

    result.write_protect = true;

    uint32_t max_basic = 0;

    cpuid(
        0,
        &max_basic,
        0,
        0,
        0
    );

    if (max_basic < 7) {
        return result;
    }

    uint32_t ebx = 0;
    uint32_t ecx = 0;

    cpuid(
        7u,
        NULL,
        &ebx,
        &ecx,
        NULL
    );

    uint64_t cr4;

    __asm__ volatile (
        "mov %%cr4, %0"
        : "=r"(cr4)
    );

    if ((ebx & (1u << 7)) != 0) {
        cr4 |= CR4_SMEP;
        result.smep = true;
    }

    if ((ebx & (1u << 20)) != 0) {
        cr4 |= CR4_SMAP;
        result.smap = true;
    }

    if ((ecx & (1u << 2)) != 0) {
        cr4 |= CR4_UMIP;
        result.umip = true;
    }

    __asm__ volatile (
        "mov %0, %%cr4"
        :
        : "r"(cr4)
        : "memory"
    );

    return result;
}

void arch_enable_interrupts(void) {
    __asm__ volatile ("sti" : : : "memory");
}

void arch_disable_interrupts(void) {
    __asm__ volatile ("cli" : : : "memory");
}

void arch_idle(void) {
    __asm__ volatile ("hlt");
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

void arch_write_cr3(uint64_t physical_address) {
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(physical_address)
        : "memory"
    );
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
    arch_disable_interrupts();

    for (;;) {
        arch_idle();
    }
}
