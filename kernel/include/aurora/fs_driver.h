#ifndef AURORA_FS_DRIVER_H
#define AURORA_FS_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/partition.h>

#define AURORA_FS_DRIVER_MAX 16u

enum aurora_fs_probe_result {
    AURORA_FS_PROBE_NO_MATCH = 0,
    AURORA_FS_PROBE_MATCH_READ_ONLY,
    AURORA_FS_PROBE_MATCH_READ_WRITE
};

struct aurora_fs_driver;

typedef enum aurora_fs_probe_result (*aurora_fs_probe_fn)(
    const struct aurora_partition *partition
);

typedef bool (*aurora_fs_mount_fn)(
    const struct aurora_partition *partition,
    void **out_context
);

typedef void (*aurora_fs_unmount_fn)(void *context);

struct aurora_fs_driver {
    const char *name;
    aurora_fs_probe_fn probe;
    aurora_fs_mount_fn mount;
    aurora_fs_unmount_fn unmount;
};

struct aurora_fs_match {
    const struct aurora_fs_driver *driver;
    enum aurora_fs_probe_result access;
};

void fs_driver_registry_init(void);
bool fs_driver_register(const struct aurora_fs_driver *driver);
size_t fs_driver_count(void);
const struct aurora_fs_driver *fs_driver_at(size_t index);

bool fs_driver_detect(
    const struct aurora_partition *partition,
    struct aurora_fs_match *out_match
);

#endif
