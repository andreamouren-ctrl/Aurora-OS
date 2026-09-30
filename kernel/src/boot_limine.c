#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#include <aurora/boot.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

bool boot_protocol_supported(void) {
    return LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision);
}

bool boot_get_hhdm_offset(uint64_t *out_offset) {
    if (out_offset == NULL || hhdm_request.response == NULL) {
        return false;
    }

    *out_offset = hhdm_request.response->offset;
    return true;
}

bool boot_get_rsdp(void **out_address) {
    if (out_address == NULL ||
        rsdp_request.response == NULL ||
        rsdp_request.response->address == NULL) {
        return false;
    }

    *out_address = rsdp_request.response->address;
    return true;
}

uint64_t boot_memory_region_count(void) {
    if (memmap_request.response == NULL) {
        return 0;
    }

    return memmap_request.response->entry_count;
}

static enum aurora_memory_type map_memory_type(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE:
            return AURORA_MEMORY_USABLE;

        case LIMINE_MEMMAP_RESERVED:
            return AURORA_MEMORY_RESERVED;

        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
            return AURORA_MEMORY_ACPI_RECLAIMABLE;

        case LIMINE_MEMMAP_ACPI_NVS:
            return AURORA_MEMORY_ACPI_NVS;

        case LIMINE_MEMMAP_BAD_MEMORY:
            return AURORA_MEMORY_BAD;

        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
            return AURORA_MEMORY_BOOTLOADER_RECLAIMABLE;

        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
            return AURORA_MEMORY_KERNEL_AND_MODULES;

        case LIMINE_MEMMAP_FRAMEBUFFER:
            return AURORA_MEMORY_FRAMEBUFFER;

        case LIMINE_MEMMAP_RESERVED_MAPPED:
            return AURORA_MEMORY_RESERVED_MAPPED;

        default:
            return AURORA_MEMORY_UNKNOWN;
    }
}

bool boot_memory_region_at(
    uint64_t index,
    struct aurora_memory_region *out
) {
    if (out == NULL ||
        memmap_request.response == NULL ||
        index >= memmap_request.response->entry_count) {
        return false;
    }

    struct limine_memmap_entry *entry =
        memmap_request.response->entries[index];

    if (entry == NULL) {
        return false;
    }

    out->base = entry->base;
    out->length = entry->length;
    out->type = map_memory_type(entry->type);

    return true;
}

bool boot_get_framebuffer(struct aurora_framebuffer *out) {
    if (out == NULL) {
        return false;
    }

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count == 0) {
        return false;
    }

    struct limine_framebuffer *source =
        framebuffer_request.response->framebuffers[0];

    if (source == NULL ||
        source->address == NULL ||
        source->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        source->bpp != 32) {
        return false;
    }

    out->address = source->address;
    out->width = source->width;
    out->height = source->height;
    out->pitch = source->pitch;
    out->bpp = source->bpp;

    out->red_mask_size = source->red_mask_size;
    out->red_mask_shift = source->red_mask_shift;
    out->green_mask_size = source->green_mask_size;
    out->green_mask_shift = source->green_mask_shift;
    out->blue_mask_size = source->blue_mask_size;
    out->blue_mask_shift = source->blue_mask_shift;

    return true;
}
