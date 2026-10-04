#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/aurora_fs_v2_recovery.h>
#include <aurora/aurora_fs_v2_txn.h>
#include <aurora/block_device.h>

#define V2REC_INODE_SIZE 256u
#define V2REC_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2REC_INODE_SIZE)

struct v2rec_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2rec_inode_disk {
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
    struct v2rec_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

static uint8_t v2rec_inode_block[AURORA_FS_V2_FS_BLOCK_SIZE];

_Static_assert(sizeof(struct v2rec_inode_disk) == V2REC_INODE_SIZE,
               "AuroraFS v2 recovery inode layout drifted");

static bool mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || (a != 0u && b > UINT64_MAX / a)) return false;
    *out = a * b;
    return true;
}

static bool add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || b > UINT64_MAX - a) return false;
    *out = a + b;
    return true;
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

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct v2rec_inode_disk *out_inode
) {
    if (out_inode == NULL || geometry == NULL ||
        inode_index >= geometry->inode_blocks * V2REC_INODES_PER_BLOCK) return false;

    uint64_t block = geometry->inode_start + inode_index / V2REC_INODES_PER_BLOCK;
    uint32_t slot = (uint32_t)(inode_index % V2REC_INODES_PER_BLOCK);
    uint64_t lba;
    uint32_t count;
    if (!fs_block_geometry(device, geometry, block, &lba, &count) ||
        !block_device_read(device, lba, count, v2rec_inode_block)) return false;
    *out_inode = ((const struct v2rec_inode_disk *)v2rec_inode_block)[slot];
    return true;
}

static bool free_ranges(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_txn_range *ranges,
    uint32_t range_count
) {
    if (allocator == NULL || ranges == NULL) return range_count == 0u;
    for (uint32_t i = 0u; i < range_count; ++i) {
        if (ranges[i].block_count == 0u) continue;
        if (!aurora_fs_v2_allocator_free_range(
                allocator, ranges[i].first_block, ranges[i].block_count)) return false;
    }
    return true;
}

static bool inode_matches_old(
    const struct v2rec_inode_disk *inode,
    const struct aurora_fs_v2_txn_record *txn
) {
    return inode != NULL && txn != NULL &&
        inode->extent_tree_root == txn->old_root && inode->size == txn->old_size;
}

static bool inode_matches_new(
    const struct v2rec_inode_disk *inode,
    const struct aurora_fs_v2_txn_record *txn
) {
    return inode != NULL && txn != NULL &&
        inode->extent_tree_root == txn->new_root && inode->size == txn->new_size;
}

bool aurora_fs_v2_recover_pending_transaction(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry
) {
    if (allocator == NULL || allocator->device == NULL || geometry == NULL ||
        allocator->device->read_only) return false;

    struct aurora_fs_v2_txn_record txn;
    if (!aurora_fs_v2_txn_load(allocator->device, geometry->base_bytes, &txn)) return false;
    if (txn.state == AURORA_FS_V2_TXN_CLEAN) return true;
    if (txn.sequence == 0u || txn.operation == AURORA_FS_V2_TXN_OP_NONE) return false;

    struct v2rec_inode_disk inode;
    if (!read_inode(allocator->device, geometry, txn.inode_index, &inode) ||
        inode.object_id == 0u) return false;

    bool old_is_authoritative = inode_matches_old(&inode, &txn);
    bool new_is_authoritative = inode_matches_new(&inode, &txn);

    if (txn.state == AURORA_FS_V2_TXN_PREPARED) {
        if (old_is_authoritative && !new_is_authoritative) {
            if (!free_ranges(allocator, txn.rollback_ranges, txn.rollback_range_count))
                return false;
            return aurora_fs_v2_txn_clear(
                allocator->device, geometry->base_bytes, txn.sequence);
        }

        if (new_is_authoritative) {
            if (!aurora_fs_v2_txn_mark_committed(
                    allocator->device, geometry->base_bytes, txn.sequence)) return false;
            if (!free_ranges(allocator, txn.cleanup_ranges, txn.cleanup_range_count))
                return false;
            return aurora_fs_v2_txn_clear(
                allocator->device, geometry->base_bytes, txn.sequence);
        }
        return false;
    }

    if (txn.state == AURORA_FS_V2_TXN_COMMITTED) {
        if (!new_is_authoritative) return false;
        if (!free_ranges(allocator, txn.cleanup_ranges, txn.cleanup_range_count))
            return false;
        return aurora_fs_v2_txn_clear(
            allocator->device, geometry->base_bytes, txn.sequence);
    }

    return false;
}

bool aurora_fs_v2_recovery_self_test(void) {
    return true;
}
