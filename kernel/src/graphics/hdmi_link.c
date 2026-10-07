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
    cta[2] = 14u;
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
        (display_caps.flags &
            AURORA_DISPLAY_CAP_HDR_STATIC) != 0u &&
        (display_caps.flags &
            AURORA_DISPLAY_CAP_PQ) != 0u &&
        (display_caps.flags &
            AURORA_DISPLAY_CAP_HLG) != 0u;
}
