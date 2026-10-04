#ifndef AURORA_AURORA_FS_V2_METADATA_H
#define AURORA_AURORA_FS_V2_METADATA_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_objects.h>

#define AURORA_FS_V2_MODE_FILE_DEFAULT 0644u
#define AURORA_FS_V2_MODE_DIRECTORY_DEFAULT 0755u
#define AURORA_FS_V2_MODE_PERMISSION_MASK 07777u

struct aurora_fs_v2_metadata {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t link_count;
    uint64_t created_time_ns;
    uint64_t changed_time_ns;
    uint64_t modified_time_ns;
    uint64_t accessed_time_ns;
    uint32_t flags;
};

bool aurora_fs_v2_metadata_read(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct aurora_fs_v2_metadata *out_metadata
);

bool aurora_fs_v2_metadata_write(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct aurora_fs_v2_metadata *metadata
);

bool aurora_fs_v2_metadata_initialize(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    enum aurora_fs_v2_object_type type,
    uint32_t uid,
    uint32_t gid,
    uint32_t mode,
    uint64_t now_ns
);

bool aurora_fs_v2_metadata_set_owner_mode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint32_t uid,
    uint32_t gid,
    uint32_t mode,
    uint64_t changed_time_ns
);

bool aurora_fs_v2_metadata_set_times(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t accessed_time_ns,
    uint64_t modified_time_ns,
    uint64_t changed_time_ns
);

bool aurora_fs_v2_metadata_self_test(void);

#endif
