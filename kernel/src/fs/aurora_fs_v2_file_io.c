#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_file_io.h>
#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/block_device.h>

#define V2F_INODE_SIZE 256u
#define V2F_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2F_INODE_SIZE)
#define V2F_TEST_STORAGE_BYTES (2u * 1024u * 1024u)
#define V2F_TEST_PAYLOAD_BYTES 6000u
#define V2F_TEST_GROWN_BYTES 7000u

struct v2f_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2f_inode_disk {
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
    struct v2f_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct v2f_test_context {
    uint8_t *storage;
    uint64_t storage_bytes;
};

static uint8_t v2f_io_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2f_test_storage[V2F_TEST_STORAGE_BYTES];
static uint8_t v2f_test_payload[V2F_TEST_PAYLOAD_BYTES];
static uint8_t v2f_test_readback[V2F_TEST_GROWN_BYTES];

_Static_assert(sizeof(struct v2f_inode_disk) == V2F_INODE_SIZE,
               "AuroraFS v2 file inode layout drifted");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
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

static bool fs_block_geometry(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_count
) {
    if (!geometry_valid(device, geometry) || out_lba == NULL || out_count == NULL ||
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
        block_device_write(device, lba, count, buffer);
}

static bool inode_location(
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t *out_block,
    uint32_t *out_slot
) {
    if (geometry == NULL || out_block == NULL || out_slot == NULL ||
        inode_index >= geometry->inode_blocks * V2F_INODES_PER_BLOCK) return false;
    *out_block = geometry->inode_start + inode_index / V2F_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(inode_index % V2F_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct v2f_inode_disk *out_inode
) {
    uint64_t block;
    uint32_t slot;
    if (out_inode == NULL || !inode_location(geometry, inode_index, &block, &slot) ||
        !read_fs_block(device, geometry, block, v2f_io_block)) return false;
    const struct v2f_inode_disk *inodes = (const struct v2f_inode_disk *)v2f_io_block;
    *out_inode = inodes[slot];
    return true;
}

static bool write_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct v2f_inode_disk *inode
) {
    uint64_t block;
    uint32_t slot;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_location(geometry, inode_index, &block, &slot) ||
        !read_fs_block(device, geometry, block, v2f_io_block)) return false;
    struct v2f_inode_disk *inodes = (struct v2f_inode_disk *)v2f_io_block;
    inodes[slot] = *inode;
    return write_fs_block(device, geometry, block, v2f_io_block) &&
        block_device_flush(device);
}

static bool file_inode_valid(const struct v2f_inode_disk *inode) {
    return inode != NULL && inode->object_id != 0u &&
        inode->type == AURORA_FS_V2_OBJECT_FILE &&
        (inode->allocated_bytes % AURORA_FS_V2_FS_BLOCK_SIZE) == 0u;
}

static uint64_t ceil_blocks(uint64_t size) {
    return size / AURORA_FS_V2_FS_BLOCK_SIZE +
        ((size % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u ? 1u : 0u);
}

static bool set_exact_size(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t size
) {
    struct v2f_inode_disk inode;
    if (!read_inode(device, geometry, inode_index, &inode) || !file_inode_valid(&inode))
        return false;
    inode.size = size;
    inode.generation++;
    return write_inode(device, geometry, inode_index, &inode);
}

static bool ensure_capacity(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t required_size
) {
    struct v2f_inode_disk inode;
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        !read_inode(allocator->device, geometry, inode_index, &inode) ||
        !file_inode_valid(&inode)) return false;

    uint64_t old_size = inode.size;
    uint64_t current_blocks = inode.allocated_bytes / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t required_blocks = ceil_blocks(required_size);
    if (required_blocks <= current_blocks) return true;

    uint64_t add_blocks = required_blocks - current_blocks;
    uint64_t first_block;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, add_blocks, &first_block)) return false;

    zero_bytes(v2f_io_block, sizeof(v2f_io_block));
    for (uint64_t i = 0u; i < add_blocks; ++i) {
        if (!write_fs_block(allocator->device, geometry, first_block + i, v2f_io_block)) {
            aurora_fs_v2_allocator_free_range(allocator, first_block, add_blocks);
            return false;
        }
    }
    if (!block_device_flush(allocator->device)) {
        aurora_fs_v2_allocator_free_range(allocator, first_block, add_blocks);
        return false;
    }

    struct aurora_fs_v2_extent extent = {
        .logical_block = current_blocks,
        .physical_block = first_block,
        .block_count = add_blocks
    };
    if (!aurora_fs_v2_inode_extent_append(allocator, geometry, inode_index, &extent)) {
        aurora_fs_v2_allocator_free_range(allocator, first_block, add_blocks);
        return false;
    }

    /* extent append publishes block-rounded capacity; the file layer owns exact EOF. */
    return set_exact_size(allocator->device, geometry, inode_index, old_size);
}

static bool resolve_file_block(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t logical_block,
    uint64_t *out_physical
) {
    if (out_physical == NULL) return false;

    struct v2f_inode_disk inode;
    if (allocator == NULL || allocator->device == NULL ||
        !read_inode(allocator->device, geometry, inode_index, &inode) ||
        !file_inode_valid(&inode)) return false;

    uint64_t contiguous;
    if (inode.extent_tree_root == 0u) {
        return aurora_fs_v2_inode_extent_lookup(
            allocator, geometry, inode_index, logical_block, out_physical, &contiguous);
    }

    return aurora_fs_v2_inode_extent_lookup_unified(
        allocator, geometry, inode_index, logical_block, out_physical, &contiguous);
}

static bool write_range(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t offset,
    const uint8_t *source,
    uint64_t length,
    bool zero_source
) {
    uint64_t done = 0u;
    while (done < length) {
        uint64_t absolute = offset + done;
        uint64_t logical = absolute / AURORA_FS_V2_FS_BLOCK_SIZE;
        size_t within = (size_t)(absolute % AURORA_FS_V2_FS_BLOCK_SIZE);
        size_t take = AURORA_FS_V2_FS_BLOCK_SIZE - within;
        if ((uint64_t)take > length - done) take = (size_t)(length - done);

        uint64_t physical;
        if (!resolve_file_block(allocator, geometry, inode_index, logical, &physical) ||
            !read_fs_block(allocator->device, geometry, physical, v2f_io_block)) return false;

        for (size_t i = 0u; i < take; ++i)
            v2f_io_block[within + i] = zero_source ? 0u : source[done + i];

        if (!write_fs_block(allocator->device, geometry, physical, v2f_io_block)) return false;
        done += take;
    }
    return block_device_flush(allocator->device);
}

bool aurora_fs_v2_file_read(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t offset,
    void *buffer,
    size_t length,
    size_t *out_read
) {
    if (out_read != NULL) *out_read = 0u;
    if (allocator == NULL || allocator->device == NULL || buffer == NULL) return false;

    struct v2f_inode_disk inode;
    if (!read_inode(allocator->device, geometry, inode_index, &inode) ||
        !file_inode_valid(&inode)) return false;
    if (offset >= inode.size || length == 0u) return true;

    uint64_t request = length;
    uint64_t available = inode.size - offset;
    if (request > available) request = available;

    uint8_t *destination = buffer;
    uint64_t done = 0u;
    while (done < request) {
        uint64_t absolute = offset + done;
        uint64_t logical = absolute / AURORA_FS_V2_FS_BLOCK_SIZE;
        size_t within = (size_t)(absolute % AURORA_FS_V2_FS_BLOCK_SIZE);
        size_t take = AURORA_FS_V2_FS_BLOCK_SIZE - within;
        if ((uint64_t)take > request - done) take = (size_t)(request - done);

        uint64_t physical;
        if (!resolve_file_block(allocator, geometry, inode_index, logical, &physical) ||
            !read_fs_block(allocator->device, geometry, physical, v2f_io_block)) return false;
        for (size_t i = 0u; i < take; ++i) destination[done + i] = v2f_io_block[within + i];
        done += take;
    }

    if (out_read != NULL) *out_read = (size_t)done;
    return true;
}

bool aurora_fs_v2_file_write(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t offset,
    const void *buffer,
    size_t length,
    size_t *out_written
) {
    if (out_written != NULL) *out_written = 0u;
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        (buffer == NULL && length != 0u)) return false;

    struct v2f_inode_disk inode;
    if (!read_inode(allocator->device, geometry, inode_index, &inode) ||
        !file_inode_valid(&inode)) return false;
    if (length == 0u) return true;

    uint64_t old_size = inode.size;
    uint64_t end;
    if (!add_u64(offset, (uint64_t)length, &end) ||
        !ensure_capacity(allocator, geometry, inode_index, end)) return false;

    if (offset > old_size &&
        !write_range(allocator, geometry, inode_index, old_size, NULL,
                     offset - old_size, true)) return false;
    if (!write_range(allocator, geometry, inode_index, offset, buffer, length, false)) return false;

    uint64_t final_size = old_size > end ? old_size : end;
    if (!set_exact_size(allocator->device, geometry, inode_index, final_size)) return false;
    if (out_written != NULL) *out_written = length;
    return true;
}

bool aurora_fs_v2_file_truncate(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t new_size
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only) return false;

    struct v2f_inode_disk inode;
    if (!read_inode(allocator->device, geometry, inode_index, &inode) ||
        !file_inode_valid(&inode)) return false;
    uint64_t old_size = inode.size;
    if (new_size == old_size) return true;

    if (new_size > old_size) {
        if (!ensure_capacity(allocator, geometry, inode_index, new_size) ||
            !write_range(allocator, geometry, inode_index, old_size, NULL,
                         new_size - old_size, true)) return false;
    } else if (old_size > new_size) {
        /* Blocks remain allocated until the dedicated reclaim milestone. */
        if (!write_range(allocator, geometry, inode_index, new_size, NULL,
                         old_size - new_size, true)) return false;
    }

    return set_exact_size(allocator->device, geometry, inode_index, new_size);
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
    struct v2f_test_context *context = device->context;
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

static bool test_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    if (device == NULL || buffer == NULL || device->read_only || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba)
        return false;
    struct v2f_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset)
        return false;
    const uint8_t *source = buffer;
    for (uint64_t i = 0u; i < length; ++i) context->storage[offset + i] = source[i];
    return true;
}

static bool test_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool run_geometry(uint32_t device_block_size) {
    zero_bytes(v2f_test_storage, sizeof(v2f_test_storage));
    struct v2f_test_context context = {
        .storage = v2f_test_storage,
        .storage_bytes = sizeof(v2f_test_storage)
    };
    struct aurora_block_device device = {
        .name = "aurorafs-v2-file-io-test",
        .block_size = device_block_size,
        .block_count = sizeof(v2f_test_storage) / device_block_size,
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

    if (!aurora_fs_v2_object_init(
            &device, &geometry, 0u, 1u, 1u, AURORA_FS_V2_OBJECT_DIRECTORY) ||
        !aurora_fs_v2_create_child(
            &allocator, &geometry, 0u, 1u, 2u, AURORA_FS_V2_OBJECT_FILE, "payload.bin"))
        return false;

    for (size_t i = 0u; i < sizeof(v2f_test_payload); ++i)
        v2f_test_payload[i] = (uint8_t)((i * 37u + 11u) & 0xFFu);

    size_t written = 0u;
    if (!aurora_fs_v2_file_write(
            &allocator, &geometry, 1u, 0u, v2f_test_payload,
            sizeof(v2f_test_payload), &written) ||
        written != sizeof(v2f_test_payload)) return false;

    size_t read = 0u;
    zero_bytes(v2f_test_readback, sizeof(v2f_test_readback));
    if (!aurora_fs_v2_file_read(
            &allocator, &geometry, 1u, 0u, v2f_test_readback,
            sizeof(v2f_test_payload), &read) ||
        read != sizeof(v2f_test_payload)) return false;
    for (size_t i = 0u; i < sizeof(v2f_test_payload); ++i)
        if (v2f_test_readback[i] != v2f_test_payload[i]) return false;

    if (!aurora_fs_v2_file_truncate(&allocator, &geometry, 1u, 3000u) ||
        !aurora_fs_v2_file_truncate(&allocator, &geometry, 1u, V2F_TEST_GROWN_BYTES))
        return false;

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) return false;

    zero_bytes(v2f_test_readback, sizeof(v2f_test_readback));
    read = 0u;
    if (!aurora_fs_v2_file_read(
            &reopened, &geometry, 1u, 0u, v2f_test_readback,
            sizeof(v2f_test_readback), &read) ||
        read != sizeof(v2f_test_readback)) return false;

    for (size_t i = 0u; i < 3000u; ++i)
        if (v2f_test_readback[i] != v2f_test_payload[i]) return false;
    for (size_t i = 3000u; i < sizeof(v2f_test_readback); ++i)
        if (v2f_test_readback[i] != 0u) return false;

    static const uint8_t tail[] = { 0xA5u, 0x5Au, 0x11u, 0x22u, 0x33u };
    if (!aurora_fs_v2_file_write(
            &reopened, &geometry, 1u, 6995u, tail, sizeof(tail), &written) ||
        written != sizeof(tail)) return false;

    uint8_t tail_read[sizeof(tail)];
    read = 0u;
    if (!aurora_fs_v2_file_read(
            &reopened, &geometry, 1u, 6995u, tail_read, sizeof(tail_read), &read) ||
        read != sizeof(tail_read)) return false;
    for (size_t i = 0u; i < sizeof(tail); ++i)
        if (tail_read[i] != tail[i]) return false;

    return true;
}

bool aurora_fs_v2_file_io_self_test(void) {
    return run_geometry(512u) && run_geometry(4096u);
}
