#ifndef AURORA_AURORA_FS_V2_INODE_PUBLISH_H
#define AURORA_AURORA_FS_V2_INODE_PUBLISH_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>

bool aurora_fs_v2_inode_publish_extent_root_cow(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t expected_old_root,
    uint64_t new_root,
    const struct aurora_fs_v2_extent *extent
);

bool aurora_fs_v2_inode_publish_extent_root_self_test(void);

#endif
