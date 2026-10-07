#ifndef AURORA_DISPLAYPORT_LINK_H
#define AURORA_DISPLAYPORT_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/display_output.h>

#define AURORA_DP_DPCD_RECEIVER_CAP_SIZE 16u

typedef bool (*aurora_dp_aux_read_fn)(
    void *context,
    uint32_t address,
    uint8_t *buffer,
    size_t length
);

struct aurora_dp_aux_transport {
    aurora_dp_aux_read_fn read_dpcd;
    void *context;
    bool embedded_displayport;
};

struct aurora_dp_link_capabilities {
    bool valid;
    bool embedded_displayport;
    uint8_t dpcd_revision;
    uint32_t max_link_rate_mbps;
    uint8_t max_lane_count;
    bool enhanced_framing;
    bool tps3_supported;
    bool tps4_supported;
    bool downspread_supported;
    bool mst_supported;
    bool single_stream_sideband_supported;
    bool dsc_supported;
    bool dsc_passthrough_supported;
    bool coding_128b132b_supported;
    bool uhbr_ready;
};

bool display_dp_query_link(
    const struct aurora_dp_aux_transport *transport,
    struct aurora_dp_link_capabilities *out
);

bool display_dp_apply_capabilities(
    const struct aurora_dp_link_capabilities *link,
    struct aurora_display_capabilities *capabilities
);

bool display_dp_link_selftest(void);

#endif
