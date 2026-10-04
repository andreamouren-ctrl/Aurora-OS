#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/aurora_fs_v2_space_reclaim.h>
#include <aurora/block_device.h>

#define V2ST_STORAGE_BYTES (2u * 1024u * 1024u)
#define V2ST_INODE_SIZE 256u
#define V2ST_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2ST_INODE_SIZE)
#define V2ST_EXTENT_COUNT 6u

struct v2st_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2st_inode_disk {
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
    struct v2st_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct v2st_context {
    uint8_t *storage;
    uint64_t storage_bytes;
};

static uint8_t v2st_storage[V2ST_STORAGE_BYTES];
static uint8_t v2st_inode_block[AURORA_FS_V2_FS_BLOCK_SIZE];

_Static_assert(sizeof(struct v2st_inode_disk) == V2ST_INODE_SIZE,
               "AuroraFS v2 space reclaim test inode layout drifted");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static bool mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || (a != 0u && b > UINT64_MAX / a)) return false;
    *out = a * b;
    return true;
}

static bool test_read(struct aurora_block_device *device, uint64_t lba,
                      uint32_t block_count, void *buffer) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba)
        return false;
    struct v2st_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset)
        return false;
    uint8_t *out = buffer;
    for (uint64_t i = 0u; i < length; ++i) out[i] = context->storage[offset + i];
    return true;
}

static bool test_write(struct aurora_block_device *device, uint64_t lba,
                       uint32_t block_count, const void *buffer) {
    if (device == NULL || buffer == NULL || device->read_only || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba)
        return false;
    struct v2st_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset)
        return false;
    const uint8_t *in = buffer;
    for (uint64_t i = 0u; i < length; ++i) context->storage[offset + i] = in[i];
    return true;
}

static bool test_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct v2st_inode_disk *out_inode
) {
    if (device == NULL || geometry == NULL || out_inode == NULL ||
        inode_index >= geometry->inode_blocks * V2ST_INODES_PER_BLOCK) return false;
    uint64_t fs_block = geometry->inode_start + inode_index / V2ST_INODES_PER_BLOCK;
    uint64_t byte_offset = geometry->base_bytes + fs_block * AURORA_FS_V2_FS_BLOCK_SIZE;
    if ((byte_offset % device->block_size) != 0u) return false;
    uint64_t lba = byte_offset / device->block_size;
    uint32_t count = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    if (!block_device_read(device, lba, count, v2st_inode_block)) return false;
    *out_inode = ((const struct v2st_inode_disk *)v2st_inode_block)
        [inode_index % V2ST_INODES_PER_BLOCK];
    return true;
}

static bool seed_tree_file(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t object_id,
    uint64_t parent_id,
    uint64_t physical[V2ST_EXTENT_COUNT]
) {
    if (!aurora_fs_v2_inode_extent_init(
            allocator->device, geometry, inode_index, object_id, parent_id)) return false;

    for (uint32_t i = 0u; i < V2ST_EXTENT_COUNT; ++i) {
        uint64_t separator;
        if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &physical[i])) return false;
        if (i + 1u < V2ST_EXTENT_COUNT &&
            !aurora_fs_v2_allocator_allocate_range(allocator, 1u, &separator)) return false;
        struct aurora_fs_v2_extent extent = {
            .logical_block = i,
            .physical_block = physical[i],
            .block_count = 1u
        };
        if (i < AURORA_FS_V2_INLINE_EXTENT_COUNT + 1u) {
            if (!aurora_fs_v2_inode_extent_append(
                    allocator, geometry, inode_index, &extent)) return false;
        } else {
            if (!aurora_fs_v2_inode_extent_append_tree_cow(
                    allocator, geometry, inode_index, &extent)) return false;
        }
    }
    return true;
}

static bool run_geometry(uint32_t block_size) {
    zero_bytes(v2st_storage, sizeof(v2st_storage));
    struct v2st_context context = {
        .storage = v2st_storage,
        .storage_bytes = sizeof(v2st_storage)
    };
    struct aurora_block_device device = {
        .name = "aurorafs-v2-space-reclaim-test",
        .block_size = block_size,
        .block_count = sizeof(v2st_storage) / block_size,
        .read_only = false,
        .context = &context,
        .read_blocks = test_read,
        .write_blocks = test_write,
        .flush = test_flush
    };

    struct aurora_fs_v2_format_geometry geometry;
    if (!aurora_fs_v2_format_device(
            &device, AURORA_FS_V2_DEFAULT_BASE_BYTES, 0u, &geometry)) return false;

    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) return false;

    uint64_t physical[V2ST_EXTENT_COUNT];
    if (!seed_tree_file(&allocator, &geometry, 1u, 2u, 1u, physical)) return false;

    struct v2st_inode_disk before;
    if (!read_inode(&device, &geometry, 1u, &before) || before.extent_tree_root == 0u)
        return false;
    uint64_t old_root = before.extent_tree_root;

    if (!aurora_fs_v2_file_truncate_reclaim(
            &allocator, &geometry, 1u, 2u * AURORA_FS_V2_FS_BLOCK_SIZE)) return false;

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) return false;

    struct v2st_inode_disk after;
    if (!read_inode(&device, &geometry, 1u, &after) ||
        after.size != 2u * AURORA_FS_V2_FS_BLOCK_SIZE ||
        after.allocated_bytes != 2u * AURORA_FS_V2_FS_BLOCK_SIZE ||
        after.extent_tree_root == 0u || after.extent_tree_root == old_root) return false;

    for (uint64_t logical = 0u; logical < 2u; ++logical) {
        uint64_t resolved = 0u;
        uint64_t contiguous = 0u;
        if (!aurora_fs_v2_inode_extent_lookup_unified(
                &reopened, &geometry, 1u, logical, &resolved, &contiguous) ||
            resolved != physical[logical]) return false;
    }
    {
        uint64_t resolved = 0u;
        uint64_t contiguous = 0u;
        if (aurora_fs_v2_inode_extent_lookup_unified(
                &reopened, &geometry, 1u, 2u, &resolved, &contiguous)) return false;
    }

    bool allocated = true;
    if (!aurora_fs_v2_allocator_is_allocated(&reopened, old_root, &allocated) || allocated)
        return false;
    for (uint32_t i = 2u; i < V2ST_EXTENT_COUNT; ++i) {
        allocated = true;
        if (!aurora_fs_v2_allocator_is_allocated(&reopened, physical[i], &allocated) || allocated)
            return false;
    }

    return true;
}

bool aurora_fs_v2_space_reclaim_runtime_self_test(void) {
    return run_geometry(512u) && run_geometry(4096u);
}
