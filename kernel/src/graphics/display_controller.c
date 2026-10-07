#include <stddef.h>
#include <stdint.h>

#include <aurora/display_controller.h>

static bool scanout_valid(
    const struct aurora_display_scanout *scanout
) {
    if (scanout == NULL ||
        scanout->physical_address == 0u ||
        scanout->byte_length == 0u ||
        scanout->pitch == 0u ||
        !display_pixel_format_valid(&scanout->format)) {
        return false;
    }

    uint64_t bytes_per_pixel =
        (uint64_t)scanout->format.bits_per_pixel / 8u;

    return bytes_per_pixel != 0u &&
        scanout->pitch >= bytes_per_pixel;
}

bool display_controller_modeset(
    struct aurora_display_controller *controller,
    const struct aurora_display_mode *mode
) {
    if (controller == NULL ||
        controller->ops == NULL ||
        controller->ops->set_mode == NULL ||
        !display_mode_valid(mode) ||
        controller->enabled) {
        return false;
    }

    if (!controller->ops->set_mode(
            controller->context,
            mode)) {
        return false;
    }

    controller->programmed_mode = *mode;
    controller->mode_programmed = true;
    controller->scanout_programmed = false;
    return true;
}

bool display_controller_set_scanout(
    struct aurora_display_controller *controller,
    const struct aurora_display_scanout *scanout
) {
    if (controller == NULL ||
        controller->ops == NULL ||
        controller->ops->set_scanout == NULL ||
        !controller->mode_programmed ||
        controller->enabled ||
        !scanout_valid(scanout)) {
        return false;
    }

    const struct aurora_display_mode *mode =
        &controller->programmed_mode;

    if (scanout->format.encoding != mode->format.encoding ||
        scanout->format.bits_per_pixel != mode->format.bits_per_pixel ||
        scanout->format.red_mask_size != mode->format.red_mask_size ||
        scanout->format.red_mask_shift != mode->format.red_mask_shift ||
        scanout->format.green_mask_size != mode->format.green_mask_size ||
        scanout->format.green_mask_shift != mode->format.green_mask_shift ||
        scanout->format.blue_mask_size != mode->format.blue_mask_size ||
        scanout->format.blue_mask_shift != mode->format.blue_mask_shift ||
        scanout->format.alpha_mask_size != mode->format.alpha_mask_size ||
        scanout->format.alpha_mask_shift != mode->format.alpha_mask_shift) {
        return false;
    }

    uint64_t bytes_per_pixel =
        (uint64_t)mode->format.bits_per_pixel / 8u;

    if (mode->width > UINT64_MAX / bytes_per_pixel) {
        return false;
    }

    uint64_t visible_row_bytes =
        mode->width * bytes_per_pixel;

    if (scanout->pitch < visible_row_bytes ||
        mode->height > UINT64_MAX / scanout->pitch) {
        return false;
    }

    uint64_t required_bytes =
        mode->height * scanout->pitch;

    if (scanout->byte_length < required_bytes) {
        return false;
    }

    if (!controller->ops->set_scanout(
            controller->context,
            scanout)) {
        return false;
    }

    controller->scanout_programmed = true;
    return true;
}

bool display_controller_enable(
    struct aurora_display_controller *controller
) {
    if (controller == NULL ||
        controller->ops == NULL ||
        controller->ops->set_enabled == NULL ||
        !controller->mode_programmed ||
        !controller->scanout_programmed ||
        controller->enabled) {
        return false;
    }

    if (!controller->ops->set_enabled(
            controller->context,
            true)) {
        return false;
    }

    controller->enabled = true;
    return true;
}

bool display_controller_disable(
    struct aurora_display_controller *controller
) {
    if (controller == NULL ||
        controller->ops == NULL ||
        controller->ops->set_enabled == NULL ||
        !controller->enabled) {
        return false;
    }

    if (!controller->ops->set_enabled(
            controller->context,
            false)) {
        return false;
    }

    controller->enabled = false;
    return true;
}

struct controller_probe {
    uint32_t mode_calls;
    uint32_t scanout_calls;
    uint32_t enable_calls;
    uint32_t disable_calls;
};

static bool probe_mode(
    void *context,
    const struct aurora_display_mode *mode
) {
    struct controller_probe *probe =
        (struct controller_probe *)context;

    if (probe == NULL || mode == NULL) return false;
    ++probe->mode_calls;
    return true;
}

static bool probe_scanout(
    void *context,
    const struct aurora_display_scanout *scanout
) {
    struct controller_probe *probe =
        (struct controller_probe *)context;

    if (probe == NULL || scanout == NULL) return false;
    ++probe->scanout_calls;
    return true;
}

static bool probe_enable(void *context, bool enabled) {
    struct controller_probe *probe =
        (struct controller_probe *)context;

    if (probe == NULL) return false;

    if (enabled) {
        ++probe->enable_calls;
    } else {
        ++probe->disable_calls;
    }

    return true;
}

bool display_controller_selftest(void) {
    struct controller_probe probe = {0};

    static const struct aurora_display_controller_ops ops = {
        .set_mode = probe_mode,
        .set_scanout = probe_scanout,
        .set_enabled = probe_enable
    };

    struct aurora_display_controller controller = {
        .ops = &ops,
        .context = &probe
    };

    struct aurora_display_mode mode = {
        .width = 1920u,
        .height = 1080u,
        .pitch = 7680u,
        .refresh_numerator = 60u,
        .refresh_denominator = 1u,
        .format = {
            .encoding = AURORA_PIXEL_ENCODING_UNORM_PACKED,
            .bits_per_pixel = 32u,
            .red_mask_size = 8u,
            .red_mask_shift = 16u,
            .green_mask_size = 8u,
            .green_mask_shift = 8u,
            .blue_mask_size = 8u,
            .blue_mask_shift = 0u,
            .alpha_mask_size = 8u,
            .alpha_mask_shift = 24u
        },
        .color = {
            .primaries = AURORA_COLOR_PRIMARIES_SRGB,
            .transfer = AURORA_COLOR_TRANSFER_SRGB,
            .range = AURORA_COLOR_RANGE_FULL,
            .hdr_static = { .valid = false }
        }
    };

    struct aurora_display_scanout scanout = {
        .physical_address = UINT64_C(0x100000),
        .byte_length = UINT64_C(8294400),
        .pitch = 7680u,
        .format = mode.format
    };

    if (display_controller_set_scanout(
            &controller,
            &scanout) ||
        !display_controller_modeset(
            &controller,
            &mode) ||
        !display_controller_set_scanout(
            &controller,
            &scanout) ||
        ((scanout.byte_length = 4096u),
         display_controller_set_scanout(
            &controller,
            &scanout)) ||
        ((scanout.byte_length = UINT64_C(8294400)), false) ||
        !display_controller_enable(&controller) ||
        display_controller_modeset(
            &controller,
            &mode) ||
        !display_controller_disable(&controller) ||
        probe.mode_calls != 1u ||
        probe.scanout_calls != 1u ||
        probe.enable_calls != 1u ||
        probe.disable_calls != 1u) {
        return false;
    }

    return true;
}
