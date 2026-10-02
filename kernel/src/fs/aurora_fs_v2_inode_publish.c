#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2_inode_publish.h>
#include <aurora/block_device.h>

#define PUBLISH_INODE_SIZE 256u
#define PUBLISH_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / PUBLISH_INODE_SIZE)
#define PUBLISH_INODE_FILE 1u
#define PUBLISH_TEST_BLOCKS 64u
#define PUBLISH_TEST_BITMAP_START 1u
#define PUBLISH_TEST_INODE_START 2u
#define PUBLISH_TEST_INODE_BLOCKS 1u
#define PUBLISH_TEST_DATA_START 8u
#define PUBLISH_TEST_OLD_ROOT 12u
#define PUBLISH_TEST_NEW_ROOT 13u
#define PUBLISH_TEST_DATA_BLOCK 20u

struct publish_inode_extent {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct publish_inode {
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
    struct publish_inode_extent extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct publish_test_context {
    uint8_t storage[(PUBLISH_TEST_BLOCKS + 1u) * AURORA_FS_V2_FS_BLOCK_SIZE];
};

static uint8_t publish_inode_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static struct publish_test_context publish_test;

_Static_assert(sizeof(struct publish_inode) == PUBLISH_INODE_SIZE,
               "AuroraFS v2 inode publication expects 256-byte inode");

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

static bool geometry_lba(
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
    uint64_t block,
    void *buffer
) {
    uint64_t lba;
    uint32_t count;
    return buffer != NULL && geometry_lba(device, geometry, block, &lba, &count) &&
        block_device_read(device, lba, count, buffer);
}

static bool write_fs_block(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t block,
    const void *buffer
) {
    uint64_t lba;
    uint32_t count;
    return buffer != NULL && device != NULL && !device->read_only &&
        geometry_lba(device, geometry, block, &lba, &count) &&
        block_device_write(device, lba, count, buffer);
}

static bool inode_position(
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t index,
    uint64_t *out_block,
    uint32_t *out_slot
) {
    uint64_t capacity;
    if (geometry == NULL || out_block == NULL || out_slot == NULL ||
        !mul_u64(geometry->inode_blocks, PUBLISH_INODES_PER_BLOCK, &capacity) ||
        index >= capacity) {
        return false;
    }
    *out_block = geometry->inode_start + index / PUBLISH_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(index % PUBLISH_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t index,
    struct publish_inode *out_inode
) {
    uint64_t block;
    uint32_t slot;
    if (out_inode == NULL || !inode_position(geometry, index, &block, &slot) ||
        !read_fs_block(device, geometry, block, publish_inode_block)) {
        return false;
    }
    *out_inode = ((const struct publish_inode *)publish_inode_block)[slot];
    return true;
}

static bool write_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t index,
    const struct publish_inode *inode
) {
    uint64_t block;
    uint32_t slot;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_position(geometry, index, &block, &slot) ||
        !read_fs_block(device, geometry, block, publish_inode_block)) {
        return false;
    }
    ((struct publish_inode *)publish_inode_block)[slot] = *inode;
    return write_fs_block(device, geometry, block, publish_inode_block) &&
        block_device_flush(device);
}

bool aurora_fs_v2_inode_publish_extent_root_cow(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t expected_old_root,
    uint64_t new_root,
    const struct aurora_fs_v2_extent *extent
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        !geometry_valid(allocator->device, geometry) || extent == NULL ||
        extent->block_count == 0u || expected_old_root < allocator->data_start ||
        new_root < allocator->data_start || new_root >= allocator->total_fs_blocks ||
        expected_old_root >= allocator->total_fs_blocks || new_root == expected_old_root ||
        extent->physical_block < allocator->data_start) {
        return false;
    }

    bool root_allocated = false;
    uint64_t physical_end;
    uint64_t logical_end;
    uint64_t logical_bytes;
    uint64_t allocated_add;
    if (!aurora_fs_v2_allocator_is_allocated(allocator, new_root, &root_allocated) ||
        !root_allocated ||
        !add_u64(extent->physical_block, extent->block_count, &physical_end) ||
        physical_end > allocator->total_fs_blocks ||
        !add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        !mul_u64(logical_end, AURORA_FS_V2_FS_BLOCK_SIZE, &logical_bytes) ||
        !mul_u64(extent->block_count, AURORA_FS_V2_FS_BLOCK_SIZE, &allocated_add)) {
        return false;
    }

    struct publish_inode inode;
    if (!read_inode(allocator->device, geometry, inode_index, &inode) ||
        inode.object_id == 0u || inode.type != PUBLISH_INODE_FILE ||
        inode.extent_tree_root != expected_old_root || inode.extent_count == UINT32_MAX ||
        allocated_add > UINT64_MAX - inode.allocated_bytes) {
        return false;
    }

    inode.extent_tree_root = new_root;
    inode.extent_count++;
    inode.allocated_bytes += allocated_add;
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
    uint64_t offset;
    uint64_t length;
    if (!mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > sizeof(publish_test.storage) || length > sizeof(publish_test.storage) - offset) {
        return false;
    }
    uint8_t *out = buffer;
    for (uint64_t i = 0u; i < length; ++i) {
        out[i] = publish_test.storage[offset + i];
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
    uint64_t offset;
    uint64_t length;
    if (!mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > sizeof(publish_test.storage) || length > sizeof(publish_test.storage) - offset) {
        return false;
    }
    const uint8_t *in = buffer;
    for (uint64_t i = 0u; i < length; ++i) {
        publish_test.storage[offset + i] = in[i];
    }
    return true;
}

static bool test_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static void bitmap_set(uint64_t block) {
    uint64_t bitmap_byte_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        PUBLISH_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE + (block >> 3);
    publish_test.storage[bitmap_byte_offset] |= (uint8_t)(1u << (block & 7u));
}

static bool run_test(uint32_t block_size) {
    zero_bytes(&publish_test, sizeof(publish_test));
    uint64_t bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        (uint64_t)PUBLISH_TEST_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct aurora_block_device device = {
        .name = "aurorafs-v2-inode-publish-test",
        .block_size = block_size,
        .block_count = bytes / block_size,
        .read_only = false,
        .context = &publish_test,
        .read_blocks = test_read,
        .write_blocks = test_write,
        .flush = test_flush
    };
    struct aurora_fs_v2_format_geometry geometry = {
        .base_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES,
        .total_fs_blocks = PUBLISH_TEST_BLOCKS,
        .bitmap_start = PUBLISH_TEST_BITMAP_START,
        .bitmap_blocks = 1u,
        .inode_start = PUBLISH_TEST_INODE_START,
        .inode_blocks = PUBLISH_TEST_INODE_BLOCKS,
        .data_start = PUBLISH_TEST_DATA_START
    };
    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) {
        return false;
    }

    for (uint64_t block = 0u; block < PUBLISH_TEST_DATA_START; ++block) {
        bitmap_set(block);
    }
    bitmap_set(PUBLISH_TEST_OLD_ROOT);
    bitmap_set(PUBLISH_TEST_NEW_ROOT);
    bitmap_set(PUBLISH_TEST_DATA_BLOCK);

    struct publish_inode inode;
    zero_bytes(&inode, sizeof(inode));
    inode.object_id = 2u;
    inode.parent_object_id = 1u;
    inode.size = AURORA_FS_V2_FS_BLOCK_SIZE;
    inode.allocated_bytes = AURORA_FS_V2_FS_BLOCK_SIZE;
    inode.generation = 10u;
    inode.extent_tree_root = PUBLISH_TEST_OLD_ROOT;
    inode.type = PUBLISH_INODE_FILE;
    inode.extent_count = 1u;
    if (!write_inode(&device, &geometry, 1u, &inode)) {
        return false;
    }

    struct aurora_fs_v2_extent next = {
        .logical_block = 1u,
        .physical_block = PUBLISH_TEST_DATA_BLOCK,
        .block_count = 1u
    };
    if (!aurora_fs_v2_inode_publish_extent_root_cow(
            &allocator, &geometry, 1u, PUBLISH_TEST_OLD_ROOT,
            PUBLISH_TEST_NEW_ROOT, &next)) {
        return false;
    }

    struct aurora_fs_v2_allocator reopened;
    struct publish_inode persisted;
    return aurora_fs_v2_allocator_init(
               &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
               geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start) &&
        read_inode(&device, &geometry, 1u, &persisted) &&
        persisted.extent_tree_root == PUBLISH_TEST_NEW_ROOT &&
        persisted.extent_count == 2u &&
        persisted.size == 2u * AURORA_FS_V2_FS_BLOCK_SIZE &&
        persisted.allocated_bytes == 2u * AURORA_FS_V2_FS_BLOCK_SIZE &&
        persisted.generation == 11u;
}

bool aurora_fs_v2_inode_publish_extent_root_self_test(void) {
    return run_test(512u) && run_test(4096u);
}
