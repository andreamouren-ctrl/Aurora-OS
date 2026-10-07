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

bool display_mode_valid(const struct aurora_display_mode *mode) {
    if (mode == NULL ||
        mode->width == 0u ||
        mode->height == 0u ||
        mode->pitch == 0u ||
        mode->format.bits_per_pixel == 0u ||
        (mode->format.bits_per_pixel % 8u) != 0u) {
        return false;
    }

    uint64_t bytes_per_pixel =
        (uint64_t)mode->format.bits_per_pixel / 8u;

    if (mode->width > UINT64_MAX / bytes_per_pixel) {
        return false;
    }

    uint64_t minimum_pitch = mode->width * bytes_per_pixel;

    if (mode->pitch < minimum_pitch) {
        return false;
    }

    if (!channel_valid(
            mode->format.red_mask_size,
            mode->format.red_mask_shift,
            mode->format.bits_per_pixel) ||
        !channel_valid(
            mode->format.green_mask_size,
            mode->format.green_mask_shift,
            mode->format.bits_per_pixel) ||
        !channel_valid(
            mode->format.blue_mask_size,
            mode->format.blue_mask_shift,
            mode->format.bits_per_pixel)) {
        return false;
    }

    return true;
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
