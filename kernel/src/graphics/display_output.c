#include <stddef.h>
#include <stdint.h>

#include <aurora/display_output.h>

static void clear_output(struct aurora_display_output *output) {
    uint8_t *bytes = (uint8_t *)output;

    for (uint64_t i = 0; i < sizeof(*output); ++i) {
        bytes[i] = 0u;
    }
}

static bool channel_valid(uint8_t size, uint8_t shift, uint16_t bpp) {
    if (size == 0u) {
        return false;
    }

    if (shift >= bpp || size > bpp) {
        return false;
    }

    return (uint16_t)shift + (uint16_t)size <= bpp;
}

static bool ranges_overlap(
    uint8_t shift_a,
    uint8_t size_a,
    uint8_t shift_b,
    uint8_t size_b
) {
    if (size_a == 0u || size_b == 0u) return false;

    uint16_t end_a = (uint16_t)shift_a + (uint16_t)size_a;
    uint16_t end_b = (uint16_t)shift_b + (uint16_t)size_b;

    return (uint16_t)shift_a < end_b &&
        (uint16_t)shift_b < end_a;
}

bool display_pixel_format_valid(
    const struct aurora_display_pixel_format *format
) {
    if (format == NULL ||
        format->bits_per_pixel == 0u ||
        (format->bits_per_pixel % 8u) != 0u) {
        return false;
    }

    if (format->encoding == AURORA_PIXEL_ENCODING_FLOAT16) {
        return format->bits_per_pixel == 64u &&
            format->red_mask_size == 16u &&
            format->green_mask_size == 16u &&
            format->blue_mask_size == 16u &&
            format->alpha_mask_size == 16u &&
            format->red_mask_shift == 0u &&
            format->green_mask_shift == 16u &&
            format->blue_mask_shift == 32u &&
            format->alpha_mask_shift == 48u;
    }

    if (format->encoding != AURORA_PIXEL_ENCODING_UNORM_PACKED) {
        return false;
    }

    bool supported_integer =
        (format->bits_per_pixel == 24u &&
         format->red_mask_size == 8u &&
         format->green_mask_size == 8u &&
         format->blue_mask_size == 8u &&
         format->alpha_mask_size == 0u) ||
        (format->bits_per_pixel == 32u &&
         ((format->red_mask_size == 8u &&
           format->green_mask_size == 8u &&
           format->blue_mask_size == 8u &&
           (format->alpha_mask_size == 0u ||
            format->alpha_mask_size == 8u)) ||
          (format->red_mask_size == 10u &&
           format->green_mask_size == 10u &&
           format->blue_mask_size == 10u &&
           format->alpha_mask_size == 2u))) ||
        (format->bits_per_pixel == 48u &&
         format->red_mask_size == 12u &&
         format->green_mask_size == 12u &&
         format->blue_mask_size == 12u &&
         format->alpha_mask_size == 0u);

    if (!supported_integer ||
        !channel_valid(format->red_mask_size, format->red_mask_shift, format->bits_per_pixel) ||
        !channel_valid(format->green_mask_size, format->green_mask_shift, format->bits_per_pixel) ||
        !channel_valid(format->blue_mask_size, format->blue_mask_shift, format->bits_per_pixel)) {
        return false;
    }

    if (format->alpha_mask_size != 0u &&
        !channel_valid(format->alpha_mask_size, format->alpha_mask_shift, format->bits_per_pixel)) {
        return false;
    }

    if (ranges_overlap(format->red_mask_shift, format->red_mask_size,
                       format->green_mask_shift, format->green_mask_size) ||
        ranges_overlap(format->red_mask_shift, format->red_mask_size,
                       format->blue_mask_shift, format->blue_mask_size) ||
        ranges_overlap(format->green_mask_shift, format->green_mask_size,
                       format->blue_mask_shift, format->blue_mask_size) ||
        (format->alpha_mask_size != 0u &&
         (ranges_overlap(format->alpha_mask_shift, format->alpha_mask_size,
                         format->red_mask_shift, format->red_mask_size) ||
          ranges_overlap(format->alpha_mask_shift, format->alpha_mask_size,
                         format->green_mask_shift, format->green_mask_size) ||
          ranges_overlap(format->alpha_mask_shift, format->alpha_mask_size,
                         format->blue_mask_shift, format->blue_mask_size)))) {
        return false;
    }

    return true;
}

bool display_color_description_valid(
    const struct aurora_color_description *color
) {
    if (color == NULL ||
        color->primaries < AURORA_COLOR_PRIMARIES_SRGB ||
        color->primaries > AURORA_COLOR_PRIMARIES_BT2020 ||
        color->transfer < AURORA_COLOR_TRANSFER_SRGB ||
        color->transfer > AURORA_COLOR_TRANSFER_HLG ||
        color->range < AURORA_COLOR_RANGE_FULL ||
        color->range > AURORA_COLOR_RANGE_LIMITED) {
        return false;
    }

    if (color->hdr_static.valid &&
        color->transfer != AURORA_COLOR_TRANSFER_PQ_ST2084 &&
        color->transfer != AURORA_COLOR_TRANSFER_HLG) {
        return false;
    }

    if ((color->transfer == AURORA_COLOR_TRANSFER_PQ_ST2084 ||
         color->transfer == AURORA_COLOR_TRANSFER_HLG) &&
        color->primaries != AURORA_COLOR_PRIMARIES_BT2020) {
        return false;
    }

    return true;
}

bool display_mode_valid(const struct aurora_display_mode *mode) {
    if (mode == NULL ||
        mode->width == 0u ||
        mode->height == 0u ||
        mode->pitch == 0u ||
        !display_pixel_format_valid(&mode->format) ||
        !display_color_description_valid(&mode->color)) {
        return false;
    }

    uint64_t bytes_per_pixel =
        (uint64_t)mode->format.bits_per_pixel / 8u;

    if (mode->width > UINT64_MAX / bytes_per_pixel) {
        return false;
    }

    return mode->pitch >= mode->width * bytes_per_pixel;
}

bool display_output_init(
    struct aurora_display_output *output,
    uint64_t id,
    enum aurora_display_backend_kind backend,
    bool primary
) {
    if (output == NULL ||
        id == 0u ||
        backend == AURORA_DISPLAY_BACKEND_NONE) {
        return false;
    }

    clear_output(output);
    output->id = id;
    output->backend = backend;
    output->connected = true;
    output->primary = primary;
    return true;
}

bool display_output_add_mode(
    struct aurora_display_output *output,
    const struct aurora_display_mode *mode,
    bool make_current
) {
    if (output == NULL ||
        !output->connected ||
        !display_mode_valid(mode) ||
        output->mode_count >= AURORA_DISPLAY_MAX_MODES) {
        return false;
    }

    uint32_t index = output->mode_count;
    output->modes[index] = *mode;
    output->mode_count = index + 1u;

    if (make_current || output->mode_count == 1u) {
        output->current_mode_index = index;
    }

    return true;
}

bool display_output_select_mode(
    struct aurora_display_output *output,
    uint32_t mode_index
) {
    if (output == NULL ||
        !output->connected ||
        mode_index >= output->mode_count) {
        return false;
    }

    output->current_mode_index = mode_index;
    return true;
}

const struct aurora_display_mode *display_output_current_mode(
    const struct aurora_display_output *output
) {
    if (output == NULL ||
        !output->connected ||
        output->mode_count == 0u ||
        output->current_mode_index >= output->mode_count) {
        return NULL;
    }

    return &output->modes[output->current_mode_index];
}

bool display_output_set_capabilities(
    struct aurora_display_output *output,
    const struct aurora_display_capabilities *capabilities
) {
    if (output == NULL ||
        capabilities == NULL ||
        !output->connected ||
        capabilities->min_bits_per_component == 0u ||
        capabilities->max_bits_per_component <
            capabilities->min_bits_per_component ||
        ((capabilities->flags & AURORA_DISPLAY_CAP_VRR) != 0u &&
         (capabilities->vrr_min_millihz == 0u ||
          capabilities->vrr_max_millihz <
              capabilities->vrr_min_millihz))) {
        return false;
    }

    output->capabilities = *capabilities;
    return true;
}

const struct aurora_display_capabilities *display_output_capabilities(
    const struct aurora_display_output *output
) {
    if (output == NULL || !output->connected) return NULL;
    return &output->capabilities;
}
