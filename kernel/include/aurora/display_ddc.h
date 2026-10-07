#ifndef AURORA_DISPLAY_DDC_H
#define AURORA_DISPLAY_DDC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/display_identification.h>

#define AURORA_DDC_MAX_EDID_BLOCKS 8u

typedef bool (*aurora_ddc_read_fn)(
    void *context,
    uint8_t segment,
    uint8_t offset,
    uint8_t *buffer,
    size_t length
);

struct aurora_ddc_transport {
    aurora_ddc_read_fn read;
    void *context;
};

struct aurora_edid_snapshot {
    uint8_t block_count;
    uint8_t blocks[AURORA_DDC_MAX_EDID_BLOCKS][AURORA_EDID_BLOCK_SIZE];
};

bool display_ddc_read_block(
    const struct aurora_ddc_transport *transport,
    uint8_t block_index,
    uint8_t out_block[AURORA_EDID_BLOCK_SIZE]
);

bool display_ddc_read_snapshot(
    const struct aurora_ddc_transport *transport,
    struct aurora_edid_snapshot *out
);

bool display_ddc_selftest(void);

#endif
