#ifndef AURORA_COLOR_MANAGEMENT_H
#define AURORA_COLOR_MANAGEMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/display_output.h>

#define AURORA_COLOR_PROFILE_CURVE_SAMPLES 1025u
#define AURORA_COLOR_CALIBRATION_SAMPLES 256u
#define AURORA_COLOR_3D_LUT_EDGE 17u
#define AURORA_COLOR_3D_LUT_ENTRIES \
    (AURORA_COLOR_3D_LUT_EDGE * AURORA_COLOR_3D_LUT_EDGE * AURORA_COLOR_3D_LUT_EDGE)

struct aurora_color_output_profile {
    bool valid;
    bool has_icc_matrix;
    bool has_icc_trc;
    bool has_calibration_1d;
    bool has_calibration_3d;

    /* ICC PCS XYZ D50 -> device-linear RGB, Q16 signed. */
    int32_t pcs_to_device_q16[9];

    /*
     * Device-linear -> encoded device channel. These are inverse display TRCs
     * normalized to 0..65535.
     */
    uint16_t encode_trc[3][AURORA_COLOR_PROFILE_CURVE_SAMPLES];

    /* Post-profile calibration ramps, normalized 0..65535. */
    uint16_t calibration_1d[3][AURORA_COLOR_CALIBRATION_SAMPLES];

    /*
     * Optional 17^3 calibration LUT in RGB-major lattice order.
     * Each entry has 3 normalized uint16 channels.
     */
    uint16_t calibration_3d[AURORA_COLOR_3D_LUT_ENTRIES][3];

    uint32_t target_peak_nits_q16;
    uint32_t target_black_nits_q16;
};

bool color_management_init(void);

bool color_management_set_output_profile(
    uint32_t output_index,
    const struct aurora_color_output_profile *profile
);

bool color_management_get_output_profile(
    uint32_t output_index,
    struct aurora_color_output_profile *out_profile
);

/*
 * Bounded ICC v2/v4 display-profile import.
 *
 * Supported professional monitor-profile path:
 * - RGB matrix-shaper rXYZ/gXYZ/bXYZ;
 * - sampled curveType rTRC/gTRC/bTRC;
 * - identity/linear curveType;
 * - optional table-form vcgt calibration ramps.
 *
 * Unsupported/ambiguous profile constructs fail closed.
 */
bool color_management_parse_icc(
    const uint8_t *profile_bytes,
    size_t profile_size,
    struct aurora_color_output_profile *out_profile
);

bool color_management_set_3d_calibration(
    struct aurora_color_output_profile *profile,
    const uint16_t (*lut)[3],
    uint32_t entry_count
);

/* Exact SMPTE ST 2084 constants evaluated into a high-precision LUT. */
uint32_t color_st2084_eotf_nits_q16(uint16_t encoded_q16);

/*
 * Convert encoded source RGB to calibrated output RGB8. Alpha is handled by
 * the compositor and is intentionally not transformed here.
 */
bool color_management_transform_rgb8(
    uint32_t output_index,
    const struct aurora_color_description *source_color,
    uint16_t red_encoded_q16,
    uint16_t green_encoded_q16,
    uint16_t blue_encoded_q16,
    uint8_t *out_red,
    uint8_t *out_green,
    uint8_t *out_blue
);

bool color_management_selftest(void);

#endif
