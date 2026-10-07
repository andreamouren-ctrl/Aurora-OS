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
            .encoding = AURORA_PIXEL_ENCODING_UNORM_PACKED,
            .bits_per_pixel = framebuffer->bpp,
            .red_mask_size = framebuffer->red_mask_size,
            .red_mask_shift = framebuffer->red_mask_shift,
            .green_mask_size = framebuffer->green_mask_size,
            .green_mask_shift = framebuffer->green_mask_shift,
            .blue_mask_size = framebuffer->blue_mask_size,
            .blue_mask_shift = framebuffer->blue_mask_shift,
            .alpha_mask_size = 0u,
            .alpha_mask_shift = 0u
        },
        .color = {
            .primaries = AURORA_COLOR_PRIMARIES_SRGB,
            .transfer = AURORA_COLOR_TRANSFER_SRGB,
            .range = AURORA_COLOR_RANGE_FULL,
            .hdr_static = { .valid = false }
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

    struct aurora_display_capabilities capabilities = {
        .flags = AURORA_DISPLAY_CAP_SDR,
        .primaries_mask =
            (1u << AURORA_COLOR_PRIMARIES_SRGB) |
            (1u << AURORA_COLOR_PRIMARIES_BT709),
        .transfer_mask =
            (1u << AURORA_COLOR_TRANSFER_SRGB) |
            (1u << AURORA_COLOR_TRANSFER_BT1886),
        .min_bits_per_component = 8u,
        .max_bits_per_component = 8u,
        .vrr_min_millihz = 0u,
        .vrr_max_millihz = 0u
    };

    if (!display_output_set_capabilities(
            &backend->output,
            &capabilities)) {
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

static bool same_format(
    const struct aurora_display_pixel_format *a,
    const struct aurora_display_pixel_format *b
) {
    return a->encoding == b->encoding &&
        a->bits_per_pixel == b->bits_per_pixel &&
        a->red_mask_size == b->red_mask_size &&
        a->red_mask_shift == b->red_mask_shift &&
        a->green_mask_size == b->green_mask_size &&
        a->green_mask_shift == b->green_mask_shift &&
        a->blue_mask_size == b->blue_mask_size &&
        a->blue_mask_shift == b->blue_mask_shift &&
        a->alpha_mask_size == b->alpha_mask_size &&
        a->alpha_mask_shift == b->alpha_mask_shift;
}

bool display_boot_framebuffer_present(
    const struct aurora_boot_framebuffer_backend *backend,
    const struct aurora_display_backbuffer *buffer
) {
    if (backend == NULL ||
        !backend->ready ||
        buffer == NULL ||
        !buffer->ready ||
        buffer->pixels == NULL) {
        return false;
    }

    const struct aurora_display_mode *mode =
        display_output_current_mode(&backend->output);

    if (mode == NULL ||
        buffer->width != mode->width ||
        buffer->height != mode->height ||
        buffer->pitch != mode->pitch ||
        !same_format(&buffer->format, &mode->format) ||
        mode->format.bits_per_pixel == 0u ||
        (mode->format.bits_per_pixel % 8u) != 0u) {
        return false;
    }

    uint64_t bytes_per_pixel =
        (uint64_t)mode->format.bits_per_pixel / 8u;

    if (mode->width > UINT64_MAX / bytes_per_pixel) {
        return false;
    }

    uint64_t visible_row_bytes =
        mode->width * bytes_per_pixel;

    if (visible_row_bytes > mode->pitch ||
        mode->height > UINT64_MAX / mode->pitch ||
        buffer->byte_length < mode->height * mode->pitch) {
        return false;
    }

    volatile uint8_t *destination =
        (volatile uint8_t *)backend->framebuffer.address;

    if (destination == NULL) {
        return false;
    }

    for (uint64_t row = 0u; row < mode->height; ++row) {
        const uint8_t *source_row =
            buffer->pixels + row * buffer->pitch;
        volatile uint8_t *destination_row =
            destination + row * backend->framebuffer.pitch;

        for (uint64_t byte = 0u;
             byte < visible_row_bytes;
             ++byte) {
            destination_row[byte] = source_row[byte];
        }
    }

    return true;
}
