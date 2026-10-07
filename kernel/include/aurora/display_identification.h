#ifndef AURORA_DISPLAY_IDENTIFICATION_H
#define AURORA_DISPLAY_IDENTIFICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/display_output.h>

#define AURORA_EDID_BLOCK_SIZE 128u
#define AURORA_EDID_EXTENSION_CTA 0x02u
#define AURORA_EDID_EXTENSION_DISPLAYID 0x70u

struct aurora_edid_base_info {
    bool valid;
    bool digital_input;
    uint16_t manufacturer_id;
    uint16_t product_code;
    uint32_t serial_number;
    uint8_t version;
    uint8_t revision;
    uint8_t extension_count;
    uint16_t preferred_width;
    uint16_t preferred_height;
    uint32_t preferred_refresh_millihz;
};

struct aurora_cta861_info {
    bool valid;
    uint8_t revision;
    bool basic_audio;
    bool ycbcr444;
    bool ycbcr422;
    bool ycbcr420;
    bool bt2020_rgb;
    bool bt2020_ycc;
    bool bt2020_cycc;
    bool hdr_static_metadata;
    bool eotf_pq;
    bool eotf_hlg;
    bool static_metadata_type1;
    uint8_t max_luminance_code;
    uint8_t max_fall_code;
    uint8_t min_luminance_code;
    bool hdmi_vsdb;
    bool hdmi_forum_vsdb;
    bool scdc_present;
    bool read_request_capable;
    bool allm;
    bool fast_vactive;
    bool dsc_1p2;
    uint32_t max_tmds_clock_khz;
    uint8_t max_frl_rate_code;
    uint8_t max_frl_lanes;
    uint8_t max_frl_gbps_per_lane;
    bool vrr_supported;
    uint16_t vrr_min_hz;
    uint16_t vrr_max_hz;
};

struct aurora_displayid_info {
    bool valid;
    uint8_t structure_revision;
    uint8_t payload_bytes;
    uint8_t product_type_or_primary_use;
    uint8_t extension_count;
    uint8_t data_block_count;
    bool display_parameters;
    bool color_characteristics;
    bool detailed_timing;
    bool dynamic_video_timing;
    bool display_interface_features;
    bool tiled_topology;
    bool embedded_cta;
};

bool display_edid_checksum_valid(
    const uint8_t block[AURORA_EDID_BLOCK_SIZE]
);

bool display_edid_parse_base(
    const uint8_t block[AURORA_EDID_BLOCK_SIZE],
    struct aurora_edid_base_info *out
);

bool display_cta861_parse(
    const uint8_t block[AURORA_EDID_BLOCK_SIZE],
    struct aurora_cta861_info *out
);

bool display_displayid_parse(
    const uint8_t block[AURORA_EDID_BLOCK_SIZE],
    struct aurora_displayid_info *out
);

bool display_cta861_apply_capabilities(
    const struct aurora_cta861_info *cta,
    struct aurora_display_capabilities *capabilities
);

bool display_identification_selftest(void);

#endif
