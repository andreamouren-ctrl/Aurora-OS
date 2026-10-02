#ifndef AURORA_AURORA_FS_V2_H
#define AURORA_AURORA_FS_V2_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/block_device.h>

#define AURORA_FS_V2_FS_BLOCK_SIZE 4096u
#define AURORA_FS_V2_DEFAULT_BASE_BYTES 4096u
#define AURORA_FS_V2_INLINE_EXTENT_COUNT 4u

struct aurora_fs_v2_allocator {
    struct aurora_block_device *device;
    uint64_t base_bytes;
    uint64_t total_fs_blocks;
    uint64_t bitmap_start;
    uint64_t bitmap_blocks;
    uint64_t data_start;
};

struct aurora_fs_v2_format_geometry {
    uint64_t base_bytes;
    uint64_t total_fs_blocks;
    uint64_t bitmap_start;
    uint64_t bitmap_blocks;
    uint64_t inode_start;
    uint64_t inode_blocks;
    uint64_t data_start;
};

struct aurora_fs_v2_extent {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
};

bool aurora_fs_v2_allocator_init(
    struct aurora_fs_v2_allocator *allocator,
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t total_fs_blocks,
    uint64_t bitmap_start,
    uint64_t bitmap_blocks,
    uint64_t data_start
);

bool aurora_fs_v2_allocator_is_allocated(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t fs_block,
    bool *out_allocated
);

bool aurora_fs_v2_allocator_allocate_range(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t block_count,
    uint64_t *out_first_block
);

bool aurora_fs_v2_allocator_free_range(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t first_block,
    uint64_t block_count
);

bool aurora_fs_v2_format_device(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t initial_data_blocks,
    struct aurora_fs_v2_format_geometry *out_geometry
);

bool aurora_fs_v2_extent_tree_write(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_extent *extents,
    uint32_t extent_count,
    uint64_t *out_root_block
);

bool aurora_fs_v2_extent_tree_lookup(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t root_block,
    uint64_t logical_block,
    uint64_t *out_physical_block,
    uint64_t *out_contiguous_blocks
);

bool aurora_fs_v2_extent_tree_clone_append_leaf(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root_block,
    const struct aurora_fs_v2_extent *extent,
    uint64_t *out_new_root_block
);

bool aurora_fs_v2_extent_tree_expand_full_leaf_cow(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root_block,
    const struct aurora_fs_v2_extent *extent,
    uint64_t *out_new_root_block
);

bool aurora_fs_v2_inode_extent_init(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t object_id,
    uint64_t parent_object_id
);

bool aurora_fs_v2_inode_extent_append(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct aurora_fs_v2_extent *extent
);

bool aurora_fs_v2_inode_extent_append_tree_cow(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct aurora_fs_v2_extent *extent
);

bool aurora_fs_v2_inode_extent_lookup(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t logical_block,
    uint64_t *out_physical_block,
    uint64_t *out_contiguous_blocks
);

bool aurora_fs_v2_formatter_self_test(void);
bool aurora_fs_v2_allocator_self_test(void);
bool aurora_fs_v2_extent_tree_self_test(void);
bool aurora_fs_v2_extent_tree_growth_self_test(void);
bool aurora_fs_v2_inode_extent_self_test(void);
bool aurora_fs_v2_inode_tree_append_self_test(void);

#endif
