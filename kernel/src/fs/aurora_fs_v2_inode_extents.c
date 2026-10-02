#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define V2_INODE_SIZE 256u
#define V2_INODE_FILE 1u
#define V2_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2_INODE_SIZE)
#define V2_INODE_TEST_DEVICE_BYTES (2u * 1024u * 1024u)

struct v2_inode_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2_inode_disk {
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
    struct v2_inode_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct inode_extent_test_context {
    uint8_t *storage;
    uint64_t storage_bytes;
};

static uint8_t inode_io_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t inode_extent_test_storage[V2_INODE_TEST_DEVICE_BYTES];

_Static_assert(sizeof(struct v2_inode_extent_disk) == 24u,
               "AuroraFS v2 inode extent must remain 24 bytes");
_Static_assert(sizeof(struct v2_inode_disk) == V2_INODE_SIZE,
               "AuroraFS v2 inode must remain 256 bytes");

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
    if (device == NULL || geometry == NULL || device->block_size == 0u ||
        device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) != 0u ||
        (geometry->base_bytes % device->block_size) != 0u ||
        geometry->inode_blocks == 0u || geometry->inode_start >= geometry->data_start ||
        geometry->data_start >= geometry->total_fs_blocks ||
        geometry->inode_blocks > geometry->data_start - geometry->inode_start) {
        return false;
    }
    return true;
}

static bool fs_block_geometry(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_device_blocks
) {
    if (!geometry_valid(device, geometry) || out_lba == NULL ||
        out_device_blocks == NULL || fs_block >= geometry->total_fs_blocks) {
        return false;
    }

    uint64_t fs_bytes;
    uint64_t byte_offset;
    if (!mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_bytes) ||
        !add_u64(geometry->base_bytes, fs_bytes, &byte_offset)) {
        return false;
    }

    uint64_t device_blocks = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    uint64_t lba = byte_offset / device->block_size;
    if (device_blocks == 0u || device_blocks > UINT32_MAX ||
        lba >= device->block_count || device_blocks > device->block_count - lba) {
        return false;
    }

    *out_lba = lba;
    *out_device_blocks = (uint32_t)device_blocks;
    return true;
}

static bool read_fs_block(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    void *buffer
) {
    uint64_t lba;
    uint32_t device_blocks;
    return buffer != NULL &&
        fs_block_geometry(device, geometry, fs_block, &lba, &device_blocks) &&
        block_device_read(device, lba, device_blocks, buffer);
}

static bool write_fs_block(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    const void *buffer
) {
    uint64_t lba;
    uint32_t device_blocks;
    return buffer != NULL && !device->read_only &&
        fs_block_geometry(device, geometry, fs_block, &lba, &device_blocks) &&
        block_device_write(device, lba, device_blocks, buffer);
}

static bool inode_location(
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t *out_fs_block,
    uint32_t *out_slot
) {
    if (geometry == NULL || out_fs_block == NULL || out_slot == NULL ||
        inode_index >= geometry->inode_blocks * V2_INODES_PER_BLOCK) {
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
    struct v2_inode_disk *out_inode
) {
    uint64_t fs_block;
    uint32_t slot;
    if (out_inode == NULL || !inode_location(geometry, inode_index, &fs_block, &slot) ||
        !read_fs_block(device, geometry, fs_block, inode_io_block)) {
        return false;
    }

    const struct v2_inode_disk *inodes = (const struct v2_inode_disk *)inode_io_block;
    *out_inode = inodes[slot];
    return true;
}

static bool write_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct v2_inode_disk *inode
) {
    uint64_t fs_block;
    uint32_t slot;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_location(geometry, inode_index, &fs_block, &slot) ||
        !read_fs_block(device, geometry, fs_block, inode_io_block)) {
        return false;
    }

    struct v2_inode_disk *inodes = (struct v2_inode_disk *)inode_io_block;
    inodes[slot] = *inode;
    return write_fs_block(device, geometry, fs_block, inode_io_block) &&
        block_device_flush(device);
}

static bool extent_valid(
    const struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_extent *extent
) {
    uint64_t logical_end;
    uint64_t physical_end;
    return allocator != NULL && extent != NULL && extent->block_count != 0u &&
        extent->physical_block >= allocator->data_start &&
        add_u64(extent->logical_block, extent->block_count, &logical_end) &&
        add_u64(extent->physical_block, extent->block_count, &physical_end) &&
        physical_end <= allocator->total_fs_blocks;
}

static bool inode_extent_sequence_valid(
    const struct v2_inode_disk *inode,
    const struct aurora_fs_v2_extent *next_extent
) {
    if (inode == NULL || next_extent == NULL || inode->extent_tree_root != 0u ||
        inode->extent_count > AURORA_FS_V2_INLINE_EXTENT_COUNT) {
        return false;
    }
    if (inode->extent_count == 0u) {
        return true;
    }

    const struct v2_inode_extent_disk *last = &inode->extents[inode->extent_count - 1u];
    uint64_t last_end;
    return add_u64(last->logical_block, last->block_count, &last_end) &&
        next_extent->logical_block >= last_end;
}

bool aurora_fs_v2_inode_extent_init(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t object_id,
    uint64_t parent_object_id
) {
    if (!geometry_valid(device, geometry) || device->read_only || object_id == 0u) {
        return false;
    }

    struct v2_inode_disk inode;
    zero_bytes(&inode, sizeof(inode));
    inode.object_id = object_id;
    inode.parent_object_id = parent_object_id;
    inode.generation = 1u;
    inode.type = V2_INODE_FILE;
    return write_inode(device, geometry, inode_index, &inode);
}

bool aurora_fs_v2_inode_extent_append(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct aurora_fs_v2_extent *extent
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        !geometry_valid(allocator->device, geometry) || !extent_valid(allocator, extent)) {
        return false;
    }

    struct v2_inode_disk inode;
    if (!read_inode(allocator->device, geometry, inode_index, &inode) ||
        inode.object_id == 0u || inode.type != V2_INODE_FILE ||
        !inode_extent_sequence_valid(&inode, extent)) {
        return false;
    }

    uint64_t logical_end;
    uint64_t allocated_add;
    uint64_t new_allocated;
    if (!add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        !mul_u64(extent->block_count, AURORA_FS_V2_FS_BLOCK_SIZE, &allocated_add) ||
        !add_u64(inode.allocated_bytes, allocated_add, &new_allocated)) {
        return false;
    }

    if (inode.extent_count < AURORA_FS_V2_INLINE_EXTENT_COUNT) {
        uint32_t slot = inode.extent_count;
        inode.extents[slot].logical_block = extent->logical_block;
        inode.extents[slot].physical_block = extent->physical_block;
        inode.extents[slot].block_count = extent->block_count;
        inode.extent_count++;
    } else {
        struct aurora_fs_v2_extent promoted[AURORA_FS_V2_INLINE_EXTENT_COUNT + 1u];
        for (uint32_t i = 0u; i < AURORA_FS_V2_INLINE_EXTENT_COUNT; ++i) {
            promoted[i].logical_block = inode.extents[i].logical_block;
            promoted[i].physical_block = inode.extents[i].physical_block;
            promoted[i].block_count = inode.extents[i].block_count;
        }
        promoted[AURORA_FS_V2_INLINE_EXTENT_COUNT] = *extent;

        uint64_t root_block;
        if (!aurora_fs_v2_extent_tree_write(
                allocator,
                promoted,
                AURORA_FS_V2_INLINE_EXTENT_COUNT + 1u,
                &root_block)) {
            return false;
        }

        inode.extent_tree_root = root_block;
        inode.extent_count = AURORA_FS_V2_INLINE_EXTENT_COUNT + 1u;
        for (uint32_t i = 0u; i < AURORA_FS_V2_INLINE_EXTENT_COUNT; ++i) {
            inode.extents[i].logical_block = 0u;
            inode.extents[i].physical_block = 0u;
            inode.extents[i].block_count = 0u;
        }
    }

    inode.allocated_bytes = new_allocated;
    uint64_t logical_bytes;
    if (!mul_u64(logical_end, AURORA_FS_V2_FS_BLOCK_SIZE, &logical_bytes)) {
        return false;
    }
    if (logical_bytes > inode.size) {
        inode.size = logical_bytes;
    }
    inode.generation++;
    return write_inode(allocator->device, geometry, inode_index, &inode);
}

bool aurora_fs_v2_inode_extent_lookup(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t logical_block,
    uint64_t *out_physical_block,
    uint64_t *out_contiguous_blocks
) {
    if (allocator == NULL || allocator->device == NULL || out_physical_block == NULL ||
        out_contiguous_blocks == NULL || !geometry_valid(allocator->device, geometry)) {
        return false;
    }

    struct v2_inode_disk inode;
    if (!read_inode(allocator->device, geometry, inode_index, &inode) ||
        inode.object_id == 0u || inode.type != V2_INODE_FILE || inode.extent_count == 0u) {
        return false;
    }

    if (inode.extent_tree_root != 0u) {
        return aurora_fs_v2_extent_tree_lookup(
            allocator,
            inode.extent_tree_root,
            logical_block,
            out_physical_block,
            out_contiguous_blocks);
    }

    if (inode.extent_count > AURORA_FS_V2_INLINE_EXTENT_COUNT) {
        return false;
    }
    for (uint32_t i = 0u; i < inode.extent_count; ++i) {
        uint64_t end;
        if (inode.extents[i].block_count == 0u ||
            !add_u64(inode.extents[i].logical_block, inode.extents[i].block_count, &end)) {
            return false;
        }
        if (logical_block >= inode.extents[i].logical_block && logical_block < end) {
            uint64_t within = logical_block - inode.extents[i].logical_block;
            *out_physical_block = inode.extents[i].physical_block + within;
            *out_contiguous_blocks = inode.extents[i].block_count - within;
            return true;
        }
    }
    return false;
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
    struct inode_extent_test_context *context = device->context;
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
    struct inode_extent_test_context *context = device->context;
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

static bool run_inode_extent_geometry(uint32_t device_block_size) {
    zero_bytes(inode_extent_test_storage, sizeof(inode_extent_test_storage));
    struct inode_extent_test_context context = {
        .storage = inode_extent_test_storage,
        .storage_bytes = sizeof(inode_extent_test_storage)
    };
    struct aurora_block_device device = {
        .name = "aurorafs-v2-inode-extents-test",
        .block_size = device_block_size,
        .block_count = sizeof(inode_extent_test_storage) / device_block_size,
        .read_only = false,
        .context = &context,
        .read_blocks = test_read,
        .write_blocks = test_write,
        .flush = test_flush
    };

    struct aurora_fs_v2_format_geometry geometry;
    if (!aurora_fs_v2_format_device(
            &device,
            AURORA_FS_V2_DEFAULT_BASE_BYTES,
            0u,
            &geometry)) {
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

    uint64_t expected_physical[AURORA_FS_V2_INLINE_EXTENT_COUNT + 1u];
    for (uint32_t i = 0u; i < AURORA_FS_V2_INLINE_EXTENT_COUNT + 1u; ++i) {
        uint64_t physical;
        uint64_t separator;
        if (!aurora_fs_v2_allocator_allocate_range(&allocator, 1u, &physical)) {
            return false;
        }
        expected_physical[i] = physical;
        if (i != AURORA_FS_V2_INLINE_EXTENT_COUNT &&
            !aurora_fs_v2_allocator_allocate_range(&allocator, 1u, &separator)) {
            return false;
        }

        struct aurora_fs_v2_extent extent = {
            .logical_block = i,
            .physical_block = physical,
            .block_count = 1u
        };
        if (!aurora_fs_v2_inode_extent_append(&allocator, &geometry, 1u, &extent)) {
            return false;
        }
    }

    struct v2_inode_disk persisted;
    if (!read_inode(&device, &geometry, 1u, &persisted) ||
        persisted.extent_count != AURORA_FS_V2_INLINE_EXTENT_COUNT + 1u ||
        persisted.extent_tree_root < geometry.data_start || persisted.generation != 6u) {
        return false;
    }
    for (uint32_t i = 0u; i < AURORA_FS_V2_INLINE_EXTENT_COUNT; ++i) {
        if (persisted.extents[i].logical_block != 0u ||
            persisted.extents[i].physical_block != 0u ||
            persisted.extents[i].block_count != 0u) {
            return false;
        }
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

    for (uint32_t i = 0u; i < AURORA_FS_V2_INLINE_EXTENT_COUNT + 1u; ++i) {
        uint64_t physical = 0u;
        uint64_t contiguous = 0u;
        if (!aurora_fs_v2_inode_extent_lookup(
                &reopened,
                &geometry,
                1u,
                i,
                &physical,
                &contiguous) ||
            physical != expected_physical[i] || contiguous != 1u) {
            return false;
        }
    }

    return true;
}

bool aurora_fs_v2_inode_extent_self_test(void) {
    return run_inode_extent_geometry(512u) && run_inode_extent_geometry(4096u);
}
