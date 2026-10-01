#ifndef AURORA_BLOCK_DEVICE_H
#define AURORA_BLOCK_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURORA_BLOCK_DEVICE_REGISTRY_MAX 16u

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

typedef bool (*aurora_block_flush_fn)(
    struct aurora_block_device *device
);

struct aurora_block_device {
    const char *name;
    uint32_t block_size;
    uint64_t block_count;
    bool read_only;
    void *context;
    aurora_block_read_fn read_blocks;
    aurora_block_write_fn write_blocks;
    aurora_block_flush_fn flush;
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

bool block_device_flush(struct aurora_block_device *device);

void block_device_registry_init(void);
bool block_device_register(struct aurora_block_device *device);
size_t block_device_count(void);
struct aurora_block_device *block_device_get(size_t index);
struct aurora_block_device *block_device_find(const char *name);

bool block_device_self_test(void);

#endif
