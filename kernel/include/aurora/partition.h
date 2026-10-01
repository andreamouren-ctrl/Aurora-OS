#ifndef AURORA_PARTITION_H
#define AURORA_PARTITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/block_device.h>

#define AURORA_PARTITION_NAME_MAX 36u

enum aurora_partition_scheme {
    AURORA_PARTITION_SCHEME_WHOLE_DEVICE = 0,
    AURORA_PARTITION_SCHEME_MBR,
    AURORA_PARTITION_SCHEME_GPT
};

struct aurora_partition {
    struct aurora_block_device *device;
    enum aurora_partition_scheme scheme;
    uint32_t index;
    uint64_t first_lba;
    uint64_t block_count;
    uint8_t mbr_type;
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    char name[AURORA_PARTITION_NAME_MAX];
};

size_t partition_scan(
    struct aurora_block_device *device,
    struct aurora_partition *out_partitions,
    size_t capacity
);

bool partition_read(
    const struct aurora_partition *partition,
    uint64_t relative_lba,
    uint32_t block_count,
    void *buffer
);

bool partition_write(
    const struct aurora_partition *partition,
    uint64_t relative_lba,
    uint32_t block_count,
    const void *buffer
);

#endif
