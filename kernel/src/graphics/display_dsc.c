#include <stddef.h>
#include <stdint.h>

#include <aurora/display_dsc.h>

bool display_dsc_capabilities_valid(
    const struct aurora_dsc_capabilities *capabilities
) {
    if (capabilities == NULL ||
        !capabilities->valid ||
        capabilities->transport == AURORA_DSC_TRANSPORT_NONE ||
        capabilities->major == 0u ||
        capabilities->max_bits_per_component < 8u ||
        capabilities->max_bits_per_component > 16u ||
        capabilities->max_slices == 0u ||
        capabilities->max_slices > 16u ||
        capabilities->max_slice_width == 0u ||
        capabilities->line_buffer_bits < 8u ||
        capabilities->line_buffer_bits > 16u) {
        return false;
    }

    return true;
}

bool display_dsc_config_valid(
    const struct aurora_dsc_capabilities *capabilities,
    const struct aurora_dsc_config *config,
    uint32_t picture_width
) {
    if (!display_dsc_capabilities_valid(capabilities) ||
        config == NULL ||
        !config->enabled ||
        picture_width == 0u ||
        config->bits_per_component < 8u ||
        config->bits_per_component > capabilities->max_bits_per_component ||
        config->slice_count == 0u ||
        config->slice_count > capabilities->max_slices ||
        config->slice_width == 0u ||
        config->slice_width > capabilities->max_slice_width ||
        config->bits_per_pixel_x16 < 96u ||
        config->bits_per_pixel_x16 > 512u) {
        return false;
    }

    if ((config->native_422 && !capabilities->native_422) ||
        (config->native_420 && !capabilities->native_420) ||
        (config->native_422 && config->native_420)) {
        return false;
    }

    uint64_t covered =
        (uint64_t)config->slice_count *
        (uint64_t)config->slice_width;

    return covered >= picture_width;
}

bool display_dsc_selftest(void) {
    struct aurora_dsc_capabilities caps = {
        .valid = true,
        .transport = AURORA_DSC_TRANSPORT_DISPLAYPORT,
        .major = 1u,
        .minor = 2u,
        .max_bits_per_component = 12u,
        .max_slices = 8u,
        .max_slice_width = 2048u,
        .line_buffer_bits = 13u,
        .block_prediction = true,
        .native_422 = true,
        .native_420 = true
    };

    struct aurora_dsc_config config = {
        .enabled = true,
        .bits_per_component = 10u,
        .slice_count = 2u,
        .slice_width = 1920u,
        .bits_per_pixel_x16 = 128u,
        .native_422 = false,
        .native_420 = false
    };

    if (!display_dsc_capabilities_valid(&caps) ||
        !display_dsc_config_valid(&caps, &config, 3840u)) {
        return false;
    }

    config.bits_per_component = 16u;
    if (display_dsc_config_valid(&caps, &config, 3840u)) {
        return false;
    }

    config.bits_per_component = 10u;
    config.slice_count = 1u;
    return !display_dsc_config_valid(&caps, &config, 3840u);
}
