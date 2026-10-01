#ifndef AURORA_BLOCK_DEVICE_H
#define AURORA_BLOCK_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct aurora_block_device;

typedef bool (*aurora_block_read_fn)(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
);

typedef bool (*aurora_block_write_fn)(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
);

struct aurora_block_device {
    const char *name;
    uint32_t block_size;
    uint64_t block_count;
    bool read_only;
    void *context;
    aurora_block_read_fn read_blocks;
    aurora_block_write_fn write_blocks;
};

bool block_device_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
);

bool block_device_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
);

bool block_device_self_test(void);

#endif
