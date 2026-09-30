#include <stddef.h>
#include <stdint.h>

#include <aurora/acpi.h>
#include <aurora/hpet.h>
#include <aurora/vmm.h>

#define HPET_MMIO_VIRTUAL 0xFFFFFFFFB0001000ull

#define HPET_CAPABILITIES  0x000u
#define HPET_CONFIGURATION 0x010u
#define HPET_MAIN_COUNTER  0x0F0u

struct acpi_gas {
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed));

struct acpi_hpet {
    struct acpi_sdt_header header;
    uint32_t event_timer_block_id;
    struct acpi_gas base_address;
    uint8_t hpet_number;
    uint16_t minimum_clock_tick;
    uint8_t page_protection;
} __attribute__((packed));

static volatile uint8_t *hpet_base;
static uint64_t period_fs;
static uint64_t frequency_hz;
static bool counter_64bit;

static uint32_t last_counter32;
static uint64_t counter32_high;

static uint64_t mmio_read64(uint32_t offset) {
    volatile uint64_t *reg =
        (volatile uint64_t *)(hpet_base + offset);

    return *reg;
}

static void mmio_write64(
    uint32_t offset,
    uint64_t value
) {
    volatile uint64_t *reg =
        (volatile uint64_t *)(hpet_base + offset);

    *reg = value;
}

static bool ensure_mapping(
    uint64_t virtual_address,
    uint64_t physical_address
) {
    if (vmm_map_page(
            virtual_address,
            physical_address,
            VMM_FLAG_WRITE |
            VMM_FLAG_NO_CACHE)) {
        return true;
    }

    uint64_t existing = 0;

    return vmm_translate(
               virtual_address,
               &existing) &&
        (existing & ~0xFFFull) ==
            (physical_address & ~0xFFFull);
}

bool hpet_init(void) {
    const struct acpi_sdt_header *header =
        acpi_find_table("HPET");

    if (header == NULL ||
        header->length < sizeof(struct acpi_hpet)) {
        return false;
    }

    const struct acpi_hpet *table =
        (const struct acpi_hpet *)header;

    /*
     * Aurora currently supports the normal memory-mapped HPET address
     * space. I/O-port GAS entries are deliberately rejected.
     */
    if (table->base_address.address_space_id != 0 ||
        table->base_address.address == 0) {
        return false;
    }

    uint64_t physical =
        table->base_address.address;

    uint64_t physical_page =
        physical & ~0xFFFull;

    uint64_t offset =
        physical & 0xFFFull;

    if (!ensure_mapping(
            HPET_MMIO_VIRTUAL,
            physical_page)) {
        return false;
    }

    /*
     * The main-counter register sits at offset 0xF0. Map one additional
     * page as a guard for firmware that supplies a non-page-aligned GAS.
     */
    if (offset + HPET_MAIN_COUNTER + sizeof(uint64_t) >
        0x1000ull) {
        if (!ensure_mapping(
                HPET_MMIO_VIRTUAL + 0x1000ull,
                physical_page + 0x1000ull)) {
            return false;
        }
    }

    hpet_base =
        (volatile uint8_t *)(uintptr_t)
            (HPET_MMIO_VIRTUAL + offset);

    uint64_t capabilities =
        mmio_read64(HPET_CAPABILITIES);

    period_fs = capabilities >> 32;
    counter_64bit =
        (capabilities & (1ull << 13)) != 0;

    if (period_fs == 0) {
        hpet_base = NULL;
        return false;
    }

    frequency_hz =
        1000000000000000ull / period_fs;

    if (frequency_hz == 0) {
        hpet_base = NULL;
        return false;
    }

    uint64_t configuration =
        mmio_read64(HPET_CONFIGURATION);

    mmio_write64(
        HPET_CONFIGURATION,
        configuration & ~1ull
    );

    mmio_write64(HPET_MAIN_COUNTER, 0);

    last_counter32 = 0;
    counter32_high = 0;

    mmio_write64(
        HPET_CONFIGURATION,
        configuration | 1ull
    );

    return true;
}

uint64_t hpet_counter(void) {
    if (hpet_base == NULL) {
        return 0;
    }

    if (counter_64bit) {
        return mmio_read64(HPET_MAIN_COUNTER);
    }

    volatile uint32_t *counter =
        (volatile uint32_t *)
            (hpet_base + HPET_MAIN_COUNTER);

    uint32_t current = *counter;

    if (current < last_counter32) {
        counter32_high += (1ull << 32);
    }

    last_counter32 = current;

    return counter32_high | current;
}

uint64_t hpet_frequency_hz(void) {
    return frequency_hz;
}

uint64_t hpet_ticks_to_ns(uint64_t ticks) {
    if (period_fs == 0) {
        return 0;
    }

    __uint128_t femtoseconds =
        (__uint128_t)ticks * period_fs;

    return (uint64_t)(
        femtoseconds / 1000000ull
    );
}
