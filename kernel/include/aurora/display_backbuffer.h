#ifndef AURORA_DISPLAY_BACKBUFFER_H
#define AURORA_DISPLAY_BACKBUFFER_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/display_output.h>

struct aurora_display_backbuffer {
    uint8_t *pixels;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint64_t byte_length;
    struct aurora_display_pixel_format format;
    uint64_t generation;
    bool ready;
    bool in_flight;
};

bool display_backbuffer_init(
    struct aurora_display_backbuffer *buffer,
    const struct aurora_display_mode *mode
);

bool display_backbuffer_release(
    struct aurora_display_backbuffer *buffer
);

#endif
