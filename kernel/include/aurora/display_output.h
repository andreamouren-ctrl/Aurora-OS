#ifndef AURORA_DISPLAY_OUTPUT_H
#define AURORA_DISPLAY_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#define AURORA_DISPLAY_MAX_MODES 8u

enum aurora_display_pixel_encoding {
    AURORA_PIXEL_ENCODING_UNORM_PACKED = 0,
    AURORA_PIXEL_ENCODING_FLOAT16 = 1
};

enum aurora_color_primaries {
    AURORA_COLOR_PRIMARIES_SRGB = 1,
    AURORA_COLOR_PRIMARIES_BT709 = 2,
    AURORA_COLOR_PRIMARIES_DISPLAY_P3_D65 = 3,
    AURORA_COLOR_PRIMARIES_BT2020 = 4
};

enum aurora_color_transfer {
    AURORA_COLOR_TRANSFER_SRGB = 1,
    AURORA_COLOR_TRANSFER_LINEAR = 2,
    AURORA_COLOR_TRANSFER_GAMMA22 = 3,
    AURORA_COLOR_TRANSFER_BT1886 = 4,
    AURORA_COLOR_TRANSFER_PQ_ST2084 = 5,
    AURORA_COLOR_TRANSFER_HLG = 6
};

enum aurora_color_range {
    AURORA_COLOR_RANGE_FULL = 1,
    AURORA_COLOR_RANGE_LIMITED = 2
};

struct aurora_hdr_static_metadata {
    bool valid;
    uint32_t mastering_max_luminance_millinit;
    uint32_t mastering_min_luminance_micrinit;
    uint16_t max_cll_nits;
    uint16_t max_fall_nits;
};

struct aurora_color_description {
    enum aurora_color_primaries primaries;
    enum aurora_color_transfer transfer;
    enum aurora_color_range range;
    struct aurora_hdr_static_metadata hdr_static;
};

#define AURORA_DISPLAY_CAP_SDR        (1ull << 0)
#define AURORA_DISPLAY_CAP_HDR_STATIC (1ull << 1)
#define AURORA_DISPLAY_CAP_PQ         (1ull << 2)
#define AURORA_DISPLAY_CAP_HLG        (1ull << 3)
#define AURORA_DISPLAY_CAP_WIDE_GAMUT (1ull << 4)
#define AURORA_DISPLAY_CAP_VRR        (1ull << 5)
#define AURORA_DISPLAY_CAP_DSC        (1ull << 6)
#define AURORA_DISPLAY_CAP_HDMI_FRL   (1ull << 7)
#define AURORA_DISPLAY_CAP_MST        (1ull << 8)
#define AURORA_DISPLAY_CAP_UHBR       (1ull << 9)

struct aurora_display_vrr_policy {
    bool enabled;
    uint32_t min_millihz;
    uint32_t max_millihz;
    uint32_t preferred_millihz;
};

struct aurora_display_capabilities {
    uint64_t flags;
    uint32_t primaries_mask;
    uint32_t transfer_mask;
    uint8_t min_bits_per_component;
    uint8_t max_bits_per_component;
    uint32_t vrr_min_millihz;
    uint32_t vrr_max_millihz;
};

enum aurora_display_backend_kind {
    AURORA_DISPLAY_BACKEND_NONE = 0,
    AURORA_DISPLAY_BACKEND_BOOT_FRAMEBUFFER = 1
};

struct aurora_display_pixel_format {
    enum aurora_display_pixel_encoding encoding;
    uint16_t bits_per_pixel;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t alpha_mask_size;
    uint8_t alpha_mask_shift;
};

struct aurora_display_mode {
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint32_t refresh_numerator;
    uint32_t refresh_denominator;
    struct aurora_display_pixel_format format;
    struct aurora_color_description color;
};

struct aurora_display_output {
    uint64_t id;
    enum aurora_display_backend_kind backend;
    bool connected;
    bool primary;
    uint32_t mode_count;
    uint32_t current_mode_index;
    struct aurora_display_capabilities capabilities;
    struct aurora_display_vrr_policy vrr_policy;
    struct aurora_display_mode modes[AURORA_DISPLAY_MAX_MODES];
};

bool display_output_init(
    struct aurora_display_output *output,
    uint64_t id,
    enum aurora_display_backend_kind backend,
    bool primary
);

bool display_output_add_mode(
    struct aurora_display_output *output,
    const struct aurora_display_mode *mode,
    bool make_current
);

bool display_output_select_mode(
    struct aurora_display_output *output,
    uint32_t mode_index
);

const struct aurora_display_mode *display_output_current_mode(
    const struct aurora_display_output *output
);

bool display_pixel_format_valid(
    const struct aurora_display_pixel_format *format
);

bool display_color_description_valid(
    const struct aurora_color_description *color
);

bool display_mode_valid(
    const struct aurora_display_mode *mode
);

bool display_output_set_capabilities(
    struct aurora_display_output *output,
    const struct aurora_display_capabilities *capabilities
);

const struct aurora_display_capabilities *display_output_capabilities(
    const struct aurora_display_output *output
);

bool display_vrr_range_valid(
    uint32_t min_millihz,
    uint32_t max_millihz
);

bool display_output_set_vrr_policy(
    struct aurora_display_output *output,
    bool enabled,
    uint32_t min_millihz,
    uint32_t max_millihz,
    uint32_t preferred_millihz
);

bool display_output_refresh_allowed(
    const struct aurora_display_output *output,
    uint32_t refresh_millihz
);

bool display_vrr_selftest(void);

#endif
