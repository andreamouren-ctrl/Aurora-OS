#ifndef AURORA_FS_MOUNT_H
#define AURORA_FS_MOUNT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/fs_driver.h>

#define AURORA_FS_MOUNT_MAX 16u
#define AURORA_FS_MOUNT_PATH_MAX 128u

struct aurora_fs_mount {
    bool active;
    char mount_path[AURORA_FS_MOUNT_PATH_MAX];
    struct aurora_partition partition;
    const struct aurora_fs_driver *driver;
    enum aurora_fs_probe_result access;
    void *context;
};

void fs_mount_manager_init(void);

bool fs_mount_partition(
    const char *mount_path,
    const struct aurora_partition *partition,
    struct aurora_fs_mount **out_mount
);

bool fs_unmount(const char *mount_path);

size_t fs_mount_count(void);
const struct aurora_fs_mount *fs_mount_at(size_t index);

const struct aurora_fs_mount *fs_mount_resolve(
    const char *absolute_path,
    const char **out_relative_path
);

#endif
