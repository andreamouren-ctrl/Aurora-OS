#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define V2_INODE_SIZE 256u
#define V2_INODE_FILE 1u
#define V2_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2_INODE_SIZE)
#define V2_TREE_APPEND_TEST_BYTES (2u * 1024u * 1024u)

struct tree_append_inode_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct tree_append_inode_disk {
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
    struct tree_append_inode_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct tree_append_test_context {
    uint8_t *storage;
    uint64_t storage_bytes;
};

static uint8_t tree_append_io[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t tree_append_storage[V2_TREE_APPEND_TEST_BYTES];

_Static_assert(sizeof(struct tree_append_inode_disk) == V2_INODE_SIZE,
               "AuroraFS v2 inode layout must remain 256 bytes");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) {
        bytes[i] = 0u;
    }
}

static bool add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || b > UINT64_MAX - a) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || (a != 0u && b > UINT64_MAX / a)) {
        return false;
    }
    *out = a * b;
    return true;
}

static bool geometry_valid(
    const struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry
) {
    return device != NULL && geometry != NULL && device->block_size != 0u &&
        device->block_size <= AURORA_FS_V2_FS_BLOCK_SIZE &&
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) == 0u &&
        (geometry->base_bytes % device->block_size) == 0u &&
        geometry->inode_blocks != 0u && geometry->inode_start < geometry->data_start &&
        geometry->data_start < geometry->total_fs_blocks;
}

static bool fs_block_lba(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_count
) {
    if (!geometry_valid(device, geometry) || out_lba == NULL || out_count == NULL ||
        fs_block >= geometry->total_fs_blocks) {
        return false;
    }
    uint64_t fs_offset;
    uint64_t byte_offset;
    if (!mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_offset) ||
        !add_u64(geometry->base_bytes, fs_offset, &byte_offset)) {
        return false;
    }
    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    uint64_t lba = byte_offset / device->block_size;
    if (count == 0u || count > UINT32_MAX || lba >= device->block_count ||
        count > device->block_count - lba) {
        return false;
    }
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
    return buffer != NULL && fs_block_lba(device, geometry, fs_block, &lba, &count) &&
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
    return buffer != NULL && !device->read_only &&
        fs_block_lba(device, geometry, fs_block, &lba, &count) &&
        block_device_write(device, lba, count, buffer);
}

static bool inode_position(
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t *out_fs_block,
    uint32_t *out_slot
) {
    uint64_t inode_capacity;
    if (geometry == NULL || out_fs_block == NULL || out_slot == NULL ||
        !mul_u64(geometry->inode_blocks, V2_INODES_PER_BLOCK, &inode_capacity) ||
        inode_index >= inode_capacity) {
        return false;
    }
    *out_fs_block = geometry->inode_start + inode_index / V2_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(inode_index % V2_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct tree_append_inode_disk *out_inode
) {
    uint64_t fs_block;
    uint32_t slot;
    if (out_inode == NULL || !inode_position(geometry, inode_index, &fs_block, &slot) ||
        !read_fs_block(device, geometry, fs_block, tree_append_io)) {
        return false;
    }
    const struct tree_append_inode_disk *inodes =
        (const struct tree_append_inode_disk *)tree_append_io;
    *out_inode = inodes[slot];
    return true;
}

static bool write_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct tree_append_inode_disk *inode
) {
    uint64_t fs_block;
    uint32_t slot;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_position(geometry, inode_index, &fs_block, &slot) ||
        !read_fs_block(device, geometry, fs_block, tree_append_io)) {
        return false;
    }
    struct tree_append_inode_disk *inodes =
        (struct tree_append_inode_disk *)tree_append_io;
    inodes[slot] = *inode;
    return write_fs_block(device, geometry, fs_block, tree_append_io) &&
        block_device_flush(device);
}

static bool extent_valid(
    const struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_extent *extent
) {
    uint64_t physical_end;
    uint64_t logical_end;
    return allocator != NULL && extent != NULL && extent->block_count != 0u &&
        extent->physical_block >= allocator->data_start &&
        add_u64(extent->physical_block, extent->block_count, &physical_end) &&
        physical_end <= allocator->total_fs_blocks &&
        add_u64(extent->logical_block, extent->block_count, &logical_end);
}

bool aurora_fs_v2_inode_extent_append_tree_cow(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct aurora_fs_v2_extent *extent
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        !geometry_valid(allocator->device, geometry) || !extent_valid(allocator, extent)) {
        return false;
    }

    struct tree_append_inode_disk inode;
    if (!read_inode(allocator->device, geometry, inode_index, &inode) ||
        inode.object_id == 0u || inode.type != V2_INODE_FILE ||
        inode.extent_tree_root == 0u ||
        inode.extent_count < AURORA_FS_V2_INLINE_EXTENT_COUNT + 1u) {
        return false;
    }

    uint64_t new_root;
    if (!aurora_fs_v2_extent_tree_clone_append_leaf(
            allocator, inode.extent_tree_root, extent, &new_root)) {
        return false;
    }

    uint64_t logical_end;
    uint64_t logical_bytes;
    uint64_t allocated_add;
    uint64_t new_allocated;
    if (!add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        !mul_u64(logical_end, AURORA_FS_V2_FS_BLOCK_SIZE, &logical_bytes) ||
        !mul_u64(extent->block_count, AURORA_FS_V2_FS_BLOCK_SIZE, &allocated_add) ||
        !add_u64(inode.allocated_bytes, allocated_add, &new_allocated) ||
        inode.extent_count == UINT32_MAX) {
        return false;
    }

    inode.extent_tree_root = new_root;
    inode.extent_count++;
    inode.allocated_bytes = new_allocated;
    if (logical_bytes > inode.size) {
        inode.size = logical_bytes;
    }
    inode.generation++;

    return write_inode(allocator->device, geometry, inode_index, &inode);
}

static bool test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }
    struct tree_append_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset) {
        return false;
    }
    uint8_t *out = buffer;
    for (uint64_t i = 0u; i < length; ++i) {
        out[i] = context->storage[offset + i];
    }
    return true;
}

static bool test_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    if (device == NULL || buffer == NULL || device->read_only || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }
    struct tree_append_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset) {
        return false;
    }
    const uint8_t *source = buffer;
    for (uint64_t i = 0u; i < length; ++i) {
        context->storage[offset + i] = source[i];
    }
    return true;
}

static bool test_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool run_tree_append_geometry(uint32_t device_block_size) {
    zero_bytes(tree_append_storage, sizeof(tree_append_storage));
    struct tree_append_test_context context = {
        .storage = tree_append_storage,
        .storage_bytes = sizeof(tree_append_storage)
    };
    struct aurora_block_device device = {
        .name = "aurorafs-v2-tree-append-test",
        .block_size = device_block_size,
        .block_count = sizeof(tree_append_storage) / device_block_size,
        .read_only = false,
        .context = &context,
        .read_blocks = test_read,
        .write_blocks = test_write,
        .flush = test_flush
    };

    struct aurora_fs_v2_format_geometry geometry;
    if (!aurora_fs_v2_format_device(
            &device, AURORA_FS_V2_DEFAULT_BASE_BYTES, 0u, &geometry)) {
        return false;
    }

    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator,
            &device,
            geometry.base_bytes,
            geometry.total_fs_blocks,
            geometry.bitmap_start,
            geometry.bitmap_blocks,
            geometry.data_start) ||
        !aurora_fs_v2_inode_extent_init(&device, &geometry, 1u, 2u, 1u)) {
        return false;
    }

    uint64_t expected[6];
    for (uint32_t i = 0u; i < 6u; ++i) {
        uint64_t physical;
        uint64_t separator;
        if (!aurora_fs_v2_allocator_allocate_range(&allocator, 1u, &physical)) {
            return false;
        }
        expected[i] = physical;
        if (i != 5u && !aurora_fs_v2_allocator_allocate_range(&allocator, 1u, &separator)) {
            return false;
        }

        struct aurora_fs_v2_extent extent = {
            .logical_block = i,
            .physical_block = physical,
            .block_count = 1u
        };
        bool appended = i < 5u ?
            aurora_fs_v2_inode_extent_append(&allocator, &geometry, 1u, &extent) :
            aurora_fs_v2_inode_extent_append_tree_cow(&allocator, &geometry, 1u, &extent);
        if (!appended) {
            return false;
        }
    }

    struct tree_append_inode_disk inode;
    if (!read_inode(&device, &geometry, 1u, &inode) || inode.extent_count != 6u ||
        inode.extent_tree_root < geometry.data_start || inode.generation != 7u) {
        return false;
    }

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened,
            &device,
            geometry.base_bytes,
            geometry.total_fs_blocks,
            geometry.bitmap_start,
            geometry.bitmap_blocks,
            geometry.data_start)) {
        return false;
    }

    for (uint32_t i = 0u; i < 6u; ++i) {
        uint64_t physical = 0u;
        uint64_t contiguous = 0u;
        if (!aurora_fs_v2_inode_extent_lookup(
                &reopened, &geometry, 1u, i, &physical, &contiguous) ||
            physical != expected[i] || contiguous != 1u) {
            return false;
        }
    }

    return true;
}

bool aurora_fs_v2_inode_tree_append_self_test(void) {
    return run_tree_append_geometry(512u) && run_tree_append_geometry(4096u);
}
