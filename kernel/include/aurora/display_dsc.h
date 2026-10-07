#ifndef AURORA_DISPLAY_DSC_H
#define AURORA_DISPLAY_DSC_H

#include <stdbool.h>
#include <stdint.h>

enum aurora_dsc_transport {
    AURORA_DSC_TRANSPORT_NONE = 0,
    AURORA_DSC_TRANSPORT_DISPLAYPORT,
    AURORA_DSC_TRANSPORT_HDMI
};

struct aurora_dsc_capabilities {
    bool valid;
    enum aurora_dsc_transport transport;
    uint8_t major;
    uint8_t minor;
    uint8_t max_bits_per_component;
    uint8_t max_slices;
    uint16_t max_slice_width;
    uint16_t line_buffer_bits;
    bool block_prediction;
    bool native_422;
    bool native_420;
};

struct aurora_dsc_config {
    bool enabled;
    uint8_t bits_per_component;
    uint8_t slice_count;
    uint16_t slice_width;
    uint16_t bits_per_pixel_x16;
    bool native_422;
    bool native_420;
};

bool display_dsc_capabilities_valid(
    const struct aurora_dsc_capabilities *capabilities
);

bool display_dsc_config_valid(
    const struct aurora_dsc_capabilities *capabilities,
    const struct aurora_dsc_config *config,
    uint32_t picture_width
);

bool display_dsc_selftest(void);

#endif
