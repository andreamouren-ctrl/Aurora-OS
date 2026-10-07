#include <stddef.h>
#include <stdint.h>

#include <aurora/display_boot_framebuffer.h>

static void clear_backend(struct aurora_boot_framebuffer_backend *backend) {
    uint8_t *bytes = (uint8_t *)backend;

    for (uint64_t i = 0; i < sizeof(*backend); ++i) {
        bytes[i] = 0u;
    }
}

bool display_boot_framebuffer_init(
    struct aurora_boot_framebuffer_backend *backend,
    const struct aurora_framebuffer *framebuffer
) {
    if (backend == NULL ||
        framebuffer == NULL ||
        framebuffer->address == NULL ||
        framebuffer->width == 0u ||
        framebuffer->height == 0u ||
        framebuffer->pitch == 0u ||
        framebuffer->bpp == 0u) {
        return false;
    }

    clear_backend(backend);

    struct aurora_display_mode mode = {
        .width = framebuffer->width,
        .height = framebuffer->height,
        .pitch = framebuffer->pitch,
        .refresh_numerator = 0u,
        .refresh_denominator = 0u,
        .format = {
            .bits_per_pixel = framebuffer->bpp,
            .red_mask_size = framebuffer->red_mask_size,
            .red_mask_shift = framebuffer->red_mask_shift,
            .green_mask_size = framebuffer->green_mask_size,
            .green_mask_shift = framebuffer->green_mask_shift,
            .blue_mask_size = framebuffer->blue_mask_size,
            .blue_mask_shift = framebuffer->blue_mask_shift
        }
    };

    if (!display_output_init(
            &backend->output,
            AURORA_BOOT_FRAMEBUFFER_OUTPUT_ID,
            AURORA_DISPLAY_BACKEND_BOOT_FRAMEBUFFER,
            true) ||
        !display_output_add_mode(
            &backend->output,
            &mode,
            true)) {
        clear_backend(backend);
        return false;
    }

    backend->framebuffer = *framebuffer;
    backend->ready = true;
    return true;
}

const struct aurora_display_output *display_boot_framebuffer_output(
    const struct aurora_boot_framebuffer_backend *backend
) {
    if (backend == NULL || !backend->ready) {
        return NULL;
    }

    return &backend->output;
}

const struct aurora_framebuffer *display_boot_framebuffer_native(
    const struct aurora_boot_framebuffer_backend *backend
) {
    if (backend == NULL || !backend->ready) {
        return NULL;
    }

    return &backend->framebuffer;
}
