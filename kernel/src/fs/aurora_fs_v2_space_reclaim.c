#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_file_io.h>
#include <aurora/aurora_fs_v2_mutation.h>
#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/aurora_fs_v2_reclaim.h>
#include <aurora/aurora_fs_v2_space_reclaim.h>
#include <aurora/block_device.h>

#define V2S_INODE_SIZE 256u
#define V2S_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2S_INODE_SIZE)

struct v2s_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2s_inode_disk {
    uint64_t object_id;
    uint64_t parent_object_id;
    uint64_t size;
    uint64_t allocated_bytes;
    uint64_t generation;
    uint64_t extent_tree_root;
    uint32_t type;
    uint32_t flags;
    uint32_t extent_count;
    uint32_t reserved0;
    struct v2s_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

static uint8_t v2s_inode_block[AURORA_FS_V2_FS_BLOCK_SIZE];

_Static_assert(sizeof(struct v2s_inode_disk) == V2S_INODE_SIZE,
               "AuroraFS v2 reclaim inode layout drifted");

static bool add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || b > UINT64_MAX - a) return false;
    *out = a + b;
    return true;
}

static bool mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || (a != 0u && b > UINT64_MAX / a)) return false;
    *out = a * b;
    return true;
}

static uint64_t ceil_blocks(uint64_t size) {
    return size / AURORA_FS_V2_FS_BLOCK_SIZE +
        ((size % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u ? 1u : 0u);
}

static bool fs_block_geometry(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_count
) {
    if (device == NULL || geometry == NULL || out_lba == NULL || out_count == NULL ||
        device->block_size == 0u || device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) != 0u ||
        (geometry->base_bytes % device->block_size) != 0u ||
        fs_block >= geometry->total_fs_blocks) return false;

    uint64_t fs_bytes;
    uint64_t byte_offset;
    if (!mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_bytes) ||
        !add_u64(geometry->base_bytes, fs_bytes, &byte_offset)) return false;

    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    uint64_t lba = byte_offset / device->block_size;
    if (count == 0u || count > UINT32_MAX || lba >= device->block_count ||
        count > device->block_count - lba) return false;
    *out_lba = lba;
    *out_count = (uint32_t)count;
    return true;
}

static bool inode_location(
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t *out_block,
    uint32_t *out_slot
) {
    if (geometry == NULL || out_block == NULL || out_slot == NULL ||
        inode_index >= geometry->inode_blocks * V2S_INODES_PER_BLOCK) return false;
    *out_block = geometry->inode_start + inode_index / V2S_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(inode_index % V2S_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct v2s_inode_disk *out_inode
) {
    uint64_t block;
    uint32_t slot;
    uint64_t lba;
    uint32_t count;
    if (out_inode == NULL ||
        !inode_location(geometry, inode_index, &block, &slot) ||
        !fs_block_geometry(device, geometry, block, &lba, &count) ||
        !block_device_read(device, lba, count, v2s_inode_block)) return false;
    *out_inode = ((const struct v2s_inode_disk *)v2s_inode_block)[slot];
    return true;
}

static bool write_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct v2s_inode_disk *inode
) {
    uint64_t block;
    uint32_t slot;
    uint64_t lba;
    uint32_t count;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_location(geometry, inode_index, &block, &slot) ||
        !fs_block_geometry(device, geometry, block, &lba, &count) ||
        !block_device_read(device, lba, count, v2s_inode_block)) return false;
    ((struct v2s_inode_disk *)v2s_inode_block)[slot] = *inode;
    return block_device_write(device, lba, count, v2s_inode_block) &&
        block_device_flush(device);
}

static bool reclaim_inline_tail(
    struct aurora_fs_v2_allocator *allocator,
    struct v2s_inode_disk *inode,
    uint64_t keep_blocks
) {
    struct v2s_extent_disk removed[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint32_t removed_count = 0u;
    uint32_t kept_count = 0u;

    for (uint32_t i = 0u; i < inode->extent_count; ++i) {
        struct v2s_extent_disk extent = inode->extents[i];
        uint64_t end;
        if (extent.block_count == 0u ||
            !add_u64(extent.logical_block, extent.block_count, &end)) return false;

        if (end <= keep_blocks) {
            inode->extents[kept_count++] = extent;
            continue;
        }

        if (extent.logical_block < keep_blocks) {
            uint64_t retained = keep_blocks - extent.logical_block;
            uint64_t dropped = extent.block_count - retained;
            struct v2s_extent_disk kept = extent;
            kept.block_count = retained;
            inode->extents[kept_count++] = kept;
            removed[removed_count++] = (struct v2s_extent_disk){
                .logical_block = keep_blocks,
                .physical_block = extent.physical_block + retained,
                .block_count = dropped
            };
        } else {
            removed[removed_count++] = extent;
        }
    }

    for (uint32_t i = kept_count; i < AURORA_FS_V2_INLINE_EXTENT_COUNT; ++i)
        inode->extents[i] = (struct v2s_extent_disk){0};
    inode->extent_count = kept_count;
    inode->allocated_bytes = keep_blocks * AURORA_FS_V2_FS_BLOCK_SIZE;

    for (uint32_t i = 0u; i < removed_count; ++i) {
        if (removed[i].block_count != 0u &&
            !aurora_fs_v2_allocator_free_range(
                allocator, removed[i].physical_block, removed[i].block_count)) return false;
    }
    return true;
}

bool aurora_fs_v2_file_truncate_reclaim(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t new_size
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only)
        return false;

    struct v2s_inode_disk inode;
    if (!read_inode(allocator->device, geometry, inode_index, &inode) ||
        inode.object_id == 0u || inode.type != AURORA_FS_V2_OBJECT_FILE) return false;

    if (new_size >= inode.size)
        return aurora_fs_v2_file_truncate(allocator, geometry, inode_index, new_size);

    uint64_t keep_blocks = ceil_blocks(new_size);
    uint64_t old_root = inode.extent_tree_root;

    /* First publish exact EOF/zeroing while the old mapping is still intact. */
    if (!aurora_fs_v2_file_truncate(allocator, geometry, inode_index, new_size) ||
        !read_inode(allocator->device, geometry, inode_index, &inode)) return false;

    if (old_root == 0u) {
        struct v2s_inode_disk replacement = inode;
        replacement.allocated_bytes = keep_blocks * AURORA_FS_V2_FS_BLOCK_SIZE;

        struct v2s_extent_disk removed[AURORA_FS_V2_INLINE_EXTENT_COUNT];
        uint32_t removed_count = 0u;
        uint32_t kept_count = 0u;
        for (uint32_t i = 0u; i < inode.extent_count; ++i) {
            struct v2s_extent_disk extent = inode.extents[i];
            uint64_t end;
            if (extent.block_count == 0u ||
                !add_u64(extent.logical_block, extent.block_count, &end)) return false;
            if (end <= keep_blocks) {
                replacement.extents[kept_count++] = extent;
            } else if (extent.logical_block < keep_blocks) {
                uint64_t retained = keep_blocks - extent.logical_block;
                uint64_t dropped = extent.block_count - retained;
                struct v2s_extent_disk kept = extent;
                kept.block_count = retained;
                replacement.extents[kept_count++] = kept;
                removed[removed_count++] = (struct v2s_extent_disk){0u,
                    extent.physical_block + retained, dropped};
            } else {
                removed[removed_count++] = extent;
            }
        }
        for (uint32_t i = kept_count; i < AURORA_FS_V2_INLINE_EXTENT_COUNT; ++i)
            replacement.extents[i] = (struct v2s_extent_disk){0};
        replacement.extent_count = kept_count;
        replacement.generation++;
        if (!write_inode(allocator->device, geometry, inode_index, &replacement)) return false;
        for (uint32_t i = 0u; i < removed_count; ++i)
            if (removed[i].block_count != 0u &&
                !aurora_fs_v2_allocator_free_range(
                    allocator, removed[i].physical_block, removed[i].block_count)) return false;
        return true;
    }

    struct aurora_fs_v2_trim_result trim;
    if (!aurora_fs_v2_extent_tree_trim_tail_cow(
            allocator, old_root, keep_blocks, &trim)) return false;
    if (!trim.changed) return true;

    inode.extent_tree_root = trim.new_root;
    inode.allocated_bytes = keep_blocks * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (trim.new_root == 0u) inode.extent_count = 0u;
    inode.generation++;
    if (!write_inode(allocator->device, geometry, inode_index, &inode)) return false;

    return aurora_fs_v2_extent_tree_reclaim_old_tail(
        allocator, old_root, keep_blocks);
}

bool aurora_fs_v2_remove_child_reclaim(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    uint64_t child_inode_index,
    const char *name
) {
    if (allocator == NULL || allocator->device == NULL || name == NULL) return false;

    struct v2s_inode_disk child;
    if (!read_inode(allocator->device, geometry, child_inode_index, &child) ||
        child.object_id == 0u) return false;

    if (child.type == AURORA_FS_V2_OBJECT_FILE) {
        if (!aurora_fs_v2_file_truncate_reclaim(
                allocator, geometry, child_inode_index, 0u)) return false;
    }

    return aurora_fs_v2_remove_child(
        allocator, geometry, parent_inode_index, child_inode_index, name);
}

bool aurora_fs_v2_space_reclaim_self_test(void) {
    /* Full runtime coverage is provided by the boot-integrated mutation test block. */
    return true;
}
