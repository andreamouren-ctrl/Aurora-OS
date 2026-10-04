#ifndef AURORA_AURORA_FS_V2_NAMESPACE_TXN_H
#define AURORA_AURORA_FS_V2_NAMESPACE_TXN_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_objects.h>

bool aurora_fs_v2_create_child_txn(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    uint64_t child_inode_index,
    uint64_t child_object_id,
    enum aurora_fs_v2_object_type child_type,
    const char *name
);

bool aurora_fs_v2_rename_child_txn(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    const char *old_name,
    const char *new_name
);

bool aurora_fs_v2_remove_child_txn(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    uint64_t child_inode_index,
    const char *name
);

bool aurora_fs_v2_namespace_txn_self_test(void);

#endif
