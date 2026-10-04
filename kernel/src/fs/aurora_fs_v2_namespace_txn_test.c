#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_file_io.h>
#include <aurora/aurora_fs_v2_mutation.h>
#include <aurora/aurora_fs_v2_namespace_txn.h>
#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/aurora_fs_v2_recovery.h>
#include <aurora/aurora_fs_v2_txn.h>
#include <aurora/block_device.h>

#define V2NT_STORAGE_BYTES (2u * 1024u * 1024u)
#define V2NT_INODE_SIZE 256u
#define V2NT_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2NT_INODE_SIZE)
#define V2NT_RECORD_SIZE AURORA_FS_V2_TXN_NAMESPACE_RECORD_SIZE
#define V2NT_NAME_MAX 108u

struct v2nt_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2nt_inode_disk {
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
    struct v2nt_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct v2nt_record_disk {
    uint64_t object_id;
    uint32_t type;
    uint16_t name_length;
    uint16_t flags;
    uint32_t checksum;
    uint8_t name[V2NT_NAME_MAX];
} __attribute__((packed));

struct v2nt_context {
    uint8_t storage[V2NT_STORAGE_BYTES];
};

static struct v2nt_context v2nt_context;
static uint8_t v2nt_inode_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2nt_slot_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2nt_payload[6000u];

_Static_assert(sizeof(struct v2nt_inode_disk) == V2NT_INODE_SIZE,
               "AuroraFS v2 namespace test inode layout drifted");
_Static_assert(sizeof(struct v2nt_record_disk) == V2NT_RECORD_SIZE,
               "AuroraFS v2 namespace test record layout drifted");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t i = 0u; i < length; ++i) out[i] = in[i];
}

static bool bytes_equal(const void *a, const void *b, size_t length) {
    const uint8_t *left = a;
    const uint8_t *right = b;
    for (size_t i = 0u; i < length; ++i)
        if (left[i] != right[i]) return false;
    return true;
}

static bool test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba)
        return false;
    struct v2nt_context *context = device->context;
    uint64_t offset = lba * device->block_size;
    uint64_t length = (uint64_t)block_count * device->block_size;
    if (context == NULL || offset > V2NT_STORAGE_BYTES || length > V2NT_STORAGE_BYTES - offset)
        return false;
    uint8_t *out = buffer;
    for (uint64_t i = 0u; i < length; ++i) out[i] = context->storage[offset + i];
    return true;
}

static bool test_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    if (device == NULL || device->read_only || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba)
        return false;
    struct v2nt_context *context = device->context;
    uint64_t offset = lba * device->block_size;
    uint64_t length = (uint64_t)block_count * device->block_size;
    if (context == NULL || offset > V2NT_STORAGE_BYTES || length > V2NT_STORAGE_BYTES - offset)
        return false;
    const uint8_t *in = buffer;
    for (uint64_t i = 0u; i < length; ++i) context->storage[offset + i] = in[i];
    return true;
}

static bool test_flush(struct aurora_block_device *device) {
    return device != NULL;
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

static uint32_t record_checksum(struct v2nt_record_disk *record) {
    uint32_t saved = record->checksum;
    record->checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)record, sizeof(*record));
    record->checksum = saved;
    return checksum;
}

static bool make_record(
    struct v2nt_record_disk *record,
    uint64_t object_id,
    enum aurora_fs_v2_object_type type,
    const char *name
) {
    if (record == NULL || object_id == 0u || name == NULL) return false;
    size_t length = 0u;
    while (name[length] != '\0') {
        if (length >= V2NT_NAME_MAX) return false;
        ++length;
    }
    if (length == 0u) return false;
    zero_bytes(record, sizeof(*record));
    record->object_id = object_id;
    record->type = (uint32_t)type;
    record->name_length = (uint16_t)length;
    for (size_t i = 0u; i < length; ++i) record->name[i] = (uint8_t)name[i];
    record->checksum = record_checksum(record);
    return true;
}

static bool init_case(
    uint32_t block_size,
    struct aurora_block_device *device,
    struct aurora_fs_v2_format_geometry *geometry,
    struct aurora_fs_v2_allocator *allocator
) {
    if (block_size == 0u || (V2NT_STORAGE_BYTES % block_size) != 0u) return false;
    zero_bytes(&v2nt_context, sizeof(v2nt_context));
    *device = (struct aurora_block_device){
        .name = "aurorafs-v2-namespace-txn-test",
        .block_size = block_size,
        .block_count = V2NT_STORAGE_BYTES / block_size,
        .read_only = false,
        .context = &v2nt_context,
        .read_blocks = test_read,
        .write_blocks = test_write,
        .flush = test_flush
    };
    if (!aurora_fs_v2_format_device(
            device, AURORA_FS_V2_DEFAULT_BASE_BYTES, 0u, geometry) ||
        !aurora_fs_v2_allocator_init(
            allocator, device, geometry->base_bytes, geometry->total_fs_blocks,
            geometry->bitmap_start, geometry->bitmap_blocks, geometry->data_start) ||
        !aurora_fs_v2_object_init(
            device, geometry, 0u, 1u, 1u, AURORA_FS_V2_OBJECT_DIRECTORY)) return false;
    return true;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct v2nt_inode_disk *out_inode
) {
    if (out_inode == NULL || inode_index >= geometry->inode_blocks * V2NT_INODES_PER_BLOCK)
        return false;
    uint64_t fs_block = geometry->inode_start + inode_index / V2NT_INODES_PER_BLOCK;
    uint64_t byte_offset = geometry->base_bytes + fs_block * AURORA_FS_V2_FS_BLOCK_SIZE;
    if ((byte_offset % device->block_size) != 0u) return false;
    uint64_t lba = byte_offset / device->block_size;
    uint32_t count = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    if (!block_device_read(device, lba, count, v2nt_inode_block)) return false;
    *out_inode = ((const struct v2nt_inode_disk *)v2nt_inode_block)
        [inode_index % V2NT_INODES_PER_BLOCK];
    return true;
}

static bool read_first_record(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    const struct v2nt_inode_disk *parent,
    struct v2nt_record_disk *out_record
) {
    if (parent == NULL || out_record == NULL || parent->extent_tree_root != 0u ||
        parent->extent_count == 0u || parent->extents[0].block_count == 0u) return false;
    uint64_t physical = parent->extents[0].physical_block;
    uint64_t byte_offset = geometry->base_bytes + physical * AURORA_FS_V2_FS_BLOCK_SIZE;
    if ((byte_offset % device->block_size) != 0u) return false;
    uint64_t lba = byte_offset / device->block_size;
    uint32_t count = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    if (!block_device_read(device, lba, count, v2nt_slot_block)) return false;
    *out_record = *(const struct v2nt_record_disk *)v2nt_slot_block;
    return true;
}

static bool txn_is_clean(struct aurora_block_device *device, uint64_t base_bytes) {
    struct aurora_fs_v2_txn_record txn;
    return aurora_fs_v2_txn_load(device, base_bytes, &txn) &&
        txn.state == AURORA_FS_V2_TXN_CLEAN;
}

static bool lookup_state(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    const char *name,
    bool expected
) {
    struct aurora_fs_v2_directory_entry entry;
    bool found = aurora_fs_v2_directory_lookup_entry(allocator, geometry, 0u, name, &entry);
    return found == expected;
}

static bool run_create_before_publish(uint32_t block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!init_case(block_size, &device, &geometry, &allocator)) return false;

    struct v2nt_record_disk after;
    if (!make_record(&after, 2u, AURORA_FS_V2_OBJECT_FILE, "created")) return false;
    struct aurora_fs_v2_txn_record txn = {0};
    txn.state = AURORA_FS_V2_TXN_PREPARED;
    txn.operation = AURORA_FS_V2_TXN_OP_CREATE;
    txn.sequence = 101u;
    txn.inode_index = 0u;
    txn.parent_inode_index = 0u;
    txn.child_inode_index = 1u;
    txn.child_object_id = 2u;
    txn.old_size = 0u;
    txn.new_size = V2NT_RECORD_SIZE;
    txn.namespace_slot_count = 1u;
    txn.namespace_slots[0].record_index = 0u;
    copy_bytes(txn.namespace_slots[0].after, &after, sizeof(after));
    if (!aurora_fs_v2_txn_prepare(&device, geometry.base_bytes, &txn)) return false;

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start) ||
        !aurora_fs_v2_recover_pending_transaction(&reopened, &geometry)) return false;

    struct v2nt_inode_disk child;
    return txn_is_clean(&device, geometry.base_bytes) &&
        lookup_state(&reopened, &geometry, "created", false) &&
        read_inode(&device, &geometry, 1u, &child) && child.object_id == 0u;
}

static bool run_create_after_publish(uint32_t block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!init_case(block_size, &device, &geometry, &allocator)) return false;

    struct v2nt_record_disk after;
    if (!make_record(&after, 2u, AURORA_FS_V2_OBJECT_FILE, "created")) return false;
    struct aurora_fs_v2_txn_record txn = {0};
    txn.state = AURORA_FS_V2_TXN_PREPARED;
    txn.operation = AURORA_FS_V2_TXN_OP_CREATE;
    txn.sequence = 102u;
    txn.inode_index = 0u;
    txn.parent_inode_index = 0u;
    txn.child_inode_index = 1u;
    txn.child_object_id = 2u;
    txn.old_size = 0u;
    txn.new_size = V2NT_RECORD_SIZE;
    txn.namespace_slot_count = 1u;
    txn.namespace_slots[0].record_index = 0u;
    copy_bytes(txn.namespace_slots[0].after, &after, sizeof(after));
    if (!aurora_fs_v2_txn_prepare(&device, geometry.base_bytes, &txn) ||
        !aurora_fs_v2_create_child(
            &allocator, &geometry, 0u, 1u, 2u, AURORA_FS_V2_OBJECT_FILE, "created"))
        return false;

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start) ||
        !aurora_fs_v2_recover_pending_transaction(&reopened, &geometry)) return false;
    return txn_is_clean(&device, geometry.base_bytes) &&
        lookup_state(&reopened, &geometry, "created", true);
}

static bool prepare_rename(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    struct aurora_fs_v2_txn_record *txn
) {
    struct v2nt_inode_disk parent;
    struct v2nt_record_disk before;
    struct v2nt_record_disk after;
    if (!read_inode(device, geometry, 0u, &parent) ||
        !read_first_record(device, geometry, &parent, &before) ||
        !make_record(&after, before.object_id,
                     (enum aurora_fs_v2_object_type)before.type, "beta")) return false;
    zero_bytes(txn, sizeof(*txn));
    txn->state = AURORA_FS_V2_TXN_PREPARED;
    txn->operation = AURORA_FS_V2_TXN_OP_RENAME;
    txn->sequence = 103u;
    txn->inode_index = 0u;
    txn->parent_inode_index = 0u;
    txn->child_object_id = before.object_id;
    txn->old_size = parent.size;
    txn->new_size = parent.size;
    txn->namespace_slot_count = 1u;
    txn->namespace_slots[0].record_index = 0u;
    copy_bytes(txn->namespace_slots[0].before, &before, sizeof(before));
    copy_bytes(txn->namespace_slots[0].after, &after, sizeof(after));
    return true;
}

static bool run_rename_before_publish(uint32_t block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!init_case(block_size, &device, &geometry, &allocator) ||
        !aurora_fs_v2_create_child(
            &allocator, &geometry, 0u, 1u, 2u, AURORA_FS_V2_OBJECT_FILE, "alpha"))
        return false;
    struct aurora_fs_v2_txn_record txn;
    if (!prepare_rename(&device, &geometry, &txn) ||
        !aurora_fs_v2_txn_prepare(&device, geometry.base_bytes, &txn) ||
        !aurora_fs_v2_recover_pending_transaction(&allocator, &geometry)) return false;
    return txn_is_clean(&device, geometry.base_bytes) &&
        lookup_state(&allocator, &geometry, "alpha", true) &&
        lookup_state(&allocator, &geometry, "beta", false);
}

static bool run_rename_after_publish(uint32_t block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!init_case(block_size, &device, &geometry, &allocator) ||
        !aurora_fs_v2_create_child(
            &allocator, &geometry, 0u, 1u, 2u, AURORA_FS_V2_OBJECT_FILE, "alpha"))
        return false;
    struct aurora_fs_v2_txn_record txn;
    if (!prepare_rename(&device, &geometry, &txn) ||
        !aurora_fs_v2_txn_prepare(&device, geometry.base_bytes, &txn) ||
        !aurora_fs_v2_rename_child(&allocator, &geometry, 0u, "alpha", "beta") ||
        !aurora_fs_v2_recover_pending_transaction(&allocator, &geometry)) return false;
    return txn_is_clean(&device, geometry.base_bytes) &&
        lookup_state(&allocator, &geometry, "alpha", false) &&
        lookup_state(&allocator, &geometry, "beta", true);
}

static bool run_remove_after_publish(uint32_t block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!init_case(block_size, &device, &geometry, &allocator) ||
        !aurora_fs_v2_create_child(
            &allocator, &geometry, 0u, 1u, 2u, AURORA_FS_V2_OBJECT_FILE, "alpha"))
        return false;

    for (size_t i = 0u; i < sizeof(v2nt_payload); ++i)
        v2nt_payload[i] = (uint8_t)((i * 23u + 11u) & 0xFFu);
    size_t written = 0u;
    if (!aurora_fs_v2_file_write(
            &allocator, &geometry, 1u, 0u, v2nt_payload,
            sizeof(v2nt_payload), &written) || written != sizeof(v2nt_payload)) return false;

    struct v2nt_inode_disk parent;
    struct v2nt_inode_disk child;
    struct v2nt_record_disk before;
    if (!read_inode(&device, &geometry, 0u, &parent) ||
        !read_inode(&device, &geometry, 1u, &child) ||
        !read_first_record(&device, &geometry, &parent, &before) || child.extent_count == 0u)
        return false;

    struct aurora_fs_v2_txn_record txn = {0};
    txn.state = AURORA_FS_V2_TXN_PREPARED;
    txn.operation = AURORA_FS_V2_TXN_OP_REMOVE;
    txn.sequence = 104u;
    txn.inode_index = 0u;
    txn.parent_inode_index = 0u;
    txn.child_inode_index = 1u;
    txn.child_object_id = child.object_id;
    txn.old_size = parent.size;
    txn.new_size = 0u;
    txn.namespace_slot_count = 1u;
    txn.namespace_slots[0].record_index = 0u;
    copy_bytes(txn.namespace_slots[0].before, &before, sizeof(before));
    txn.cleanup_range_count = child.extent_count;
    for (uint32_t i = 0u; i < child.extent_count; ++i) {
        txn.cleanup_ranges[i].first_block = child.extents[i].physical_block;
        txn.cleanup_ranges[i].block_count = child.extents[i].block_count;
    }
    if (!aurora_fs_v2_txn_prepare(&device, geometry.base_bytes, &txn) ||
        !aurora_fs_v2_remove_child(&allocator, &geometry, 0u, 1u, "alpha") ||
        !aurora_fs_v2_recover_pending_transaction(&allocator, &geometry)) return false;

    if (!txn_is_clean(&device, geometry.base_bytes) ||
        !lookup_state(&allocator, &geometry, "alpha", false) ||
        !read_inode(&device, &geometry, 1u, &child) || child.object_id != 0u) return false;
    for (uint32_t i = 0u; i < txn.cleanup_range_count; ++i) {
        for (uint64_t b = 0u; b < txn.cleanup_ranges[i].block_count; ++b) {
            bool allocated = true;
            if (!aurora_fs_v2_allocator_is_allocated(
                    &allocator, txn.cleanup_ranges[i].first_block + b, &allocated) || allocated)
                return false;
        }
    }
    return true;
}

static bool run_end_to_end(uint32_t block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!init_case(block_size, &device, &geometry, &allocator) ||
        !aurora_fs_v2_create_child_txn(
            &allocator, &geometry, 0u, 1u, 2u, AURORA_FS_V2_OBJECT_FILE, "alpha") ||
        !aurora_fs_v2_rename_child_txn(
            &allocator, &geometry, 0u, "alpha", "beta") ||
        !aurora_fs_v2_remove_child_txn(
            &allocator, &geometry, 0u, 1u, "beta")) return false;
    return txn_is_clean(&device, geometry.base_bytes) &&
        lookup_state(&allocator, &geometry, "alpha", false) &&
        lookup_state(&allocator, &geometry, "beta", false);
}

static bool run_geometry(uint32_t block_size) {
    return run_create_before_publish(block_size) &&
        run_create_after_publish(block_size) &&
        run_rename_before_publish(block_size) &&
        run_rename_after_publish(block_size) &&
        run_remove_after_publish(block_size) &&
        run_end_to_end(block_size);
}

bool aurora_fs_v2_namespace_txn_runtime_self_test(void) {
    return run_geometry(512u) && run_geometry(4096u);
}
