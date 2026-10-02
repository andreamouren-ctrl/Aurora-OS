#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define V2_FORMAT_MAGIC_0 'A'
#define V2_FORMAT_MAGIC_1 'U'
#define V2_FORMAT_MAGIC_2 'R'
#define V2_FORMAT_MAGIC_3 'A'
#define V2_FORMAT_MAGIC_4 'F'
#define V2_FORMAT_MAGIC_5 'S'
#define V2_FORMAT_MAGIC_6 '2'
#define V2_FORMAT_MAGIC_7 '\0'
#define V2_FORMAT_VERSION 2u
#define V2_FORMAT_MIN_INODE_BLOCKS 4u
#define V2_FORMAT_BITS_PER_BITMAP_BLOCK ((uint64_t)AURORA_FS_V2_FS_BLOCK_SIZE * 8u)
#define V2_FORMAT_TEST_TOTAL_BLOCKS 70000u
#define V2_FORMAT_TEST_BITMAP_BLOCKS 3u
#define V2_FORMAT_TEST_INITIAL_DATA_BLOCKS 3u

struct aurora_fs_v2_superblock_disk {
    uint8_t magic[8];
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t generation;
    uint64_t bitmap_start;
    uint64_t bitmap_blocks;
    uint64_t inode_start;
    uint64_t inode_blocks;
    uint64_t data_start;
    uint64_t root_object_id;
    uint64_t next_object_id;
    uint64_t feature_flags;
    uint32_t metadata_checksum;
    uint8_t reserved[3996];
} __attribute__((packed));

struct formatter_test_context {
    uint32_t device_block_size;
    uint64_t superblock_byte_start;
    uint64_t bitmap_byte_start;
    uint64_t bitmap_byte_length;
    uint8_t superblock[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint8_t bitmap[V2_FORMAT_TEST_BITMAP_BLOCKS][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static uint8_t format_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static struct formatter_test_context formatter_test_context;

_Static_assert(sizeof(struct aurora_fs_v2_superblock_disk) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 formatter superblock must be 4 KiB");

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

static uint64_t ceil_div_u64(uint64_t value, uint64_t divisor) {
    return value / divisor + ((value % divisor) != 0u ? 1u : 0u);
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

static uint32_t superblock_checksum(struct aurora_fs_v2_superblock_disk *superblock) {
    uint32_t saved = superblock->metadata_checksum;
    superblock->metadata_checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)superblock, sizeof(*superblock));
    superblock->metadata_checksum = saved;
    return checksum;
}

static bool set_superblock_magic(struct aurora_fs_v2_superblock_disk *superblock) {
    if (superblock == NULL) {
        return false;
    }
    static const uint8_t magic[8] = {
        V2_FORMAT_MAGIC_0, V2_FORMAT_MAGIC_1, V2_FORMAT_MAGIC_2, V2_FORMAT_MAGIC_3,
        V2_FORMAT_MAGIC_4, V2_FORMAT_MAGIC_5, V2_FORMAT_MAGIC_6, V2_FORMAT_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        superblock->magic[i] = magic[i];
    }
    return true;
}

static bool superblock_magic_valid(const struct aurora_fs_v2_superblock_disk *superblock) {
    static const uint8_t magic[8] = {
        V2_FORMAT_MAGIC_0, V2_FORMAT_MAGIC_1, V2_FORMAT_MAGIC_2, V2_FORMAT_MAGIC_3,
        V2_FORMAT_MAGIC_4, V2_FORMAT_MAGIC_5, V2_FORMAT_MAGIC_6, V2_FORMAT_MAGIC_7
    };
    if (superblock == NULL) {
        return false;
    }
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        if (superblock->magic[i] != magic[i]) {
            return false;
        }
    }
    return true;
}

static bool fs_block_io_geometry(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_device_blocks
) {
    if (device == NULL || out_lba == NULL || out_device_blocks == NULL ||
        device->block_size == 0u || device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) != 0u ||
        (base_bytes % device->block_size) != 0u) {
        return false;
    }

    uint64_t fs_offset;
    uint64_t byte_offset;
    if (!mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_offset) ||
        !add_u64(base_bytes, fs_offset, &byte_offset)) {
        return false;
    }

    uint64_t device_blocks = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    if (device_blocks == 0u || device_blocks > UINT32_MAX) {
        return false;
    }

    uint64_t lba = byte_offset / device->block_size;
    if (lba >= device->block_count || device_blocks > device->block_count - lba) {
        return false;
    }

    *out_lba = lba;
    *out_device_blocks = (uint32_t)device_blocks;
    return true;
}

static bool write_fs_block(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t fs_block,
    const void *buffer
) {
    uint64_t lba;
    uint32_t device_blocks;
    return buffer != NULL && !device->read_only &&
        fs_block_io_geometry(device, base_bytes, fs_block, &lba, &device_blocks) &&
        block_device_write(device, lba, device_blocks, buffer);
}

static bool read_fs_block(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t fs_block,
    void *buffer
) {
    uint64_t lba;
    uint32_t device_blocks;
    return buffer != NULL &&
        fs_block_io_geometry(device, base_bytes, fs_block, &lba, &device_blocks) &&
        block_device_read(device, lba, device_blocks, buffer);
}

static void bitmap_set_local(uint8_t *bitmap, uint64_t local_bit) {
    bitmap[local_bit >> 3] |= (uint8_t)(1u << (local_bit & 7u));
}

bool aurora_fs_v2_format_device(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t initial_data_blocks,
    struct aurora_fs_v2_format_geometry *out_geometry
) {
    if (device == NULL || device->read_only || out_geometry == NULL ||
        device->block_size == 0u ||
        device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) != 0u ||
        (base_bytes % device->block_size) != 0u) {
        return false;
    }

    uint64_t device_bytes;
    if (!mul_u64(device->block_count, device->block_size, &device_bytes) ||
        device_bytes <= base_bytes + AURORA_FS_V2_FS_BLOCK_SIZE * 8u) {
        return false;
    }

    uint64_t total_fs_blocks =
        (device_bytes - base_bytes) / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t bitmap_bytes = ceil_div_u64(total_fs_blocks, 8u);
    uint64_t bitmap_blocks =
        ceil_div_u64(bitmap_bytes, AURORA_FS_V2_FS_BLOCK_SIZE);
    uint64_t inode_blocks = total_fs_blocks / 128u;
    if (inode_blocks < V2_FORMAT_MIN_INODE_BLOCKS) {
        inode_blocks = V2_FORMAT_MIN_INODE_BLOCKS;
    }

    uint64_t bitmap_start = 1u;
    uint64_t inode_start;
    uint64_t data_start;
    if (!add_u64(bitmap_start, bitmap_blocks, &inode_start) ||
        !add_u64(inode_start, inode_blocks, &data_start) ||
        data_start >= total_fs_blocks ||
        initial_data_blocks > total_fs_blocks - data_start) {
        return false;
    }

    struct aurora_fs_v2_superblock_disk superblock;
    zero_bytes(&superblock, sizeof(superblock));
    if (!set_superblock_magic(&superblock)) {
        return false;
    }
    superblock.version = V2_FORMAT_VERSION;
    superblock.block_size = AURORA_FS_V2_FS_BLOCK_SIZE;
    superblock.total_blocks = total_fs_blocks;
    superblock.generation = 1u;
    superblock.bitmap_start = bitmap_start;
    superblock.bitmap_blocks = bitmap_blocks;
    superblock.inode_start = inode_start;
    superblock.inode_blocks = inode_blocks;
    superblock.data_start = data_start;
    superblock.root_object_id = 1u;
    superblock.next_object_id = 2u;
    superblock.feature_flags = 0u;
    superblock.metadata_checksum = superblock_checksum(&superblock);

    if (!write_fs_block(device, base_bytes, 0u, &superblock)) {
        return false;
    }

    uint64_t reserved_end = data_start + initial_data_blocks;
    for (uint64_t bitmap_index = 0u; bitmap_index < bitmap_blocks; ++bitmap_index) {
        zero_bytes(format_block, sizeof(format_block));
        uint64_t block_base = bitmap_index * V2_FORMAT_BITS_PER_BITMAP_BLOCK;
        uint64_t local_limit = V2_FORMAT_BITS_PER_BITMAP_BLOCK;
        if (block_base >= total_fs_blocks) {
            local_limit = 0u;
        } else if (local_limit > total_fs_blocks - block_base) {
            local_limit = total_fs_blocks - block_base;
        }

        for (uint64_t local_bit = 0u; local_bit < local_limit; ++local_bit) {
            uint64_t fs_block = block_base + local_bit;
            if (fs_block < reserved_end) {
                bitmap_set_local(format_block, local_bit);
            }
        }
        if (!write_fs_block(
                device,
                base_bytes,
                bitmap_start + bitmap_index,
                format_block)) {
            return false;
        }
    }

    zero_bytes(format_block, sizeof(format_block));
    for (uint64_t inode_index = 0u; inode_index < inode_blocks; ++inode_index) {
        if (!write_fs_block(device, base_bytes, inode_start + inode_index, format_block)) {
            return false;
        }
    }

    if (!block_device_flush(device)) {
        return false;
    }

    out_geometry->base_bytes = base_bytes;
    out_geometry->total_fs_blocks = total_fs_blocks;
    out_geometry->bitmap_start = bitmap_start;
    out_geometry->bitmap_blocks = bitmap_blocks;
    out_geometry->inode_start = inode_start;
    out_geometry->inode_blocks = inode_blocks;
    out_geometry->data_start = data_start;
    return true;
}

static bool test_sparse_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }

    struct formatter_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length)) {
        return false;
    }

    uint8_t *out = buffer;
    zero_bytes(out, (size_t)length);
    uint64_t request_end = offset + length;

    uint64_t superblock_end = context->superblock_byte_start + AURORA_FS_V2_FS_BLOCK_SIZE;
    if (request_end > context->superblock_byte_start && offset < superblock_end) {
        uint64_t copy_start = offset > context->superblock_byte_start ?
            offset : context->superblock_byte_start;
        uint64_t copy_end = request_end < superblock_end ? request_end : superblock_end;
        uint64_t source_offset = copy_start - context->superblock_byte_start;
        uint64_t dest_offset = copy_start - offset;
        for (uint64_t i = 0u; i < copy_end - copy_start; ++i) {
            out[dest_offset + i] = context->superblock[source_offset + i];
        }
    }

    uint64_t bitmap_end = context->bitmap_byte_start + context->bitmap_byte_length;
    if (request_end > context->bitmap_byte_start && offset < bitmap_end) {
        uint64_t copy_start = offset > context->bitmap_byte_start ?
            offset : context->bitmap_byte_start;
        uint64_t copy_end = request_end < bitmap_end ? request_end : bitmap_end;
        uint64_t source_offset = copy_start - context->bitmap_byte_start;
        uint64_t dest_offset = copy_start - offset;
        const uint8_t *source = &context->bitmap[0][0];
        for (uint64_t i = 0u; i < copy_end - copy_start; ++i) {
            out[dest_offset + i] = source[source_offset + i];
        }
    }
    return true;
}

static bool test_sparse_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    if (device == NULL || buffer == NULL || device->read_only || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }

    struct formatter_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length)) {
        return false;
    }
    uint64_t request_end = offset + length;
    const uint8_t *source = buffer;

    uint64_t superblock_end = context->superblock_byte_start + AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset >= context->superblock_byte_start && request_end <= superblock_end) {
        uint64_t dest_offset = offset - context->superblock_byte_start;
        for (uint64_t i = 0u; i < length; ++i) {
            context->superblock[dest_offset + i] = source[i];
        }
        return true;
    }

    uint64_t bitmap_end = context->bitmap_byte_start + context->bitmap_byte_length;
    if (offset >= context->bitmap_byte_start && request_end <= bitmap_end) {
        uint64_t dest_offset = offset - context->bitmap_byte_start;
        uint8_t *dest = &context->bitmap[0][0];
        for (uint64_t i = 0u; i < length; ++i) {
            dest[dest_offset + i] = source[i];
        }
        return true;
    }

    /* Inode-table zeroing and other formatter initialization writes are accepted
       without backing storage in this sparse geometry test. */
    for (uint64_t i = 0u; i < length; ++i) {
        if (source[i] != 0u) {
            return false;
        }
    }
    return true;
}

static bool test_sparse_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool run_formatter_geometry(uint32_t device_block_size) {
    zero_bytes(&formatter_test_context, sizeof(formatter_test_context));
    formatter_test_context.device_block_size = device_block_size;
    formatter_test_context.superblock_byte_start = AURORA_FS_V2_DEFAULT_BASE_BYTES;
    formatter_test_context.bitmap_byte_start =
        AURORA_FS_V2_DEFAULT_BASE_BYTES + AURORA_FS_V2_FS_BLOCK_SIZE;
    formatter_test_context.bitmap_byte_length =
        V2_FORMAT_TEST_BITMAP_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;

    uint64_t virtual_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        V2_FORMAT_TEST_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct aurora_block_device device = {
        .name = "aurorafs-v2-formatter-test",
        .block_size = device_block_size,
        .block_count = virtual_bytes / device_block_size,
        .read_only = false,
        .context = &formatter_test_context,
        .read_blocks = test_sparse_read,
        .write_blocks = test_sparse_write,
        .flush = test_sparse_flush
    };

    struct aurora_fs_v2_format_geometry geometry;
    if (!aurora_fs_v2_format_device(
            &device,
            AURORA_FS_V2_DEFAULT_BASE_BYTES,
            V2_FORMAT_TEST_INITIAL_DATA_BLOCKS,
            &geometry) ||
        geometry.total_fs_blocks != V2_FORMAT_TEST_TOTAL_BLOCKS ||
        geometry.bitmap_blocks != V2_FORMAT_TEST_BITMAP_BLOCKS ||
        geometry.bitmap_start != 1u || geometry.inode_start != 4u ||
        geometry.data_start <= geometry.inode_start) {
        return false;
    }

    struct aurora_fs_v2_superblock_disk reopened;
    if (!read_fs_block(&device, geometry.base_bytes, 0u, &reopened) ||
        !superblock_magic_valid(&reopened) || reopened.version != V2_FORMAT_VERSION ||
        reopened.block_size != AURORA_FS_V2_FS_BLOCK_SIZE ||
        reopened.total_blocks != geometry.total_fs_blocks ||
        reopened.bitmap_blocks != geometry.bitmap_blocks ||
        reopened.inode_start != geometry.inode_start ||
        reopened.data_start != geometry.data_start ||
        reopened.metadata_checksum != superblock_checksum(&reopened)) {
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
            geometry.data_start)) {
        return false;
    }

    uint64_t reserved_end = geometry.data_start + V2_FORMAT_TEST_INITIAL_DATA_BLOCKS;
    bool allocated = false;
    if (!aurora_fs_v2_allocator_is_allocated(&allocator, 0u, &allocated) || !allocated ||
        !aurora_fs_v2_allocator_is_allocated(&allocator, geometry.data_start, &allocated) ||
        !allocated ||
        !aurora_fs_v2_allocator_is_allocated(&allocator, reserved_end, &allocated) ||
        allocated) {
        return false;
    }

    uint64_t first = 0u;
    if (!aurora_fs_v2_allocator_allocate_range(&allocator, 5u, &first) ||
        first != reserved_end) {
        return false;
    }
    for (uint64_t i = 0u; i < 5u; ++i) {
        if (!aurora_fs_v2_allocator_is_allocated(&allocator, first + i, &allocated) ||
            !allocated) {
            return false;
        }
    }
    return true;
}

bool aurora_fs_v2_formatter_self_test(void) {
    return run_formatter_geometry(512u) && run_formatter_geometry(4096u);
}
