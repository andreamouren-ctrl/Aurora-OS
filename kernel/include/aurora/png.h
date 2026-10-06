#ifndef AURORA_PNG_H
#define AURORA_PNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct aurora_png_image {
    uint32_t width;
    uint32_t height;
    uint8_t channels;
    uint8_t *pixels;
    size_t pixel_bytes;
};

bool aurora_png_decode(
    const uint8_t *data,
    size_t size,
    struct aurora_png_image *out
);

void aurora_png_release(struct aurora_png_image *image);

#endif
