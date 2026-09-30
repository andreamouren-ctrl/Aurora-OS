#ifndef AURORA_BOOT_H
#define AURORA_BOOT_H

#include <stdbool.h>
#include <stdint.h>

enum aurora_memory_type {
    AURORA_MEMORY_USABLE = 0,
    AURORA_MEMORY_RESERVED,
    AURORA_MEMORY_ACPI_RECLAIMABLE,
    AURORA_MEMORY_ACPI_NVS,
    AURORA_MEMORY_BAD,
    AURORA_MEMORY_BOOTLOADER_RECLAIMABLE,
    AURORA_MEMORY_KERNEL_AND_MODULES,
    AURORA_MEMORY_FRAMEBUFFER,
    AURORA_MEMORY_RESERVED_MAPPED,
    AURORA_MEMORY_UNKNOWN
};

struct aurora_memory_region {
    uint64_t base;
    uint64_t length;
    enum aurora_memory_type type;
};

struct aurora_framebuffer {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;

    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

bool boot_protocol_supported(void);

bool boot_get_framebuffer(struct aurora_framebuffer *out);

bool boot_get_hhdm_offset(uint64_t *out_offset);

uint64_t boot_memory_region_count(void);
bool boot_memory_region_at(
    uint64_t index,
    struct aurora_memory_region *out
);

#endif
