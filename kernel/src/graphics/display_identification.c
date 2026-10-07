#include <stddef.h>
#include <stdint.h>

#include <aurora/display_identification.h>

#define CTA_DB_EXTENDED_TAG 7u
#define CTA_EXT_COLORIMETRY 5u
#define CTA_EXT_HDR_STATIC_METADATA 6u
#define CTA_EXT_Y420_VIDEO 14u
#define CTA_EXT_Y420_CAP_MAP 15u

#define DISPLAYID_BLOCK_PRODUCT_ID_V1 0x00u
#define DISPLAYID_BLOCK_DISPLAY_PARAMETERS_V1 0x01u
#define DISPLAYID_BLOCK_COLOR_CHARACTERISTICS_V1 0x02u
#define DISPLAYID_BLOCK_DETAILED_TIMING_V1 0x03u
#define DISPLAYID_BLOCK_TILED_V1 0x12u
#define DISPLAYID_BLOCK_CTA 0x81u

#define DISPLAYID_BLOCK_PRODUCT_ID_V2 0x20u
#define DISPLAYID_BLOCK_DISPLAY_PARAMETERS_V2 0x21u
#define DISPLAYID_BLOCK_DETAILED_TIMING_V2 0x22u
#define DISPLAYID_BLOCK_DYNAMIC_VIDEO_TIMING_V2 0x25u
#define DISPLAYID_BLOCK_DISPLAY_INTERFACE_FEATURES_V2 0x26u
#define DISPLAYID_BLOCK_TILED_V2 0x28u

static void clear_bytes(void *ptr, size_t length) {
    uint8_t *bytes = (uint8_t *)ptr;
    for (size_t i = 0u; i < length; ++i) {
        bytes[i] = 0u;
    }
}

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] |
        ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

bool display_edid_checksum_valid(
    const uint8_t block[AURORA_EDID_BLOCK_SIZE]
) {
    if (block == NULL) return false;

    uint8_t sum = 0u;
    for (uint32_t i = 0u; i < AURORA_EDID_BLOCK_SIZE; ++i) {
        sum = (uint8_t)(sum + block[i]);
    }

    return sum == 0u;
}

bool display_edid_parse_base(
    const uint8_t block[AURORA_EDID_BLOCK_SIZE],
    struct aurora_edid_base_info *out
) {
    static const uint8_t header[8] = {
        0x00u, 0xFFu, 0xFFu, 0xFFu,
        0xFFu, 0xFFu, 0xFFu, 0x00u
    };

    if (block == NULL || out == NULL) return false;
    clear_bytes(out, sizeof(*out));

    for (uint32_t i = 0u; i < 8u; ++i) {
        if (block[i] != header[i]) return false;
    }

    if (!display_edid_checksum_valid(block)) return false;

    out->manufacturer_id =
        ((uint16_t)block[8] << 8) |
        (uint16_t)block[9];
    out->product_code = read_le16(&block[10]);
    out->serial_number = read_le32(&block[12]);
    out->version = block[18];
    out->revision = block[19];
    out->digital_input = (block[20] & 0x80u) != 0u;
    out->extension_count = block[126];

    if (out->version != 1u) return false;

    const uint8_t *dtd = &block[54];
    uint16_t pixel_clock_10khz = read_le16(dtd);

    if (pixel_clock_10khz != 0u) {
        uint16_t hactive =
            (uint16_t)dtd[2] |
            ((uint16_t)(dtd[4] & 0xF0u) << 4);
        uint16_t hblank =
            (uint16_t)dtd[3] |
            ((uint16_t)(dtd[4] & 0x0Fu) << 8);
        uint16_t vactive =
            (uint16_t)dtd[5] |
            ((uint16_t)(dtd[7] & 0xF0u) << 4);
        uint16_t vblank =
            (uint16_t)dtd[6] |
            ((uint16_t)(dtd[7] & 0x0Fu) << 8);

        out->preferred_width = hactive;
        out->preferred_height = vactive;

        uint32_t htotal =
            (uint32_t)hactive + (uint32_t)hblank;
        uint32_t vtotal =
            (uint32_t)vactive + (uint32_t)vblank;

        if (htotal != 0u &&
            vtotal != 0u &&
            htotal <= UINT32_MAX / vtotal) {
            uint32_t total_pixels = htotal * vtotal;
            uint64_t pixel_clock_millihz =
                (uint64_t)pixel_clock_10khz *
                UINT64_C(10000000);

            out->preferred_refresh_millihz =
                (uint32_t)(
                    pixel_clock_millihz /
                    total_pixels
                );
        }
    }

    out->valid = true;
    return true;
}

bool display_cta861_parse(
    const uint8_t block[AURORA_EDID_BLOCK_SIZE],
    struct aurora_cta861_info *out
) {
    if (block == NULL || out == NULL) return false;
    clear_bytes(out, sizeof(*out));

    if (block[0] != AURORA_EDID_EXTENSION_CTA ||
        block[1] < 3u ||
        !display_edid_checksum_valid(block)) {
        return false;
    }

    uint8_t dtd_offset = block[2];

    if (dtd_offset != 0u &&
        (dtd_offset < 4u || dtd_offset > 127u)) {
        return false;
    }

    uint32_t collection_end =
        dtd_offset == 0u ? 4u : (uint32_t)dtd_offset;

    out->revision = block[1];
    out->basic_audio = (block[3] & (1u << 6)) != 0u;
    out->ycbcr444 = (block[3] & (1u << 5)) != 0u;
    out->ycbcr422 = (block[3] & (1u << 4)) != 0u;

    uint32_t index = 4u;

    while (index < collection_end) {
        uint8_t header = block[index++];
        uint8_t tag = header >> 5;
        uint8_t length = header & 0x1Fu;

        if (length == 0u ||
            index + length > collection_end) {
            return false;
        }

        const uint8_t *payload = &block[index];

        if (tag == CTA_DB_EXTENDED_TAG) {
            uint8_t ext_tag = payload[0];

            if (ext_tag == CTA_EXT_COLORIMETRY &&
                length >= 2u) {
                uint8_t flags = payload[1];

                out->bt2020_cycc =
                    (flags & (1u << 5)) != 0u;
                out->bt2020_ycc =
                    (flags & (1u << 6)) != 0u;
                out->bt2020_rgb =
                    (flags & (1u << 7)) != 0u;
            } else if (
                ext_tag == CTA_EXT_HDR_STATIC_METADATA &&
                length >= 3u) {
                uint8_t eotf = payload[1];
                uint8_t metadata = payload[2];

                out->hdr_static_metadata = true;
                out->eotf_pq =
                    (eotf & (1u << 2)) != 0u;
                out->eotf_hlg =
                    (eotf & (1u << 3)) != 0u;
                out->static_metadata_type1 =
                    (metadata & (1u << 0)) != 0u;

                if (length >= 4u) {
                    out->max_luminance_code =
                        payload[3];
                }
                if (length >= 5u) {
                    out->max_fall_code =
                        payload[4];
                }
                if (length >= 6u) {
                    out->min_luminance_code =
                        payload[5];
                }
            } else if (
                ext_tag == CTA_EXT_Y420_VIDEO ||
                ext_tag == CTA_EXT_Y420_CAP_MAP) {
                out->ycbcr420 = true;
            }
        }

        index += length;
    }

    out->valid = true;
    return true;
}

bool display_displayid_parse(
    const uint8_t block[AURORA_EDID_BLOCK_SIZE],
    struct aurora_displayid_info *out
) {
    if (block == NULL || out == NULL) return false;
    clear_bytes(out, sizeof(*out));

    if (block[0] != AURORA_EDID_EXTENSION_DISPLAYID ||
        !display_edid_checksum_valid(block)) {
        return false;
    }

    uint8_t structure_revision = block[1];
    uint8_t payload_bytes = block[2];

    if (payload_bytes > 121u) {
        return false;
    }

    uint32_t payload_end =
        5u + (uint32_t)payload_bytes;

    if (payload_end > 127u) {
        return false;
    }

    out->structure_revision = structure_revision;
    out->payload_bytes = payload_bytes;
    out->product_type_or_primary_use = block[3];
    out->extension_count = block[4];

    uint32_t index = 5u;

    while (index < payload_end) {
        if (payload_end - index < 3u) {
            return false;
        }

        uint8_t tag = block[index];
        uint8_t num_bytes = block[index + 2u];
        uint32_t next =
            index + 3u + (uint32_t)num_bytes;

        if (next > payload_end) {
            return false;
        }

        if (out->data_block_count == UINT8_MAX) {
            return false;
        }
        ++out->data_block_count;

        switch (tag) {
            case DISPLAYID_BLOCK_DISPLAY_PARAMETERS_V1:
            case DISPLAYID_BLOCK_DISPLAY_PARAMETERS_V2:
                out->display_parameters = true;
                break;

            case DISPLAYID_BLOCK_COLOR_CHARACTERISTICS_V1:
                out->color_characteristics = true;
                break;

            case DISPLAYID_BLOCK_DETAILED_TIMING_V1:
            case DISPLAYID_BLOCK_DETAILED_TIMING_V2:
                out->detailed_timing = true;
                break;

            case DISPLAYID_BLOCK_DYNAMIC_VIDEO_TIMING_V2:
                out->dynamic_video_timing = true;
                break;

            case DISPLAYID_BLOCK_DISPLAY_INTERFACE_FEATURES_V2:
                out->display_interface_features = true;
                break;

            case DISPLAYID_BLOCK_TILED_V1:
            case DISPLAYID_BLOCK_TILED_V2:
                out->tiled_topology = true;
                break;

            case DISPLAYID_BLOCK_CTA:
                out->embedded_cta = true;
                break;

            default:
                break;
        }

        index = next;
    }

    if (index != payload_end) return false;

    out->valid = true;
    return true;
}

bool display_cta861_apply_capabilities(
    const struct aurora_cta861_info *cta,
    struct aurora_display_capabilities *capabilities
) {
    if (cta == NULL ||
        capabilities == NULL ||
        !cta->valid) {
        return false;
    }

    capabilities->flags |= AURORA_DISPLAY_CAP_SDR;

    if (cta->bt2020_rgb ||
        cta->bt2020_ycc ||
        cta->bt2020_cycc) {
        capabilities->flags |=
            AURORA_DISPLAY_CAP_WIDE_GAMUT;
        capabilities->primaries_mask |=
            (1u << AURORA_COLOR_PRIMARIES_BT2020);
    }

    if (cta->hdr_static_metadata) {
        capabilities->flags |=
            AURORA_DISPLAY_CAP_HDR_STATIC;
    }

    if (cta->eotf_pq) {
        capabilities->flags |=
            AURORA_DISPLAY_CAP_PQ;
        capabilities->transfer_mask |=
            (1u << AURORA_COLOR_TRANSFER_PQ_ST2084);
    }

    if (cta->eotf_hlg) {
        capabilities->flags |=
            AURORA_DISPLAY_CAP_HLG;
        capabilities->transfer_mask |=
            (1u << AURORA_COLOR_TRANSFER_HLG);
    }

    return true;
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

bool display_identification_selftest(void) {
    uint8_t base[AURORA_EDID_BLOCK_SIZE] = {0};

    base[0] = 0x00u;
    base[1] = 0xFFu;
    base[2] = 0xFFu;
    base[3] = 0xFFu;
    base[4] = 0xFFu;
    base[5] = 0xFFu;
    base[6] = 0xFFu;
    base[7] = 0x00u;
    base[8] = 0x12u;
    base[9] = 0x34u;
    base[10] = 0x78u;
    base[11] = 0x56u;
    base[12] = 0x04u;
    base[13] = 0x03u;
    base[14] = 0x02u;
    base[15] = 0x01u;
    base[18] = 1u;
    base[19] = 4u;
    base[20] = 0x80u;
    base[54] = 0x02u;
    base[55] = 0x3Au;
    base[56] = 0x80u;
    base[57] = 0x18u;
    base[58] = 0x71u;
    base[59] = 0x38u;
    base[60] = 0x2Du;
    base[61] = 0x40u;
    base[126] = 2u;
    finalize_checksum(base);

    struct aurora_edid_base_info base_info;

    if (!display_edid_parse_base(base, &base_info) ||
        !base_info.valid ||
        !base_info.digital_input ||
        base_info.manufacturer_id != UINT16_C(0x1234) ||
        base_info.product_code != UINT16_C(0x5678) ||
        base_info.serial_number != UINT32_C(0x01020304) ||
        base_info.extension_count != 2u ||
        base_info.preferred_width != 1920u ||
        base_info.preferred_height != 1080u ||
        base_info.preferred_refresh_millihz != 60000u) {
        return false;
    }

    uint8_t broken[AURORA_EDID_BLOCK_SIZE];
    for (uint32_t i = 0u; i < AURORA_EDID_BLOCK_SIZE; ++i) {
        broken[i] = base[i];
    }
    broken[10] ^= 1u;

    if (display_edid_parse_base(broken, &base_info)) {
        return false;
    }

    uint8_t cta[AURORA_EDID_BLOCK_SIZE] = {0};
    cta[0] = AURORA_EDID_EXTENSION_CTA;
    cta[1] = 3u;
    cta[2] = 14u;
    cta[3] = (1u << 6) | (1u << 5) | (1u << 4);

    cta[4] = (uint8_t)((7u << 5) | 2u);
    cta[5] = CTA_EXT_COLORIMETRY;
    cta[6] = (1u << 5) | (1u << 6) | (1u << 7);

    cta[7] = (uint8_t)((7u << 5) | 6u);
    cta[8] = CTA_EXT_HDR_STATIC_METADATA;
    cta[9] = (1u << 2) | (1u << 3);
    cta[10] = 1u;
    cta[11] = 100u;
    cta[12] = 80u;
    cta[13] = 5u;

    finalize_checksum(cta);

    struct aurora_cta861_info cta_info;

    if (!display_cta861_parse(cta, &cta_info) ||
        !cta_info.valid ||
        !cta_info.basic_audio ||
        !cta_info.ycbcr444 ||
        !cta_info.ycbcr422 ||
        !cta_info.bt2020_cycc ||
        !cta_info.bt2020_ycc ||
        !cta_info.bt2020_rgb ||
        !cta_info.hdr_static_metadata ||
        !cta_info.eotf_pq ||
        !cta_info.eotf_hlg ||
        !cta_info.static_metadata_type1 ||
        cta_info.max_luminance_code != 100u ||
        cta_info.max_fall_code != 80u ||
        cta_info.min_luminance_code != 5u) {
        return false;
    }

    struct aurora_display_capabilities capabilities = {
        .flags = AURORA_DISPLAY_CAP_SDR,
        .primaries_mask =
            (1u << AURORA_COLOR_PRIMARIES_SRGB),
        .transfer_mask =
            (1u << AURORA_COLOR_TRANSFER_SRGB),
        .min_bits_per_component = 8u,
        .max_bits_per_component = 12u
    };

    if (!display_cta861_apply_capabilities(
            &cta_info,
            &capabilities) ||
        (capabilities.flags & AURORA_DISPLAY_CAP_HDR_STATIC) == 0u ||
        (capabilities.flags & AURORA_DISPLAY_CAP_PQ) == 0u ||
        (capabilities.flags & AURORA_DISPLAY_CAP_HLG) == 0u ||
        (capabilities.flags & AURORA_DISPLAY_CAP_WIDE_GAMUT) == 0u) {
        return false;
    }

    uint8_t displayid[AURORA_EDID_BLOCK_SIZE] = {0};
    displayid[0] = AURORA_EDID_EXTENSION_DISPLAYID;
    displayid[1] = 0x20u;
    displayid[2] = 20u;
    displayid[3] = 5u;
    displayid[4] = 0u;

    const uint8_t tags[5] = {
        DISPLAYID_BLOCK_DISPLAY_PARAMETERS_V2,
        DISPLAYID_BLOCK_DYNAMIC_VIDEO_TIMING_V2,
        DISPLAYID_BLOCK_DISPLAY_INTERFACE_FEATURES_V2,
        DISPLAYID_BLOCK_TILED_V2,
        DISPLAYID_BLOCK_CTA
    };

    uint32_t offset = 5u;

    for (uint32_t i = 0u; i < 5u; ++i) {
        displayid[offset] = tags[i];
        displayid[offset + 1u] = 0u;
        displayid[offset + 2u] = 1u;
        displayid[offset + 3u] = 0u;
        offset += 4u;
    }

    finalize_checksum(displayid);

    struct aurora_displayid_info displayid_info;

    if (!display_displayid_parse(
            displayid,
            &displayid_info) ||
        !displayid_info.valid ||
        displayid_info.structure_revision != 0x20u ||
        displayid_info.data_block_count != 5u ||
        !displayid_info.display_parameters ||
        !displayid_info.dynamic_video_timing ||
        !displayid_info.display_interface_features ||
        !displayid_info.tiled_topology ||
        !displayid_info.embedded_cta) {
        return false;
    }

    return true;
}
