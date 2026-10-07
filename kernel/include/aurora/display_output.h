#ifndef AURORA_DISPLAY_OUTPUT_H
#define AURORA_DISPLAY_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#define AURORA_DISPLAY_MAX_MODES 8u

enum aurora_display_backend_kind {
    AURORA_DISPLAY_BACKEND_NONE = 0,
    AURORA_DISPLAY_BACKEND_BOOT_FRAMEBUFFER = 1
};

struct aurora_display_pixel_format {
    uint16_t bits_per_pixel;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

struct aurora_display_mode {
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint32_t refresh_numerator;
    uint32_t refresh_denominator;
    struct aurora_display_pixel_format format;
};

struct aurora_display_output {
    uint64_t id;
    enum aurora_display_backend_kind backend;
    bool connected;
    bool primary;
    uint32_t mode_count;
    uint32_t current_mode_index;
    struct aurora_display_mode modes[AURORA_DISPLAY_MAX_MODES];
};

bool display_output_init(
    struct aurora_display_output *output,
    uint64_t id,
    enum aurora_display_backend_kind backend,
    bool primary
);

bool display_output_add_mode(
    struct aurora_display_output *output,
    const struct aurora_display_mode *mode,
    bool make_current
);

bool display_output_select_mode(
    struct aurora_display_output *output,
    uint32_t mode_index
);

const struct aurora_display_mode *display_output_current_mode(
    const struct aurora_display_output *output
);

bool display_mode_valid(
    const struct aurora_display_mode *mode
);

#endif
