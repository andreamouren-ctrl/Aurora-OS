#ifndef AURORA_DISPLAY_H
#define AURORA_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/boot.h>
#include <aurora/display_output.h>
#include <aurora/display_backbuffer.h>

#define AURORA_DISPLAY_MAX_OUTPUTS 4u

struct aurora_display_present_state {
    uint64_t next_serial;
    uint64_t last_presented_serial;
    uint64_t last_released_serial;
};

bool display_init_bootstrap(
    const struct aurora_framebuffer *framebuffer
);

uint32_t display_output_count(void);

const struct aurora_display_output *display_output_at(
    uint32_t output_index
);

const struct aurora_display_mode *display_mode_at(
    uint32_t output_index,
    uint32_t mode_index
);

bool display_present(
    uint32_t output_index,
    struct aurora_display_backbuffer *buffer,
    uint64_t *out_present_serial
);

bool display_present_state(
    uint32_t output_index,
    struct aurora_display_present_state *out_state
);

#endif
