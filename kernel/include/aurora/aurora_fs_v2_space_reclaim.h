#ifndef AURORA_AURORA_FS_V2_SPACE_RECLAIM_H
#define AURORA_AURORA_FS_V2_SPACE_RECLAIM_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>

/*
 * Shrink a file and reclaim physical space only after the replacement inode
 * mapping has been durably published. Growth is delegated to the normal file
 * truncate path.
 */
bool aurora_fs_v2_file_truncate_reclaim(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t new_size
);

/*
 * Remove a file with safe data/metadata reclaim. The file is first reduced to
 * an empty, durably published inode; namespace removal happens afterwards.
 */
bool aurora_fs_v2_remove_child_reclaim(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    uint64_t child_inode_index,
    const char *name
);

bool aurora_fs_v2_space_reclaim_self_test(void);
bool aurora_fs_v2_space_reclaim_runtime_self_test(void);

#endif
