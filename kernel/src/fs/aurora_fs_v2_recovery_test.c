#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_file_io.h>
#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/aurora_fs_v2_recovery.h>
#include <aurora/aurora_fs_v2_txn.h>
#include <aurora/block_device.h>

#define V2RT_STORAGE_BYTES (2u * 1024u * 1024u)
#define V2RT_FILE_INODE 1u

struct v2rt_context {
    uint8_t storage[V2RT_STORAGE_BYTES];
};

static struct v2rt_context v2rt_context;

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static bool v2rt_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba)
        return false;
    struct v2rt_context *context = device->context;
    uint64_t offset = lba * device->block_size;
    uint64_t length = (uint64_t)block_count * device->block_size;
    if (context == NULL || offset > V2RT_STORAGE_BYTES || length > V2RT_STORAGE_BYTES - offset)
        return false;
    uint8_t *out = buffer;
    for (uint64_t i = 0u; i < length; ++i) out[i] = context->storage[offset + i];
    return true;
}

static bool v2rt_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    if (device == NULL || device->read_only || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba)
        return false;
    struct v2rt_context *context = device->context;
    uint64_t offset = lba * device->block_size;
    uint64_t length = (uint64_t)block_count * device->block_size;
    if (context == NULL || offset > V2RT_STORAGE_BYTES || length > V2RT_STORAGE_BYTES - offset)
        return false;
    const uint8_t *in = buffer;
    for (uint64_t i = 0u; i < length; ++i) context->storage[offset + i] = in[i];
    return true;
}

static bool v2rt_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool init_case(
    uint32_t device_block_size,
    struct aurora_block_device *device,
    struct aurora_fs_v2_format_geometry *geometry,
    struct aurora_fs_v2_allocator *allocator
) {
    if (device == NULL || geometry == NULL || allocator == NULL ||
        device_block_size == 0u || (V2RT_STORAGE_BYTES % device_block_size) != 0u)
        return false;

    zero_bytes(&v2rt_context, sizeof(v2rt_context));
    *device = (struct aurora_block_device){
        .name = "aurorafs-v2-recovery-test",
        .block_size = device_block_size,
        .block_count = V2RT_STORAGE_BYTES / device_block_size,
        .read_only = false,
        .context = &v2rt_context,
        .read_blocks = v2rt_read,
        .write_blocks = v2rt_write,
        .flush = v2rt_flush
    };

    if (!aurora_fs_v2_format_device(
            device, AURORA_FS_V2_DEFAULT_BASE_BYTES, 0u, geometry)) return false;
    if (!aurora_fs_v2_allocator_init(
            allocator, device, geometry->base_bytes, geometry->total_fs_blocks,
            geometry->bitmap_start, geometry->bitmap_blocks, geometry->data_start)) return false;
    return aurora_fs_v2_object_init(
        device, geometry, V2RT_FILE_INODE, 2u, 1u, AURORA_FS_V2_OBJECT_FILE);
}

static bool block_is_allocated(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t block,
    bool expected
) {
    bool allocated = false;
    return aurora_fs_v2_allocator_is_allocated(allocator, block, &allocated) &&
        allocated == expected;
}

static bool txn_is_clean(struct aurora_block_device *device, uint64_t base_bytes) {
    struct aurora_fs_v2_txn_record record;
    return aurora_fs_v2_txn_load(device, base_bytes, &record) &&
        record.state == AURORA_FS_V2_TXN_CLEAN;
}

static bool run_prepared_before_publish(uint32_t device_block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!init_case(device_block_size, &device, &geometry, &allocator)) return false;

    uint64_t rollback_block;
    if (!aurora_fs_v2_allocator_allocate_range(&allocator, 1u, &rollback_block)) return false;

    struct aurora_fs_v2_txn_record txn = {0};
    txn.state = AURORA_FS_V2_TXN_PREPARED;
    txn.operation = AURORA_FS_V2_TXN_OP_FILE_GROW;
    txn.sequence = 1u;
    txn.inode_index = V2RT_FILE_INODE;
    txn.old_root = 0u;
    txn.new_root = 0u;
    txn.old_size = 0u;
    txn.new_size = AURORA_FS_V2_FS_BLOCK_SIZE;
    txn.rollback_range_count = 1u;
    txn.rollback_ranges[0] = (struct aurora_fs_v2_txn_range){rollback_block, 1u};
    if (!aurora_fs_v2_txn_prepare(&device, geometry.base_bytes, &txn)) return false;

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start) ||
        !aurora_fs_v2_recover_pending_transaction(&reopened, &geometry)) return false;

    return block_is_allocated(&reopened, rollback_block, false) &&
        txn_is_clean(&device, geometry.base_bytes);
}

static bool run_prepared_after_publish(uint32_t device_block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!init_case(device_block_size, &device, &geometry, &allocator)) return false;

    uint64_t cleanup_block;
    if (!aurora_fs_v2_allocator_allocate_range(&allocator, 1u, &cleanup_block)) return false;

    struct aurora_fs_v2_txn_record txn = {0};
    txn.state = AURORA_FS_V2_TXN_PREPARED;
    txn.operation = AURORA_FS_V2_TXN_OP_FILE_GROW;
    txn.sequence = 2u;
    txn.inode_index = V2RT_FILE_INODE;
    txn.old_root = 0u;
    txn.new_root = 0u;
    txn.old_size = 0u;
    txn.new_size = AURORA_FS_V2_FS_BLOCK_SIZE;
    txn.cleanup_range_count = 1u;
    txn.cleanup_ranges[0] = (struct aurora_fs_v2_txn_range){cleanup_block, 1u};
    if (!aurora_fs_v2_txn_prepare(&device, geometry.base_bytes, &txn) ||
        !aurora_fs_v2_file_truncate(
            &allocator, &geometry, V2RT_FILE_INODE, AURORA_FS_V2_FS_BLOCK_SIZE)) return false;

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start) ||
        !aurora_fs_v2_recover_pending_transaction(&reopened, &geometry)) return false;

    return block_is_allocated(&reopened, cleanup_block, false) &&
        txn_is_clean(&device, geometry.base_bytes);
}

static bool run_committed_before_cleanup(uint32_t device_block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!init_case(device_block_size, &device, &geometry, &allocator)) return false;

    uint64_t cleanup_block;
    if (!aurora_fs_v2_allocator_allocate_range(&allocator, 1u, &cleanup_block)) return false;

    struct aurora_fs_v2_txn_record txn = {0};
    txn.state = AURORA_FS_V2_TXN_PREPARED;
    txn.operation = AURORA_FS_V2_TXN_OP_FILE_GROW;
    txn.sequence = 3u;
    txn.inode_index = V2RT_FILE_INODE;
    txn.old_root = 0u;
    txn.new_root = 0u;
    txn.old_size = 0u;
    txn.new_size = AURORA_FS_V2_FS_BLOCK_SIZE;
    txn.cleanup_range_count = 1u;
    txn.cleanup_ranges[0] = (struct aurora_fs_v2_txn_range){cleanup_block, 1u};
    if (!aurora_fs_v2_txn_prepare(&device, geometry.base_bytes, &txn) ||
        !aurora_fs_v2_file_truncate(
            &allocator, &geometry, V2RT_FILE_INODE, AURORA_FS_V2_FS_BLOCK_SIZE) ||
        !aurora_fs_v2_txn_mark_committed(&device, geometry.base_bytes, txn.sequence))
        return false;

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start) ||
        !aurora_fs_v2_recover_pending_transaction(&reopened, &geometry)) return false;

    return block_is_allocated(&reopened, cleanup_block, false) &&
        txn_is_clean(&device, geometry.base_bytes);
}

static bool run_ambiguous_inode_guard(uint32_t device_block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!init_case(device_block_size, &device, &geometry, &allocator)) return false;

    uint64_t rollback_block;
    if (!aurora_fs_v2_allocator_allocate_range(&allocator, 1u, &rollback_block)) return false;

    struct aurora_fs_v2_txn_record txn = {0};
    txn.state = AURORA_FS_V2_TXN_PREPARED;
    txn.operation = AURORA_FS_V2_TXN_OP_FILE_GROW;
    txn.sequence = 4u;
    txn.inode_index = V2RT_FILE_INODE;
    txn.old_root = 0u;
    txn.new_root = 0u;
    txn.old_size = 0u;
    txn.new_size = AURORA_FS_V2_FS_BLOCK_SIZE;
    txn.rollback_range_count = 1u;
    txn.rollback_ranges[0] = (struct aurora_fs_v2_txn_range){rollback_block, 1u};
    if (!aurora_fs_v2_txn_prepare(&device, geometry.base_bytes, &txn) ||
        !aurora_fs_v2_file_truncate(
            &allocator, &geometry, V2RT_FILE_INODE, AURORA_FS_V2_FS_BLOCK_SIZE * 2u))
        return false;

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) return false;
    if (aurora_fs_v2_recover_pending_transaction(&reopened, &geometry)) return false;

    struct aurora_fs_v2_txn_record persisted;
    return block_is_allocated(&reopened, rollback_block, true) &&
        aurora_fs_v2_txn_load(&device, geometry.base_bytes, &persisted) &&
        persisted.state == AURORA_FS_V2_TXN_PREPARED && persisted.sequence == txn.sequence;
}

static bool run_geometry(uint32_t device_block_size) {
    return run_prepared_before_publish(device_block_size) &&
        run_prepared_after_publish(device_block_size) &&
        run_committed_before_cleanup(device_block_size) &&
        run_ambiguous_inode_guard(device_block_size);
}

bool aurora_fs_v2_recovery_self_test(void) {
    return run_geometry(512u) && run_geometry(4096u);
}
