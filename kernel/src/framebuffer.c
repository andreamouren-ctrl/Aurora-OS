#include <stddef.h>
#include <stdint.h>

#include <aurora/framebuffer.h>

static uint32_t scale_channel(
    uint8_t value,
    uint8_t mask_size,
    uint8_t mask_shift
) {
    if (mask_size == 0) {
        return 0;
    }

    uint64_t max_value =
        mask_size >= 32 ? 0xFFFFFFFFull : ((1ull << mask_size) - 1ull);

    uint64_t scaled = ((uint64_t)value * max_value) / 255ull;
    return (uint32_t)(scaled << mask_shift);
}

uint32_t framebuffer_rgb(
    const struct aurora_framebuffer *fb,
    uint8_t red,
    uint8_t green,
    uint8_t blue
) {
    return scale_channel(red, fb->red_mask_size, fb->red_mask_shift)
        | scale_channel(green, fb->green_mask_size, fb->green_mask_shift)
        | scale_channel(blue, fb->blue_mask_size, fb->blue_mask_shift);
}

void framebuffer_clear(
    const struct aurora_framebuffer *fb,
    uint32_t pixel
) {
    framebuffer_fill_rect(fb, 0, 0, fb->width, fb->height, pixel);
}

void framebuffer_fill_rect(
    const struct aurora_framebuffer *fb,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint32_t pixel
) {
    if (fb == NULL || fb->address == NULL || fb->bpp != 32) {
        return;
    }

    if (x >= fb->width || y >= fb->height) {
        return;
    }

    uint64_t x_end = x + width;
    uint64_t y_end = y + height;

    if (x_end < x || x_end > fb->width) {
        x_end = fb->width;
    }

    if (y_end < y || y_end > fb->height) {
        y_end = fb->height;
    }

    volatile uint8_t *base = (volatile uint8_t *)fb->address;

    for (uint64_t row = y; row < y_end; ++row) {
        volatile uint32_t *line =
            (volatile uint32_t *)(base + row * fb->pitch);

        for (uint64_t column = x; column < x_end; ++column) {
            line[column] = pixel;
        }
    }
}

void framebuffer_draw_boot_splash(
    const struct aurora_framebuffer *fb
) {
    const uint32_t background = framebuffer_rgb(fb, 7, 11, 20);
    const uint32_t aurora = framebuffer_rgb(fb, 99, 72, 255);
    const uint32_t highlight = framebuffer_rgb(fb, 100, 224, 255);
    const uint32_t soft = framebuffer_rgb(fb, 30, 39, 62);

    framebuffer_clear(fb, background);

    uint64_t bar_height = fb->height / 120;
    if (bar_height < 3) {
        bar_height = 3;
    }

    framebuffer_fill_rect(fb, 0, 0, fb->width, bar_height, aurora);

    uint64_t block_width = fb->width / 5;
    uint64_t block_height = fb->height / 80;

    if (block_width < 120) {
        block_width = 120;
    }
    if (block_width > fb->width) {
        block_width = fb->width;
    }
    if (block_height < 8) {
        block_height = 8;
    }

    uint64_t block_x = (fb->width - block_width) / 2;
    uint64_t block_y = (fb->height - block_height) / 2;

    framebuffer_fill_rect(
        fb,
        block_x,
        block_y,
        block_width,
        block_height,
        soft
    );

    framebuffer_fill_rect(
        fb,
        block_x,
        block_y,
        block_width / 3,
        block_height,
        highlight
    );
}
