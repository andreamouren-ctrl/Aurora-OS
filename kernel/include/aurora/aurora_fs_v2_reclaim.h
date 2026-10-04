#ifndef AURORA_AURORA_FS_V2_RECLAIM_H
#define AURORA_AURORA_FS_V2_RECLAIM_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>

struct aurora_fs_v2_trim_result {
    uint64_t new_root;
    uint64_t removed_data_blocks;
    bool changed;
};

/*
 * Build a COW replacement for the right edge of an extent tree so mappings
 * at logical blocks >= keep_logical_blocks disappear. Unchanged left-side
 * subtrees remain shared. No block reachable from old_root is freed here.
 */
bool aurora_fs_v2_extent_tree_trim_tail_cow(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root,
    uint64_t keep_logical_blocks,
    struct aurora_fs_v2_trim_result *out_result
);

/*
 * Call only after the inode has durably published the replacement root.
 * Frees removed data extents plus only the old metadata nodes replaced by
 * the trim; shared retained subtrees are never reclaimed.
 */
bool aurora_fs_v2_extent_tree_reclaim_old_tail(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root,
    uint64_t keep_logical_blocks
);

bool aurora_fs_v2_tree_reclaim_self_test(void);

#endif
