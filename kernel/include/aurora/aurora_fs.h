#ifndef AURORA_AURORA_FS_H
#define AURORA_AURORA_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/block_device.h>

#define AURORA_FS_BOOTSTRAP_BASE_LBA 8u
#define AURORA_FS_BOOTSTRAP_MAX_FILE_SIZE 512u
#define AURORA_FS_BOOTSTRAP_NAME_MAX 31u

struct aurora_fs_bootstrap_result {
    bool formatted;
    bool reopened_existing_file;
    uint64_t generation;
};

bool aurora_fs_bootstrap_probe(
    struct aurora_block_device *device,
    struct aurora_fs_bootstrap_result *out_result
);

#endif
