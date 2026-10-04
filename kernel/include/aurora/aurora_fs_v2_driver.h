#ifndef AURORA_AURORA_FS_V2_DRIVER_H
#define AURORA_AURORA_FS_V2_DRIVER_H

#include <stdbool.h>

#include <aurora/fs_driver.h>
#include <aurora/partition.h>

const struct aurora_fs_driver *aurora_fs_v2_driver(void);

bool aurora_fs_v2_prepare_system_partition(
    const struct aurora_partition *partition,
    bool *out_formatted
);

#endif
