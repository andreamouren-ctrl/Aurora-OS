#include <stddef.h>
#include <stdint.h>

#include <aurora/display_ddc.h>
#include <aurora/hdmi_link.h>

static void clear_bytes(void *ptr, size_t length) {
    uint8_t *bytes = (uint8_t *)ptr;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

bool display_hdmi_query_sink(
    const struct aurora_edid_snapshot *snapshot,
    struct aurora_hdmi_sink_capabilities *out,
    struct aurora_display_capabilities *display_caps
) {
    if (snapshot == NULL ||
        out == NULL ||
        display_caps == NULL ||
        snapshot->block_count == 0u) {
        return false;
    }

    clear_bytes(out, sizeof(*out));

    struct aurora_edid_base_info base;
    if (!display_edid_parse_base(
            snapshot->blocks[0],
            &base) ||
        !base.digital_input) {
        return false;
    }

    out->digital_sink = true;

    for (uint32_t i = 1u;
         i < snapshot->block_count;
         ++i) {
        if (snapshot->blocks[i][0] !=
            AURORA_EDID_EXTENSION_CTA) {
            continue;
        }

        struct aurora_cta861_info cta;
        if (!display_cta861_parse(
                snapshot->blocks[i],
                &cta)) {
            return false;
        }

        out->cta_present = true;
        out->basic_audio |= cta.basic_audio;
        out->ycbcr444 |= cta.ycbcr444;
        out->ycbcr422 |= cta.ycbcr422;
        out->ycbcr420 |= cta.ycbcr420;
        out->hdr_static_metadata |=
            cta.hdr_static_metadata;
        out->pq |= cta.eotf_pq;
        out->hlg |= cta.eotf_hlg;
        out->bt2020 |=
            cta.bt2020_rgb ||
            cta.bt2020_ycc ||
            cta.bt2020_cycc;
        out->hdmi_vsdb |= cta.hdmi_vsdb;
        out->hdmi_forum_vsdb |= cta.hdmi_forum_vsdb;
        out->scdc_present |= cta.scdc_present;
        out->read_request_capable |= cta.read_request_capable;
        out->allm |= cta.allm;
        out->fast_vactive |= cta.fast_vactive;
        out->dsc_1p2 |= cta.dsc_1p2;

        if (cta.max_tmds_clock_khz > out->max_tmds_clock_khz) {
            out->max_tmds_clock_khz = cta.max_tmds_clock_khz;
        }

        if (cta.max_frl_rate_code > out->max_frl_rate_code) {
            out->max_frl_rate_code = cta.max_frl_rate_code;
            out->max_frl_lanes = cta.max_frl_lanes;
            out->max_frl_gbps_per_lane =
                cta.max_frl_gbps_per_lane;
        }

        if (cta.vrr_supported) {
            out->vrr_supported = true;
            out->vrr_min_hz = cta.vrr_min_hz;
            out->vrr_max_hz = cta.vrr_max_hz;
        }

        if (!display_cta861_apply_capabilities(
                &cta,
                display_caps)) {
            return false;
        }
    }

    /*
     * Presence of CTA identifies a modern digital consumer-display
     * capability path. HDMI VSDB/HF-VSDB parsing will later refine TMDS,
     * FRL, VRR and vendor-specific transport limits.
     */
    out->valid = out->cta_present;
    return out->valid;
}

static void finalize_checksum(
    uint8_t block[AURORA_EDID_BLOCK_SIZE]
) {
    uint8_t sum = 0u;
    for (uint32_t i = 0u; i < AURORA_EDID_BLOCK_SIZE - 1u; ++i) {
        sum = (uint8_t)(sum + block[i]);
    }
    block[AURORA_EDID_BLOCK_SIZE - 1u] =
        (uint8_t)(0u - sum);
}

bool display_hdmi_link_selftest(void) {
    struct aurora_edid_snapshot snapshot = {0};
    snapshot.block_count = 2u;

    static const uint8_t header[8] = {
        0x00u, 0xFFu, 0xFFu, 0xFFu,
        0xFFu, 0xFFu, 0xFFu, 0x00u
    };

    for (uint32_t i = 0u; i < 8u; ++i) {
        snapshot.blocks[0][i] = header[i];
    }

    snapshot.blocks[0][18] = 1u;
    snapshot.blocks[0][19] = 4u;
    snapshot.blocks[0][20] = 0x80u;
    snapshot.blocks[0][126] = 1u;
    finalize_checksum(snapshot.blocks[0]);

    uint8_t *cta = snapshot.blocks[1];
    cta[0] = AURORA_EDID_EXTENSION_CTA;
    cta[1] = 3u;
    cta[2] = 34u;
    cta[3] = (1u << 6) | (1u << 5) | (1u << 4);

    cta[4] = (uint8_t)((7u << 5) | 2u);
    cta[5] = 5u;
    cta[6] = (1u << 6) | (1u << 7);

    cta[7] = (uint8_t)((7u << 5) | 6u);
    cta[8] = 6u;
    cta[9] = (1u << 2) | (1u << 3);
    cta[10] = 1u;
    cta[11] = 100u;
    cta[12] = 80u;
    cta[13] = 5u;

    /* HDMI Licensing, LLC VSDB: 300 MHz maximum TMDS clock. */
    cta[14] = (uint8_t)((3u << 5) | 7u);
    cta[15] = 0x03u;
    cta[16] = 0x0Cu;
    cta[17] = 0x00u;
    cta[18] = 0u;
    cta[19] = 0u;
    cta[20] = 0u;
    cta[21] = 60u;

    /* HDMI Forum VSDB: 600 MHz TMDS, FRL12x4, VRR 48-144, DSC 1.2. */
    cta[22] = (uint8_t)((3u << 5) | 11u);
    cta[23] = 0xD8u;
    cta[24] = 0x5Du;
    cta[25] = 0xC4u;
    cta[26] = 1u;
    cta[27] = 120u;
    cta[28] = 0xC0u;
    cta[29] = (uint8_t)(6u << 4);
    cta[30] = 0x06u;
    cta[31] = 48u;
    cta[32] = 144u;
    cta[33] = 0x80u;

    finalize_checksum(cta);

    struct aurora_display_capabilities display_caps = {
        .flags = AURORA_DISPLAY_CAP_SDR,
        .primaries_mask =
            (1u << AURORA_COLOR_PRIMARIES_SRGB),
        .transfer_mask =
            (1u << AURORA_COLOR_TRANSFER_SRGB),
        .min_bits_per_component = 8u,
        .max_bits_per_component = 12u
    };

    struct aurora_hdmi_sink_capabilities caps;

    return display_hdmi_query_sink(
            &snapshot,
            &caps,
            &display_caps) &&
        caps.valid &&
        caps.digital_sink &&
        caps.cta_present &&
        caps.basic_audio &&
        caps.ycbcr444 &&
        caps.ycbcr422 &&
        caps.hdr_static_metadata &&
        caps.pq &&
        caps.hlg &&
        caps.bt2020 &&
        caps.hdmi_vsdb &&
        caps.hdmi_forum_vsdb &&
        caps.scdc_present &&
        caps.read_request_capable &&
        caps.allm &&
        caps.fast_vactive &&
        caps.dsc_1p2 &&
        caps.max_tmds_clock_khz == 600000u &&
        caps.max_frl_rate_code == 6u &&
        caps.max_frl_lanes == 4u &&
        caps.max_frl_gbps_per_lane == 12u &&
        caps.vrr_supported &&
        caps.vrr_min_hz == 48u &&
        caps.vrr_max_hz == 144u &&
        (display_caps.flags & AURORA_DISPLAY_CAP_HDMI_FRL) != 0u &&
        (display_caps.flags & AURORA_DISPLAY_CAP_VRR) != 0u &&
        (display_caps.flags & AURORA_DISPLAY_CAP_DSC) != 0u &&
        display_caps.vrr_min_millihz == 48000u &&
        display_caps.vrr_max_millihz == 144000u &&
        (display_caps.flags &
            AURORA_DISPLAY_CAP_HDR_STATIC) != 0u &&
        (display_caps.flags &
            AURORA_DISPLAY_CAP_PQ) != 0u &&
        (display_caps.flags &
            AURORA_DISPLAY_CAP_HLG) != 0u;
}
