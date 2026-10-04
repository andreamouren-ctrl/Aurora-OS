#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/aurora_fs_v2_recovery.h>
#include <aurora/aurora_fs_v2_txn.h>
#include <aurora/block_device.h>

#define V2REC_INODE_SIZE 256u
#define V2REC_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2REC_INODE_SIZE)
#define V2REC_DIRECTORY_RECORD_SIZE AURORA_FS_V2_TXN_NAMESPACE_RECORD_SIZE

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
static uint8_t v2rec_slot_block[AURORA_FS_V2_FS_BLOCK_SIZE];

_Static_assert(sizeof(struct v2rec_inode_disk) == V2REC_INODE_SIZE,
               "AuroraFS v2 recovery inode layout drifted");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static bool bytes_equal(const void *a, const void *b, size_t length) {
    const uint8_t *left = a;
    const uint8_t *right = b;
    for (size_t i = 0u; i < length; ++i)
        if (left[i] != right[i]) return false;
    return true;
}

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

static bool read_fs_block(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    void *buffer
) {
    uint64_t lba;
    uint32_t count;
    return buffer != NULL && fs_block_geometry(device, geometry, fs_block, &lba, &count) &&
        block_device_read(device, lba, count, buffer);
}

static bool write_fs_block(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    const void *buffer
) {
    uint64_t lba;
    uint32_t count;
    return device != NULL && !device->read_only && buffer != NULL &&
        fs_block_geometry(device, geometry, fs_block, &lba, &count) &&
        block_device_write(device, lba, count, buffer) && block_device_flush(device);
}

static bool inode_location(
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t *out_block,
    uint32_t *out_slot
) {
    if (geometry == NULL || out_block == NULL || out_slot == NULL ||
        inode_index >= geometry->inode_blocks * V2REC_INODES_PER_BLOCK) return false;
    *out_block = geometry->inode_start + inode_index / V2REC_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(inode_index % V2REC_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct v2rec_inode_disk *out_inode
) {
    uint64_t block;
    uint32_t slot;
    if (out_inode == NULL || !inode_location(geometry, inode_index, &block, &slot) ||
        !read_fs_block(device, geometry, block, v2rec_inode_block)) return false;
    *out_inode = ((const struct v2rec_inode_disk *)v2rec_inode_block)[slot];
    return true;
}

static bool write_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct v2rec_inode_disk *inode
) {
    uint64_t block;
    uint32_t slot;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_location(geometry, inode_index, &block, &slot) ||
        !read_fs_block(device, geometry, block, v2rec_inode_block)) return false;
    ((struct v2rec_inode_disk *)v2rec_inode_block)[slot] = *inode;
    return write_fs_block(device, geometry, block, v2rec_inode_block);
}

static bool clear_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index
) {
    struct v2rec_inode_disk empty;
    zero_bytes(&empty, sizeof(empty));
    return write_inode(device, geometry, inode_index, &empty);
}

static bool resolve_inline_block(
    const struct v2rec_inode_disk *inode,
    uint64_t logical,
    uint64_t *out_physical
) {
    if (inode == NULL || out_physical == NULL || inode->extent_tree_root != 0u ||
        inode->extent_count > AURORA_FS_V2_INLINE_EXTENT_COUNT) return false;
    for (uint32_t i = 0u; i < inode->extent_count; ++i) {
        uint64_t end;
        if (inode->extents[i].block_count == 0u ||
            !add_u64(inode->extents[i].logical_block, inode->extents[i].block_count, &end))
            return false;
        if (logical >= inode->extents[i].logical_block && logical < end) {
            *out_physical = inode->extents[i].physical_block +
                (logical - inode->extents[i].logical_block);
            return true;
        }
    }
    return false;
}

static bool read_namespace_slot(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    const struct v2rec_inode_disk *parent,
    uint64_t record_index,
    uint8_t out_record[V2REC_DIRECTORY_RECORD_SIZE]
) {
    if (parent == NULL || out_record == NULL || parent->extent_tree_root != 0u) return false;
    uint64_t offset = record_index * V2REC_DIRECTORY_RECORD_SIZE;
    uint64_t logical = offset / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t within = offset % AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t physical;
    if (within + V2REC_DIRECTORY_RECORD_SIZE > AURORA_FS_V2_FS_BLOCK_SIZE ||
        !resolve_inline_block(parent, logical, &physical) ||
        !read_fs_block(device, geometry, physical, v2rec_slot_block)) return false;
    for (size_t i = 0u; i < V2REC_DIRECTORY_RECORD_SIZE; ++i)
        out_record[i] = v2rec_slot_block[within + i];
    return true;
}

static bool write_namespace_slot(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    const struct v2rec_inode_disk *parent,
    uint64_t record_index,
    const uint8_t record[V2REC_DIRECTORY_RECORD_SIZE]
) {
    if (parent == NULL || record == NULL || parent->extent_tree_root != 0u) return false;
    uint64_t offset = record_index * V2REC_DIRECTORY_RECORD_SIZE;
    uint64_t logical = offset / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t within = offset % AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t physical;
    if (within + V2REC_DIRECTORY_RECORD_SIZE > AURORA_FS_V2_FS_BLOCK_SIZE ||
        !resolve_inline_block(parent, logical, &physical) ||
        !read_fs_block(device, geometry, physical, v2rec_slot_block)) return false;
    for (size_t i = 0u; i < V2REC_DIRECTORY_RECORD_SIZE; ++i)
        v2rec_slot_block[within + i] = record[i];
    return write_fs_block(device, geometry, physical, v2rec_slot_block);
}

static bool free_ranges(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_txn_range *ranges,
    uint32_t range_count
) {
    if (allocator == NULL || ranges == NULL) return range_count == 0u;
    for (uint32_t i = 0u; i < range_count; ++i) {
        for (uint64_t offset = 0u; offset < ranges[i].block_count; ++offset) {
            uint64_t block = ranges[i].first_block + offset;
            bool allocated = false;
            if (!aurora_fs_v2_allocator_is_allocated(allocator, block, &allocated)) return false;
            if (allocated && !aurora_fs_v2_allocator_free_range(allocator, block, 1u)) return false;
        }
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

static bool namespace_operation(enum aurora_fs_v2_txn_operation operation) {
    return operation == AURORA_FS_V2_TXN_OP_CREATE ||
        operation == AURORA_FS_V2_TXN_OP_REMOVE ||
        operation == AURORA_FS_V2_TXN_OP_RENAME;
}

static bool namespace_slots_match(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    const struct v2rec_inode_disk *parent,
    const struct aurora_fs_v2_txn_record *txn,
    bool after
) {
    uint8_t current[V2REC_DIRECTORY_RECORD_SIZE];
    for (uint32_t i = 0u; i < txn->namespace_slot_count; ++i) {
        uint64_t byte_offset = txn->namespace_slots[i].record_index * V2REC_DIRECTORY_RECORD_SIZE;
        const uint8_t *expected = after ? txn->namespace_slots[i].after : txn->namespace_slots[i].before;
        uint64_t authoritative_size = after ? txn->new_size : txn->old_size;
        if (byte_offset >= authoritative_size) {
            bool all_zero = true;
            for (size_t b = 0u; b < V2REC_DIRECTORY_RECORD_SIZE; ++b)
                if (expected[b] != 0u) all_zero = false;
            if (all_zero) continue;
        }
        if (!read_namespace_slot(device, geometry, parent,
                                 txn->namespace_slots[i].record_index, current) ||
            !bytes_equal(current, expected, sizeof(current))) return false;
    }
    return true;
}

static bool apply_namespace_images(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    struct v2rec_inode_disk *parent,
    const struct aurora_fs_v2_txn_record *txn,
    bool after
) {
    uint64_t target_size = after ? txn->new_size : txn->old_size;
    for (uint32_t i = 0u; i < txn->namespace_slot_count; ++i) {
        uint64_t byte_offset = txn->namespace_slots[i].record_index * V2REC_DIRECTORY_RECORD_SIZE;
        const uint8_t *image = after ? txn->namespace_slots[i].after : txn->namespace_slots[i].before;
        if (byte_offset >= target_size) {
            bool all_zero = true;
            for (size_t b = 0u; b < V2REC_DIRECTORY_RECORD_SIZE; ++b)
                if (image[b] != 0u) all_zero = false;
            if (all_zero) continue;
        }
        if (!write_namespace_slot(device, geometry, parent,
                                  txn->namespace_slots[i].record_index, image)) return false;
    }
    parent->size = target_size;
    parent->generation++;
    return write_inode(device, geometry, txn->parent_inode_index, parent);
}

static bool finish_namespace_commit(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    struct v2rec_inode_disk *parent,
    const struct aurora_fs_v2_txn_record *txn,
    bool already_committed
) {
    if (!apply_namespace_images(allocator->device, geometry, parent, txn, true)) return false;
    if (!already_committed && !aurora_fs_v2_txn_mark_committed(
            allocator->device, geometry->base_bytes, txn->sequence)) return false;
    if (txn->operation == AURORA_FS_V2_TXN_OP_REMOVE &&
        !clear_inode(allocator->device, geometry, txn->child_inode_index)) return false;
    if (!free_ranges(allocator, txn->cleanup_ranges, txn->cleanup_range_count)) return false;
    return aurora_fs_v2_txn_clear(
        allocator->device, geometry->base_bytes, txn->sequence);
}

static bool recover_namespace_transaction(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    const struct aurora_fs_v2_txn_record *txn
) {
    struct v2rec_inode_disk parent;
    if (!read_inode(allocator->device, geometry, txn->parent_inode_index, &parent) ||
        parent.object_id == 0u || parent.type != AURORA_FS_V2_OBJECT_DIRECTORY ||
        parent.extent_tree_root != 0u) return false;

    if (txn->state == AURORA_FS_V2_TXN_COMMITTED)
        return finish_namespace_commit(allocator, geometry, &parent, txn, true);

    bool after_authoritative = parent.size == txn->new_size &&
        namespace_slots_match(allocator->device, geometry, &parent, txn, true);
    if (after_authoritative)
        return finish_namespace_commit(allocator, geometry, &parent, txn, false);

    if (!apply_namespace_images(allocator->device, geometry, &parent, txn, false)) return false;
    if (txn->operation == AURORA_FS_V2_TXN_OP_CREATE) {
        struct v2rec_inode_disk child;
        if (read_inode(allocator->device, geometry, txn->child_inode_index, &child) &&
            child.object_id == txn->child_object_id &&
            !clear_inode(allocator->device, geometry, txn->child_inode_index)) return false;
    }
    if (!free_ranges(allocator, txn->rollback_ranges, txn->rollback_range_count)) return false;
    return aurora_fs_v2_txn_clear(
        allocator->device, geometry->base_bytes, txn->sequence);
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

    if (namespace_operation(txn.operation))
        return recover_namespace_transaction(allocator, geometry, &txn);

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
