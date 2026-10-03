#ifndef AURORA_AURORA_FS_V2_OBJECTS_H
#define AURORA_AURORA_FS_V2_OBJECTS_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>

enum aurora_fs_v2_object_type {
    AURORA_FS_V2_OBJECT_FILE = 1,
    AURORA_FS_V2_OBJECT_DIRECTORY = 2
};

struct aurora_fs_v2_directory_entry {
    uint64_t object_id;
    enum aurora_fs_v2_object_type type;
};

bool aurora_fs_v2_object_init(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t object_id,
    uint64_t parent_object_id,
    enum aurora_fs_v2_object_type type
);

bool aurora_fs_v2_directory_append_entry(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t directory_inode_index,
    uint64_t child_object_id,
    enum aurora_fs_v2_object_type child_type,
    const char *name
);

bool aurora_fs_v2_directory_lookup_entry(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t directory_inode_index,
    const char *name,
    struct aurora_fs_v2_directory_entry *out_entry
);

bool aurora_fs_v2_create_child(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    uint64_t child_inode_index,
    uint64_t child_object_id,
    enum aurora_fs_v2_object_type child_type,
    const char *name
);

bool aurora_fs_v2_create_mkdir_self_test(void);

#endif
