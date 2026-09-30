#ifndef AURORA_BOOT_H
#define AURORA_BOOT_H

#include <stdbool.h>
#include <stdint.h>

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

#endif
