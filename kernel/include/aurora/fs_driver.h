#ifndef AURORA_FS_DRIVER_H
#define AURORA_FS_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/partition.h>

#define AURORA_FS_DRIVER_MAX 16u
/* 255 UTF-16 code units can require up to 1020 UTF-8 bytes plus terminator. */
#define AURORA_FS_NAME_MAX 1024u

enum aurora_fs_probe_result {
    AURORA_FS_PROBE_NO_MATCH = 0,
    AURORA_FS_PROBE_MATCH_READ_ONLY,
    AURORA_FS_PROBE_MATCH_READ_WRITE
};

enum aurora_fs_entry_type {
    AURORA_FS_ENTRY_UNKNOWN = 0,
    AURORA_FS_ENTRY_FILE,
    AURORA_FS_ENTRY_DIRECTORY,
    AURORA_FS_ENTRY_SYMLINK,
    AURORA_FS_ENTRY_SPECIAL
};

struct aurora_fs_stat {
    enum aurora_fs_entry_type type;
    uint64_t size;
    uint64_t allocated_size;
    uint64_t created_time_ns;
    uint64_t changed_time_ns;
    uint64_t modified_time_ns;
    uint64_t accessed_time_ns;
    uint64_t filesystem_id;
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    uint32_t link_count;
};

struct aurora_fs_dirent {
    char name[AURORA_FS_NAME_MAX];
    enum aurora_fs_entry_type type;
    uint64_t size;
    uint64_t filesystem_id;
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

typedef bool (*aurora_fs_stat_fn)(
    void *context,
    const char *path,
    struct aurora_fs_stat *out_stat
);

typedef bool (*aurora_fs_readdir_fn)(
    void *context,
    const char *path,
    uint64_t index,
    struct aurora_fs_dirent *out_entry
);

typedef bool (*aurora_fs_read_fn)(
    void *context,
    const char *path,
    uint64_t offset,
    void *buffer,
    size_t length,
    size_t *out_read
);

typedef bool (*aurora_fs_write_fn)(
    void *context,
    const char *path,
    uint64_t offset,
    const void *buffer,
    size_t length,
    size_t *out_written
);

typedef bool (*aurora_fs_create_fn)(
    void *context,
    const char *path,
    enum aurora_fs_entry_type type
);

typedef bool (*aurora_fs_remove_fn)(
    void *context,
    const char *path
);

typedef bool (*aurora_fs_rename_fn)(
    void *context,
    const char *old_path,
    const char *new_path
);

typedef bool (*aurora_fs_truncate_fn)(
    void *context,
    const char *path,
    uint64_t size
);

typedef bool (*aurora_fs_chmod_fn)(
    void *context,
    const char *path,
    uint32_t mode
);

typedef bool (*aurora_fs_chown_fn)(
    void *context,
    const char *path,
    uint32_t uid,
    uint32_t gid
);

typedef bool (*aurora_fs_sync_fn)(void *context);

typedef bool (*aurora_fs_fsync_fn)(
    void *context,
    const char *path
);

typedef bool (*aurora_fs_fdatasync_fn)(
    void *context,
    const char *path
);

struct aurora_fs_driver {
    const char *name;
    aurora_fs_probe_fn probe;
    aurora_fs_mount_fn mount;
    aurora_fs_unmount_fn unmount;
    aurora_fs_stat_fn stat;
    aurora_fs_readdir_fn readdir;
    aurora_fs_read_fn read;
    aurora_fs_write_fn write;
    aurora_fs_create_fn create;
    aurora_fs_remove_fn remove;
    aurora_fs_rename_fn rename;
    aurora_fs_truncate_fn truncate;
    aurora_fs_chmod_fn chmod;
    aurora_fs_chown_fn chown;
    aurora_fs_sync_fn sync;
    aurora_fs_fsync_fn fsync;
    aurora_fs_fdatasync_fn fdatasync;
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
