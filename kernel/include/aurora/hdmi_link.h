#ifndef AURORA_HDMI_LINK_H
#define AURORA_HDMI_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/display_ddc.h>
#include <aurora/display_identification.h>

struct aurora_hdmi_sink_capabilities {
    bool valid;
    bool digital_sink;
    bool cta_present;
    bool basic_audio;
    bool ycbcr444;
    bool ycbcr422;
    bool ycbcr420;
    bool hdr_static_metadata;
    bool pq;
    bool hlg;
    bool bt2020;
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

bool display_hdmi_query_sink(
    const struct aurora_edid_snapshot *snapshot,
    struct aurora_hdmi_sink_capabilities *out,
    struct aurora_display_capabilities *display_caps
);

bool display_hdmi_link_selftest(void);

#endif
