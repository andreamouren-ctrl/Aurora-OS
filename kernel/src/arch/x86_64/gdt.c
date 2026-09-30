#include <stddef.h>
#include <stdint.h>

#include <aurora/gdt.h>

struct tss64 {
    uint32_t reserved0;

    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;

    uint64_t reserved1;

    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;

    uint64_t reserved2;

    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct cpu_gdt {
    uint64_t entries[7];
} __attribute__((packed));

extern void x86_64_load_gdt_tss(
    const struct gdtr *descriptor,
    uint16_t data_selector,
    uint16_t code_selector,
    uint16_t tss_selector
);

static struct cpu_gdt gdts[
    AURORA_GDT_MAX_CPUS
];

static struct tss64 tsses[
    AURORA_GDT_MAX_CPUS
];

static uint32_t bsp_slot;
static bool bsp_ready;

static void clear_bytes(
    void *address,
    size_t size
) {
    uint8_t *bytes = address;

    for (size_t i = 0;
         i < size;
         ++i) {
        bytes[i] = 0;
    }
}

static uint64_t make_tss_low(
    uint64_t base,
    uint32_t limit
) {
    uint64_t descriptor = 0;

    descriptor |=
        (uint64_t)(limit & 0xFFFFu);

    descriptor |=
        (base & 0xFFFFFFull) << 16;

    descriptor |=
        0x89ull << 40;

    descriptor |=
        ((uint64_t)limit & 0xF0000ull)
        << 32;

    descriptor |=
        (base & 0xFF000000ull)
        << 32;

    return descriptor;
}

static bool gdt_init_slot(
    uint32_t cpu_slot
) {
    if (cpu_slot >=
        AURORA_GDT_MAX_CPUS) {
        return false;
    }

    struct cpu_gdt *gdt =
        &gdts[cpu_slot];

    struct tss64 *tss =
        &tsses[cpu_slot];

    clear_bytes(
        gdt,
        sizeof(*gdt)
    );

    clear_bytes(
        tss,
        sizeof(*tss)
    );

    /*
     * No I/O bitmap is exposed to Ring 3. An offset beyond the TSS limit
     * causes all direct user I/O instructions to fail.
     */
    tss->iomap_base =
        (uint16_t)sizeof(*tss);

    gdt->entries[0] =
        0x0000000000000000ull;

    gdt->entries[1] =
        0x00AF9A000000FFFFull;

    gdt->entries[2] =
        0x00CF92000000FFFFull;

    gdt->entries[3] =
        0x00CFF2000000FFFFull;

    gdt->entries[4] =
        0x00AFFA000000FFFFull;

    uint64_t tss_base =
        (uint64_t)(uintptr_t)tss;

    uint32_t tss_limit =
        (uint32_t)sizeof(*tss) - 1u;

    gdt->entries[5] =
        make_tss_low(
            tss_base,
            tss_limit
        );

    gdt->entries[6] =
        tss_base >> 32;

    struct gdtr descriptor = {
        .limit =
            (uint16_t)(
                sizeof(*gdt) - 1u
            ),
        .base =
            (uint64_t)(uintptr_t)gdt
    };

    x86_64_load_gdt_tss(
        &descriptor,
        AURORA_KERNEL_DATA_SELECTOR,
        AURORA_KERNEL_CODE_SELECTOR,
        AURORA_TSS_SELECTOR
    );

    return true;
}

bool gdt_init_bsp(uint32_t cpu_slot) {
    if (!gdt_init_slot(cpu_slot)) {
        return false;
    }

    bsp_slot = cpu_slot;
    bsp_ready = true;

    return true;
}

bool gdt_init_ap(uint32_t cpu_slot) {
    return gdt_init_slot(cpu_slot);
}

void gdt_set_bsp_kernel_stack(
    uint64_t stack_top
) {
    if (!bsp_ready) {
        return;
    }

    tsses[bsp_slot].rsp0 =
        stack_top;
}

uint16_t gdt_user_code_selector(void) {
    return AURORA_USER_CODE_SELECTOR;
}

uint16_t gdt_user_data_selector(void) {
    return AURORA_USER_DATA_SELECTOR;
}
