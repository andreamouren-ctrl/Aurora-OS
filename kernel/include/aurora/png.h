#ifndef AURORA_PNG_H
#define AURORA_PNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct aurora_png_image {
    uint32_t width;
    uint32_t height;
    uint8_t *rgba;
    size_t rgba_size;
};

bool aurora_png_decode(
    const uint8_t *data,
    size_t size,
    struct aurora_png_image *out_image
);

void aurora_png_release(struct aurora_png_image *image);

#endif
