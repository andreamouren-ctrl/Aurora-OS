#ifndef AURORA_FRAMEBUFFER_H
#define AURORA_FRAMEBUFFER_H

#include <stdint.h>
#include <aurora/boot.h>

uint32_t framebuffer_rgb(
    const struct aurora_framebuffer *fb,
    uint8_t red,
    uint8_t green,
    uint8_t blue
);

void framebuffer_clear(
    const struct aurora_framebuffer *fb,
    uint32_t pixel
);

void framebuffer_fill_rect(
    const struct aurora_framebuffer *fb,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint32_t pixel
);

void framebuffer_draw_boot_splash(
    const struct aurora_framebuffer *fb
);

#endif
