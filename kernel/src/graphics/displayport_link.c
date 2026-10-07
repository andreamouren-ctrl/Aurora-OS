#include <stddef.h>
#include <stdint.h>

#include <aurora/displayport_link.h>

#define DP_DPCD_REV 0x000u
#define DP_MAX_LINK_RATE 0x001u
#define DP_MAX_LANE_COUNT 0x002u
#define DP_MAX_DOWNSPREAD 0x003u

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
    out->valid = true;
    return true;
}

struct dp_selftest_transport {
    uint8_t dpcd[AURORA_DP_DPCD_RECEIVER_CAP_SIZE];
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
        address != 0u ||
        length > sizeof(state->dpcd)) {
        return false;
    }

    for (size_t i = 0u; i < length; ++i) {
        buffer[i] = state->dpcd[i];
    }

    return true;
}

bool display_dp_link_selftest(void) {
    struct dp_selftest_transport state = {0};

    state.dpcd[DP_DPCD_REV] = 0x14u;
    state.dpcd[DP_MAX_LINK_RATE] = 0x1Eu;
    state.dpcd[DP_MAX_LANE_COUNT] =
        4u | 0x80u | 0x40u;
    state.dpcd[DP_MAX_DOWNSPREAD] =
        0x80u | 0x01u;

    struct aurora_dp_aux_transport transport = {
        .read_dpcd = selftest_aux_read,
        .context = &state,
        .embedded_displayport = true
    };

    struct aurora_dp_link_capabilities caps;

    if (!display_dp_query_link(&transport, &caps) ||
        !caps.valid ||
        !caps.embedded_displayport ||
        caps.dpcd_revision != 0x14u ||
        caps.max_link_rate_mbps != 8100u ||
        caps.max_lane_count != 4u ||
        !caps.enhanced_framing ||
        !caps.tps3_supported ||
        !caps.tps4_supported ||
        !caps.downspread_supported) {
        return false;
    }

    state.dpcd[DP_MAX_LINK_RATE] = 0xFFu;
    return !display_dp_query_link(&transport, &caps);
}
