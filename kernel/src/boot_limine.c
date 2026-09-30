#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#include <aurora/boot.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
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
