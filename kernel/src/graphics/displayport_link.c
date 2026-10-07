#include <stddef.h>
#include <stdint.h>

#include <aurora/displayport_link.h>

#define DP_DPCD_REV 0x000u
#define DP_MAX_LINK_RATE 0x001u
#define DP_MAX_LANE_COUNT 0x002u
#define DP_MAX_DOWNSPREAD 0x003u
#define DP_MAIN_LINK_CHANNEL_CODING 0x006u
#define DP_MSTM_CAP 0x021u
#define DP_DSC_SUPPORT 0x060u

#define DP_CAP_ANSI_128B132B (1u << 1)
#define DP_MST_CAP (1u << 0)
#define DP_SINGLE_STREAM_SIDEBAND_MSG (1u << 1)
#define DP_DSC_DECOMPRESSION_IS_SUPPORTED (1u << 0)
#define DP_DSC_PASSTHROUGH_IS_SUPPORTED (1u << 1)

static void clear_bytes(void *ptr, size_t length) {
    uint8_t *bytes = (uint8_t *)ptr;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static uint32_t legacy_link_rate_mbps(uint8_t code) {
    switch (code) {
        case 0x06u: return 1620u;
        case 0x0Au: return 2700u;
        case 0x14u: return 5400u;
        case 0x1Eu: return 8100u;
        default: return 0u;
    }
}

bool display_dp_query_link(
    const struct aurora_dp_aux_transport *transport,
    struct aurora_dp_link_capabilities *out
) {
    if (transport == NULL ||
        transport->read_dpcd == NULL ||
        out == NULL) {
        return false;
    }

    clear_bytes(out, sizeof(*out));

    uint8_t caps[AURORA_DP_DPCD_RECEIVER_CAP_SIZE] = {0};

    if (!transport->read_dpcd(
            transport->context,
            DP_DPCD_REV,
            caps,
            sizeof(caps))) {
        return false;
    }

    uint8_t revision = caps[DP_DPCD_REV];
    uint8_t lanes = caps[DP_MAX_LANE_COUNT] & 0x1Fu;
    uint32_t rate =
        legacy_link_rate_mbps(caps[DP_MAX_LINK_RATE]);

    if (revision < 0x10u ||
        lanes == 0u ||
        lanes > 4u ||
        rate == 0u) {
        return false;
    }

    out->embedded_displayport =
        transport->embedded_displayport;
    out->dpcd_revision = revision;
    out->max_link_rate_mbps = rate;
    out->max_lane_count = lanes;
    out->enhanced_framing =
        (caps[DP_MAX_LANE_COUNT] & 0x80u) != 0u;
    out->tps3_supported =
        (caps[DP_MAX_LANE_COUNT] & 0x40u) != 0u;
    out->tps4_supported =
        (caps[DP_MAX_DOWNSPREAD] & 0x80u) != 0u;
    out->downspread_supported =
        (caps[DP_MAX_DOWNSPREAD] & 0x01u) != 0u;

    uint8_t coding = 0u;
    uint8_t mst = 0u;
    uint8_t dsc = 0u;

    if (!transport->read_dpcd(
            transport->context,
            DP_MAIN_LINK_CHANNEL_CODING,
            &coding,
            1u) ||
        !transport->read_dpcd(
            transport->context,
            DP_MSTM_CAP,
            &mst,
            1u) ||
        !transport->read_dpcd(
            transport->context,
            DP_DSC_SUPPORT,
            &dsc,
            1u)) {
        return false;
    }

    out->mst_supported =
        (mst & DP_MST_CAP) != 0u;
    out->single_stream_sideband_supported =
        (mst & DP_SINGLE_STREAM_SIDEBAND_MSG) != 0u;
    out->dsc_supported =
        (dsc & DP_DSC_DECOMPRESSION_IS_SUPPORTED) != 0u;
    out->dsc_passthrough_supported =
        (dsc & DP_DSC_PASSTHROUGH_IS_SUPPORTED) != 0u;
    out->coding_128b132b_supported =
        (coding & DP_CAP_ANSI_128B132B) != 0u;
    out->uhbr_ready =
        revision >= 0x20u &&
        out->coding_128b132b_supported;

    out->valid = true;
    return true;
}

bool display_dp_apply_capabilities(
    const struct aurora_dp_link_capabilities *link,
    struct aurora_display_capabilities *capabilities
) {
    if (link == NULL ||
        capabilities == NULL ||
        !link->valid) {
        return false;
    }

    if (link->dsc_supported) {
        capabilities->flags |= AURORA_DISPLAY_CAP_DSC;
    }

    if (link->mst_supported) {
        capabilities->flags |= AURORA_DISPLAY_CAP_MST;
    }

    if (link->uhbr_ready) {
        capabilities->flags |= AURORA_DISPLAY_CAP_UHBR;
    }

    return true;
}

struct dp_selftest_transport {
    uint8_t dpcd[0x061u];
};

static bool selftest_aux_read(
    void *context,
    uint32_t address,
    uint8_t *buffer,
    size_t length
) {
    struct dp_selftest_transport *state =
        (struct dp_selftest_transport *)context;

    if (state == NULL ||
        buffer == NULL ||
        address >= sizeof(state->dpcd) ||
        length > sizeof(state->dpcd) - address) {
        return false;
    }

    for (size_t i = 0u; i < length; ++i) {
        buffer[i] = state->dpcd[address + i];
    }

    return true;
}

bool display_dp_link_selftest(void) {
    struct dp_selftest_transport state = {0};

    state.dpcd[DP_DPCD_REV] = 0x20u;
    state.dpcd[DP_MAX_LINK_RATE] = 0x1Eu;
    state.dpcd[DP_MAX_LANE_COUNT] =
        4u | 0x80u | 0x40u;
    state.dpcd[DP_MAX_DOWNSPREAD] =
        0x80u | 0x01u;
    state.dpcd[DP_MAIN_LINK_CHANNEL_CODING] =
        DP_CAP_ANSI_128B132B;
    state.dpcd[DP_MSTM_CAP] =
        DP_MST_CAP | DP_SINGLE_STREAM_SIDEBAND_MSG;
    state.dpcd[DP_DSC_SUPPORT] =
        DP_DSC_DECOMPRESSION_IS_SUPPORTED |
        DP_DSC_PASSTHROUGH_IS_SUPPORTED;

    struct aurora_dp_aux_transport transport = {
        .read_dpcd = selftest_aux_read,
        .context = &state,
        .embedded_displayport = true
    };

    struct aurora_dp_link_capabilities caps;

    if (!display_dp_query_link(&transport, &caps) ||
        !caps.valid ||
        !caps.embedded_displayport ||
        caps.dpcd_revision != 0x20u ||
        caps.max_link_rate_mbps != 8100u ||
        caps.max_lane_count != 4u ||
        !caps.enhanced_framing ||
        !caps.tps3_supported ||
        !caps.tps4_supported ||
        !caps.downspread_supported ||
        !caps.mst_supported ||
        !caps.single_stream_sideband_supported ||
        !caps.dsc_supported ||
        !caps.dsc_passthrough_supported ||
        !caps.coding_128b132b_supported ||
        !caps.uhbr_ready) {
        return false;
    }

    struct aurora_display_capabilities display_caps = {
        .flags = AURORA_DISPLAY_CAP_SDR,
        .min_bits_per_component = 8u,
        .max_bits_per_component = 12u
    };

    if (!display_dp_apply_capabilities(
            &caps,
            &display_caps) ||
        (display_caps.flags & AURORA_DISPLAY_CAP_DSC) == 0u ||
        (display_caps.flags & AURORA_DISPLAY_CAP_MST) == 0u ||
        (display_caps.flags & AURORA_DISPLAY_CAP_UHBR) == 0u) {
        return false;
    }

    state.dpcd[DP_MAX_LINK_RATE] = 0xFFu;
    return !display_dp_query_link(&transport, &caps);
}
