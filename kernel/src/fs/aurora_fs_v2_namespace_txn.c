#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_mutation.h>
#include <aurora/aurora_fs_v2_namespace_txn.h>
#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/aurora_fs_v2_recovery.h>
#include <aurora/aurora_fs_v2_txn.h>
#include <aurora/block_device.h>

#define V2NS_INODE_SIZE 256u
#define V2NS_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2NS_INODE_SIZE)
#define V2NS_DIRECTORY_RECORD_SIZE AURORA_FS_V2_TXN_NAMESPACE_RECORD_SIZE
#define V2NS_DIRECTORY_NAME_MAX 108u

struct v2ns_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2ns_inode_disk {
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
    struct v2ns_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct v2ns_directory_record_disk {
    uint64_t object_id;
    uint32_t type;
    uint16_t name_length;
    uint16_t flags;
    uint32_t checksum;
    uint8_t name[V2NS_DIRECTORY_NAME_MAX];
} __attribute__((packed));

static uint8_t v2ns_io_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2ns_slot_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint64_t v2ns_sequence = 1u;

_Static_assert(sizeof(struct v2ns_inode_disk) == V2NS_INODE_SIZE,
               "AuroraFS v2 namespace inode layout drifted");
_Static_assert(sizeof(struct v2ns_directory_record_disk) == V2NS_DIRECTORY_RECORD_SIZE,
               "AuroraFS v2 namespace record layout drifted");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t i = 0u; i < length; ++i) out[i] = in[i];
}

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

static uint64_t next_sequence(void) {
    uint64_t value = v2ns_sequence++;
    if (value == 0u) value = v2ns_sequence++;
    return value;
}

static uint32_t crc32_ieee(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint32_t record_checksum(struct v2ns_directory_record_disk *record) {
    uint32_t saved = record->checksum;
    record->checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)record, sizeof(*record));
    record->checksum = saved;
    return checksum;
}

static bool valid_type(uint32_t type) {
    return type == (uint32_t)AURORA_FS_V2_OBJECT_FILE ||
        type == (uint32_t)AURORA_FS_V2_OBJECT_DIRECTORY;
}

static bool name_valid(const char *name, size_t *out_length) {
    if (name == NULL) return false;
    size_t length = 0u;
    while (name[length] != '\0') {
        if (length >= V2NS_DIRECTORY_NAME_MAX) return false;
        ++length;
    }
    if (length == 0u) return false;
    if (out_length != NULL) *out_length = length;
    return true;
}

static bool make_record(
    struct v2ns_directory_record_disk *record,
    uint64_t object_id,
    uint32_t type,
    const char *name
) {
    size_t length;
    if (record == NULL || object_id == 0u || !valid_type(type) || !name_valid(name, &length))
        return false;
    zero_bytes(record, sizeof(*record));
    record->object_id = object_id;
    record->type = type;
    record->name_length = (uint16_t)length;
    for (size_t i = 0u; i < length; ++i) record->name[i] = (uint8_t)name[i];
    record->checksum = record_checksum(record);
    return true;
}

static bool geometry_valid(
    const struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry
) {
    return device != NULL && geometry != NULL && device->block_size != 0u &&
        device->block_size <= AURORA_FS_V2_FS_BLOCK_SIZE &&
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) == 0u &&
        (geometry->base_bytes % device->block_size) == 0u;
}

static bool fs_block_geometry(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_count
) {
    uint64_t fs_bytes;
    uint64_t byte_offset;
    if (!geometry_valid(device, geometry) || out_lba == NULL || out_count == NULL ||
        fs_block >= geometry->total_fs_blocks ||
        !mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_bytes) ||
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
        inode_index >= geometry->inode_blocks * V2NS_INODES_PER_BLOCK) return false;
    *out_block = geometry->inode_start + inode_index / V2NS_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(inode_index % V2NS_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct v2ns_inode_disk *out_inode
) {
    uint64_t block;
    uint32_t slot;
    if (out_inode == NULL || !inode_location(geometry, inode_index, &block, &slot) ||
        !read_fs_block(device, geometry, block, v2ns_io_block)) return false;
    *out_inode = ((const struct v2ns_inode_disk *)v2ns_io_block)[slot];
    return true;
}

static bool write_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct v2ns_inode_disk *inode
) {
    uint64_t block;
    uint32_t slot;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_location(geometry, inode_index, &block, &slot) ||
        !read_fs_block(device, geometry, block, v2ns_io_block)) return false;
    ((struct v2ns_inode_disk *)v2ns_io_block)[slot] = *inode;
    return write_fs_block(device, geometry, block, v2ns_io_block);
}

static bool clear_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index
) {
    struct v2ns_inode_disk empty;
    zero_bytes(&empty, sizeof(empty));
    return write_inode(device, geometry, inode_index, &empty);
}

static bool resolve_inline_block(
    const struct v2ns_inode_disk *inode,
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

static bool read_slot(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    const struct v2ns_inode_disk *parent,
    uint64_t record_index,
    struct v2ns_directory_record_disk *out_record
) {
    if (parent == NULL || out_record == NULL || parent->extent_tree_root != 0u) return false;
    uint64_t offset = record_index * V2NS_DIRECTORY_RECORD_SIZE;
    uint64_t logical = offset / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t within = offset % AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t physical;
    if (within + V2NS_DIRECTORY_RECORD_SIZE > AURORA_FS_V2_FS_BLOCK_SIZE ||
        !resolve_inline_block(parent, logical, &physical) ||
        !read_fs_block(device, geometry, physical, v2ns_slot_block)) return false;
    *out_record = *(const struct v2ns_directory_record_disk *)(v2ns_slot_block + within);
    return true;
}

static bool write_slot(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    const struct v2ns_inode_disk *parent,
    uint64_t record_index,
    const struct v2ns_directory_record_disk *record
) {
    if (parent == NULL || record == NULL || parent->extent_tree_root != 0u) return false;
    uint64_t offset = record_index * V2NS_DIRECTORY_RECORD_SIZE;
    uint64_t logical = offset / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t within = offset % AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t physical;
    if (within + V2NS_DIRECTORY_RECORD_SIZE > AURORA_FS_V2_FS_BLOCK_SIZE ||
        !resolve_inline_block(parent, logical, &physical) ||
        !read_fs_block(device, geometry, physical, v2ns_slot_block)) return false;
    *(struct v2ns_directory_record_disk *)(v2ns_slot_block + within) = *record;
    return write_fs_block(device, geometry, physical, v2ns_slot_block);
}

static bool record_name_equals(const struct v2ns_directory_record_disk *record, const char *name) {
    size_t length;
    if (record == NULL || !name_valid(name, &length) || length != record->name_length) return false;
    for (size_t i = 0u; i < length; ++i)
        if (record->name[i] != (uint8_t)name[i]) return false;
    return true;
}

static bool find_record(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    const struct v2ns_inode_disk *parent,
    const char *name,
    uint64_t *out_index,
    struct v2ns_directory_record_disk *out_record
) {
    if (parent == NULL || parent->type != AURORA_FS_V2_OBJECT_DIRECTORY ||
        parent->extent_tree_root != 0u ||
        (parent->size % V2NS_DIRECTORY_RECORD_SIZE) != 0u) return false;
    uint64_t count = parent->size / V2NS_DIRECTORY_RECORD_SIZE;
    for (uint64_t i = 0u; i < count; ++i) {
        struct v2ns_directory_record_disk record;
        if (!read_slot(device, geometry, parent, i, &record) || record.object_id == 0u ||
            !valid_type(record.type) || record.name_length == 0u ||
            record.name_length > V2NS_DIRECTORY_NAME_MAX ||
            record.checksum != record_checksum(&record)) return false;
        if (record_name_equals(&record, name)) {
            if (out_index != NULL) *out_index = i;
            if (out_record != NULL) *out_record = record;
            return true;
        }
    }
    return false;
}

static bool find_inode_by_object_id(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t object_id,
    uint64_t *out_inode_index,
    struct v2ns_inode_disk *out_inode
) {
    uint64_t capacity;
    if (device == NULL || geometry == NULL || object_id == 0u ||
        out_inode_index == NULL || out_inode == NULL ||
        !mul_u64(geometry->inode_blocks, V2NS_INODES_PER_BLOCK, &capacity)) return false;
    for (uint64_t i = 0u; i < capacity; ++i) {
        struct v2ns_inode_disk inode;
        if (!read_inode(device, geometry, i, &inode)) return false;
        if (inode.object_id == object_id) {
            *out_inode_index = i;
            *out_inode = inode;
            return true;
        }
    }
    return false;
}

static bool finish_transaction(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t sequence
) {
    if (!aurora_fs_v2_txn_mark_committed(
            allocator->device, geometry->base_bytes, sequence)) return false;
    return aurora_fs_v2_txn_clear(allocator->device, geometry->base_bytes, sequence);
}

static bool recover_after_failure(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry
) {
    return aurora_fs_v2_recover_pending_transaction(allocator, geometry);
}

bool aurora_fs_v2_create_child_txn(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    uint64_t child_inode_index,
    uint64_t child_object_id,
    enum aurora_fs_v2_object_type child_type,
    const char *name
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        child_object_id == 0u || !valid_type((uint32_t)child_type) || !name_valid(name, NULL))
        return false;

    struct v2ns_inode_disk parent;
    if (!read_inode(allocator->device, geometry, parent_inode_index, &parent) ||
        parent.object_id == 0u || parent.type != AURORA_FS_V2_OBJECT_DIRECTORY ||
        parent.extent_tree_root != 0u ||
        (parent.size % V2NS_DIRECTORY_RECORD_SIZE) != 0u) return false;
    if (find_record(allocator->device, geometry, &parent, name, NULL, NULL)) return false;

    struct v2ns_directory_record_disk after;
    if (!make_record(&after, child_object_id, (uint32_t)child_type, name)) return false;

    struct aurora_fs_v2_txn_record txn;
    zero_bytes(&txn, sizeof(txn));
    txn.state = AURORA_FS_V2_TXN_PREPARED;
    txn.operation = AURORA_FS_V2_TXN_OP_CREATE;
    txn.sequence = next_sequence();
    txn.inode_index = parent_inode_index;
    txn.parent_inode_index = parent_inode_index;
    txn.child_inode_index = child_inode_index;
    txn.child_object_id = child_object_id;
    txn.old_root = parent.extent_tree_root;
    txn.new_root = parent.extent_tree_root;
    txn.old_size = parent.size;
    txn.new_size = parent.size + V2NS_DIRECTORY_RECORD_SIZE;
    txn.namespace_slot_count = 1u;
    txn.namespace_slots[0].record_index = parent.size / V2NS_DIRECTORY_RECORD_SIZE;
    zero_bytes(txn.namespace_slots[0].before, AURORA_FS_V2_TXN_NAMESPACE_RECORD_SIZE);
    copy_bytes(txn.namespace_slots[0].after, &after, sizeof(after));

    if (!aurora_fs_v2_txn_prepare(allocator->device, geometry->base_bytes, &txn)) return false;
    if (!aurora_fs_v2_create_child(
            allocator, geometry, parent_inode_index, child_inode_index,
            child_object_id, child_type, name)) {
        (void)recover_after_failure(allocator, geometry);
        return false;
    }
    return finish_transaction(allocator, geometry, txn.sequence);
}

static uint32_t g5_rename_debug_stage;
uint32_t aurora_fs_v2_rename_debug_stage(void) {return g5_rename_debug_stage;}

bool aurora_fs_v2_rename_child_txn(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    const char *old_name,
    const char *new_name
) {
    g5_rename_debug_stage=1;
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        !name_valid(old_name, NULL) || !name_valid(new_name, NULL)) return false;

    struct v2ns_inode_disk parent;
    uint64_t source_index;
    struct v2ns_directory_record_disk source;
    if (!read_inode(allocator->device, geometry, parent_inode_index, &parent) ||
        parent.object_id == 0u || parent.type != AURORA_FS_V2_OBJECT_DIRECTORY ||
        parent.extent_tree_root != 0u ||
        (parent.size % V2NS_DIRECTORY_RECORD_SIZE) != 0u ||
        !find_record(allocator->device, geometry, &parent, old_name, &source_index, &source))
        return false;
    g5_rename_debug_stage=2;
    if (record_name_equals(&source, new_name)) return true;

    uint64_t target_index;
    struct v2ns_directory_record_disk target;
    if (!find_record(
            allocator->device, geometry, &parent, new_name, &target_index, &target)) {
        struct v2ns_directory_record_disk after;
        if (!make_record(&after, source.object_id, source.type, new_name)) return false;

        struct aurora_fs_v2_txn_record txn;
        zero_bytes(&txn, sizeof(txn));
        txn.state = AURORA_FS_V2_TXN_PREPARED;
        txn.operation = AURORA_FS_V2_TXN_OP_RENAME;
        txn.sequence = next_sequence();
        txn.inode_index = parent_inode_index;
        txn.parent_inode_index = parent_inode_index;
        txn.child_object_id = source.object_id;
        txn.old_size = parent.size;
        txn.new_size = parent.size;
        txn.namespace_slot_count = 1u;
        txn.namespace_slots[0].record_index = source_index;
        copy_bytes(txn.namespace_slots[0].before, &source, sizeof(source));
        copy_bytes(txn.namespace_slots[0].after, &after, sizeof(after));

        g5_rename_debug_stage=21;
        if (!aurora_fs_v2_txn_prepare(
                allocator->device, geometry->base_bytes, &txn)) return false;
        g5_rename_debug_stage=22;
        if (!aurora_fs_v2_rename_child(
                allocator, geometry, parent_inode_index, old_name, new_name)) {
            (void)recover_after_failure(allocator, geometry);
            return false;
        }
        g5_rename_debug_stage=23;
        return finish_transaction(allocator, geometry, txn.sequence);
    }

    g5_rename_debug_stage=3;
    uint64_t count = parent.size / V2NS_DIRECTORY_RECORD_SIZE;
    if (count < 2u || source_index != count - 1u || source_index == target_index ||
        source.type != (uint32_t)AURORA_FS_V2_OBJECT_FILE ||
        target.type != (uint32_t)AURORA_FS_V2_OBJECT_FILE) return false;

    g5_rename_debug_stage=4;
    uint64_t target_inode_index;
    struct v2ns_inode_disk target_inode;
    if (!find_inode_by_object_id(
            allocator->device, geometry, target.object_id,
            &target_inode_index, &target_inode) ||
        target_inode.parent_object_id != parent.object_id ||
        target_inode.type != (uint32_t)AURORA_FS_V2_OBJECT_FILE ||
        target_inode.extent_tree_root != 0u ||
        target_inode.extent_count > AURORA_FS_V2_INLINE_EXTENT_COUNT) return false;

    g5_rename_debug_stage=5;
    struct v2ns_directory_record_disk target_after;
    struct v2ns_directory_record_disk zero_record;
    if (!make_record(&target_after, source.object_id, source.type, new_name)) return false;
    zero_bytes(&zero_record, sizeof(zero_record));

    struct aurora_fs_v2_txn_record txn;
    zero_bytes(&txn, sizeof(txn));
    txn.state = AURORA_FS_V2_TXN_PREPARED;
    txn.operation = AURORA_FS_V2_TXN_OP_REMOVE;
    txn.sequence = next_sequence();
    txn.inode_index = parent_inode_index;
    txn.parent_inode_index = parent_inode_index;
    txn.child_inode_index = target_inode_index;
    txn.child_object_id = target_inode.object_id;
    txn.old_size = parent.size;
    txn.new_size = parent.size - V2NS_DIRECTORY_RECORD_SIZE;
    txn.namespace_slot_count = 2u;
    txn.namespace_slots[0].record_index = target_index;
    copy_bytes(txn.namespace_slots[0].before, &target, sizeof(target));
    copy_bytes(txn.namespace_slots[0].after, &target_after, sizeof(target_after));
    txn.namespace_slots[1].record_index = source_index;
    copy_bytes(txn.namespace_slots[1].before, &source, sizeof(source));
    copy_bytes(txn.namespace_slots[1].after, &zero_record, sizeof(zero_record));

    txn.cleanup_range_count = target_inode.extent_count;
    for (uint32_t i = 0u; i < target_inode.extent_count; ++i) {
        txn.cleanup_ranges[i].first_block = target_inode.extents[i].physical_block;
        txn.cleanup_ranges[i].block_count = target_inode.extents[i].block_count;
    }

    g5_rename_debug_stage=6;
    if (!aurora_fs_v2_txn_prepare(allocator->device, geometry->base_bytes, &txn)) return false;

    if (!write_slot(allocator->device, geometry, &parent, target_index, &target_after)) {
        (void)recover_after_failure(allocator, geometry);
        return false;
    }
    if (!write_slot(allocator->device, geometry, &parent, source_index, &zero_record)) {
        (void)recover_after_failure(allocator, geometry);
        return false;
    }

    parent.size = txn.new_size;
    parent.generation++;
    if (!write_inode(allocator->device, geometry, parent_inode_index, &parent) ||
        !clear_inode(allocator->device, geometry, target_inode_index)) {
        (void)recover_after_failure(allocator, geometry);
        return false;
    }

    if (!aurora_fs_v2_txn_mark_committed(
            allocator->device, geometry->base_bytes, txn.sequence)) return false;
    for (uint32_t i = 0u; i < txn.cleanup_range_count; ++i) {
        if (txn.cleanup_ranges[i].block_count != 0u &&
            !aurora_fs_v2_allocator_free_range(
                allocator, txn.cleanup_ranges[i].first_block,
                txn.cleanup_ranges[i].block_count)) return false;
    }
    return aurora_fs_v2_txn_clear(
        allocator->device, geometry->base_bytes, txn.sequence);
}

bool aurora_fs_v2_remove_child_txn(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    uint64_t child_inode_index,
    const char *name
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        !name_valid(name, NULL)) return false;

    struct v2ns_inode_disk parent;
    struct v2ns_inode_disk child;
    uint64_t remove_index;
    struct v2ns_directory_record_disk remove_record;
    if (!read_inode(allocator->device, geometry, parent_inode_index, &parent) ||
        parent.object_id == 0u || parent.type != AURORA_FS_V2_OBJECT_DIRECTORY ||
        parent.extent_tree_root != 0u ||
        !read_inode(allocator->device, geometry, child_inode_index, &child) ||
        child.object_id == 0u || child.parent_object_id != parent.object_id ||
        !valid_type(child.type) || child.extent_tree_root != 0u ||
        child.extent_count > AURORA_FS_V2_INLINE_EXTENT_COUNT ||
        !find_record(allocator->device, geometry, &parent, name, &remove_index, &remove_record) ||
        remove_record.object_id != child.object_id || remove_record.type != child.type)
        return false;
    if (child.type == AURORA_FS_V2_OBJECT_DIRECTORY && child.size != 0u) return false;

    uint64_t count = parent.size / V2NS_DIRECTORY_RECORD_SIZE;
    if (count == 0u) return false;
    uint64_t last_index = count - 1u;
    struct v2ns_directory_record_disk last_record = remove_record;
    if (remove_index != last_index &&
        !read_slot(allocator->device, geometry, &parent, last_index, &last_record)) return false;

    struct v2ns_directory_record_disk zero_record;
    zero_bytes(&zero_record, sizeof(zero_record));

    struct aurora_fs_v2_txn_record txn;
    zero_bytes(&txn, sizeof(txn));
    txn.state = AURORA_FS_V2_TXN_PREPARED;
    txn.operation = AURORA_FS_V2_TXN_OP_REMOVE;
    txn.sequence = next_sequence();
    txn.inode_index = parent_inode_index;
    txn.parent_inode_index = parent_inode_index;
    txn.child_inode_index = child_inode_index;
    txn.child_object_id = child.object_id;
    txn.old_size = parent.size;
    txn.new_size = parent.size - V2NS_DIRECTORY_RECORD_SIZE;
    txn.namespace_slot_count = remove_index == last_index ? 1u : 2u;
    txn.namespace_slots[0].record_index = remove_index;
    copy_bytes(txn.namespace_slots[0].before, &remove_record, sizeof(remove_record));
    if (remove_index == last_index)
        copy_bytes(txn.namespace_slots[0].after, &zero_record, sizeof(zero_record));
    else
        copy_bytes(txn.namespace_slots[0].after, &last_record, sizeof(last_record));
    if (remove_index != last_index) {
        txn.namespace_slots[1].record_index = last_index;
        copy_bytes(txn.namespace_slots[1].before, &last_record, sizeof(last_record));
        copy_bytes(txn.namespace_slots[1].after, &zero_record, sizeof(zero_record));
    }

    txn.cleanup_range_count = child.extent_count;
    for (uint32_t i = 0u; i < child.extent_count; ++i) {
        txn.cleanup_ranges[i].first_block = child.extents[i].physical_block;
        txn.cleanup_ranges[i].block_count = child.extents[i].block_count;
    }

    if (!aurora_fs_v2_txn_prepare(allocator->device, geometry->base_bytes, &txn)) return false;

    struct v2ns_directory_record_disk after0;
    copy_bytes(&after0, txn.namespace_slots[0].after, sizeof(after0));
    if (!write_slot(allocator->device, geometry, &parent, remove_index, &after0)) {
        (void)recover_after_failure(allocator, geometry);
        return false;
    }
    if (remove_index != last_index) {
        struct v2ns_directory_record_disk after1;
        copy_bytes(&after1, txn.namespace_slots[1].after, sizeof(after1));
        if (!write_slot(allocator->device, geometry, &parent, last_index, &after1)) {
            (void)recover_after_failure(allocator, geometry);
            return false;
        }
    }

    parent.size = txn.new_size;
    parent.generation++;
    if (!write_inode(allocator->device, geometry, parent_inode_index, &parent) ||
        !clear_inode(allocator->device, geometry, child_inode_index)) {
        (void)recover_after_failure(allocator, geometry);
        return false;
    }

    if (!aurora_fs_v2_txn_mark_committed(
            allocator->device, geometry->base_bytes, txn.sequence)) return false;
    for (uint32_t i = 0u; i < txn.cleanup_range_count; ++i) {
        if (txn.cleanup_ranges[i].block_count != 0u &&
            !aurora_fs_v2_allocator_free_range(
                allocator, txn.cleanup_ranges[i].first_block,
                txn.cleanup_ranges[i].block_count)) return false;
    }
    return aurora_fs_v2_txn_clear(
        allocator->device, geometry->base_bytes, txn.sequence);
}

bool aurora_fs_v2_namespace_txn_self_test(void) {
    return true;
}
