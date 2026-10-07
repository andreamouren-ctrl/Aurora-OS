#include <stddef.h>
#include <stdint.h>

#include <aurora/display_ddc.h>

static void clear_bytes(void *ptr, size_t length) {
    uint8_t *bytes = (uint8_t *)ptr;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

bool display_ddc_read_block(
    const struct aurora_ddc_transport *transport,
    uint8_t block_index,
    uint8_t out_block[AURORA_EDID_BLOCK_SIZE]
) {
    if (transport == NULL ||
        transport->read == NULL ||
        out_block == NULL ||
        block_index >= AURORA_DDC_MAX_EDID_BLOCKS) {
        return false;
    }

    uint8_t segment = (uint8_t)(block_index >> 1);
    uint8_t offset =
        (block_index & 1u) != 0u ? 128u : 0u;

    if (!transport->read(
            transport->context,
            segment,
            offset,
            out_block,
            AURORA_EDID_BLOCK_SIZE)) {
        return false;
    }

    return display_edid_checksum_valid(out_block);
}

bool display_ddc_read_snapshot(
    const struct aurora_ddc_transport *transport,
    struct aurora_edid_snapshot *out
) {
    if (transport == NULL || out == NULL) return false;

    clear_bytes(out, sizeof(*out));

    if (!display_ddc_read_block(
            transport,
            0u,
            out->blocks[0])) {
        return false;
    }

    struct aurora_edid_base_info base;
    if (!display_edid_parse_base(out->blocks[0], &base)) {
        return false;
    }

    uint32_t requested =
        1u + (uint32_t)base.extension_count;

    if (requested > AURORA_DDC_MAX_EDID_BLOCKS) {
        requested = AURORA_DDC_MAX_EDID_BLOCKS;
    }

    out->block_count = 1u;

    for (uint32_t i = 1u; i < requested; ++i) {
        if (!display_ddc_read_block(
                transport,
                (uint8_t)i,
                out->blocks[i])) {
            clear_bytes(out, sizeof(*out));
            return false;
        }
        out->block_count = (uint8_t)(i + 1u);
    }

    return true;
}

struct ddc_selftest_bus {
    uint8_t blocks[3][AURORA_EDID_BLOCK_SIZE];
};

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

static bool selftest_read(
    void *context,
    uint8_t segment,
    uint8_t offset,
    uint8_t *buffer,
    size_t length
) {
    struct ddc_selftest_bus *bus =
        (struct ddc_selftest_bus *)context;

    if (bus == NULL ||
        buffer == NULL ||
        length != AURORA_EDID_BLOCK_SIZE ||
        (offset != 0u && offset != 128u)) {
        return false;
    }

    uint32_t block =
        (uint32_t)segment * 2u +
        (offset == 128u ? 1u : 0u);

    if (block >= 3u) return false;

    for (uint32_t i = 0u; i < AURORA_EDID_BLOCK_SIZE; ++i) {
        buffer[i] = bus->blocks[block][i];
    }

    return true;
}

bool display_ddc_selftest(void) {
    struct ddc_selftest_bus bus = {0};

    static const uint8_t header[8] = {
        0x00u, 0xFFu, 0xFFu, 0xFFu,
        0xFFu, 0xFFu, 0xFFu, 0x00u
    };

    for (uint32_t i = 0u; i < 8u; ++i) {
        bus.blocks[0][i] = header[i];
    }

    bus.blocks[0][18] = 1u;
    bus.blocks[0][19] = 4u;
    bus.blocks[0][20] = 0x80u;
    bus.blocks[0][126] = 2u;
    finalize_checksum(bus.blocks[0]);

    bus.blocks[1][0] = AURORA_EDID_EXTENSION_CTA;
    bus.blocks[1][1] = 3u;
    bus.blocks[1][2] = 4u;
    finalize_checksum(bus.blocks[1]);

    bus.blocks[2][0] = AURORA_EDID_EXTENSION_DISPLAYID;
    bus.blocks[2][1] = 0x20u;
    bus.blocks[2][2] = 0u;
    finalize_checksum(bus.blocks[2]);

    struct aurora_ddc_transport transport = {
        .read = selftest_read,
        .context = &bus
    };

    struct aurora_edid_snapshot snapshot;

    if (!display_ddc_read_snapshot(
            &transport,
            &snapshot) ||
        snapshot.block_count != 3u ||
        snapshot.blocks[1][0] != AURORA_EDID_EXTENSION_CTA ||
        snapshot.blocks[2][0] != AURORA_EDID_EXTENSION_DISPLAYID) {
        return false;
    }

    bus.blocks[1][10] ^= 1u;

    return !display_ddc_read_snapshot(
        &transport,
        &snapshot
    );
}
