#ifndef AURORA_AURORA_FS_V2_MUTATION_H
#define AURORA_AURORA_FS_V2_MUTATION_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>

bool aurora_fs_v2_rename_child(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    const char *old_name,
    const char *new_name
);

bool aurora_fs_v2_remove_child(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    uint64_t child_inode_index,
    const char *name
);

bool aurora_fs_v2_reclaim_remove_rename_self_test(void);

#endif
