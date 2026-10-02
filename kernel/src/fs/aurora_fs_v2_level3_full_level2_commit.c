#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_inode_publish.h>

bool aurora_fs_v2_inode_append_level3_full_level2_cow_commit(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t expected_old_root,
    const struct aurora_fs_v2_extent *extent
) {
    if (allocator == NULL || geometry == NULL || extent == NULL) return false;

    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_append_level3_full_level2_cow(
            allocator, expected_old_root, extent, &new_root)) return false;

    return aurora_fs_v2_inode_publish_extent_root_cow(
        allocator,
        geometry,
        inode_index,
        expected_old_root,
        new_root,
        extent);
}
