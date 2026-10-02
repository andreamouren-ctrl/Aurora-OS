#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs.h>
#include <aurora/block_device.h>

#define AURORA_FS_V2_MAGIC_0 'A'
#define AURORA_FS_V2_MAGIC_1 'U'
#define AURORA_FS_V2_MAGIC_2 'R'
#define AURORA_FS_V2_MAGIC_3 'A'
#define AURORA_FS_V2_MAGIC_4 'F'
#define AURORA_FS_V2_MAGIC_5 'S'
#define AURORA_FS_V2_MAGIC_6 '2'
#define AURORA_FS_V2_MAGIC_7 '\0'

#define AURORA_FS_V2_VERSION 2u
#define AURORA_FS_V2_BLOCK_SIZE 4096u
#define AURORA_FS_V2_BASE_BYTES 4096u
#define AURORA_FS_V2_INODE_SIZE 256u
#define AURORA_FS_V2_MIN_INODE_BLOCKS 4u
#define AURORA_FS_V2_INLINE_EXTENTS 4u
#define AURORA_FS_V2_INODE_FILE 1u
#define AURORA_FS_V2_INODE_DIRECTORY 2u
#define AURORA_FS_V2_TEST_BYTES 6000u
#define AURORA_FS_V2_TEST_DEVICE_BYTES (1024u * 1024u)

struct aurora_fs_v2_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct aurora_fs_v2_inode_disk {
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
    struct aurora_fs_v2_extent_disk extents[AURORA_FS_V2_INLINE_EXTENTS];
    uint8_t reserved1[96];
} __attribute__((packed));

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

struct aurora_fs_v2_test_context {
    uint8_t *storage;
    uint32_t block_size;
    uint64_t block_count;
};

static uint8_t test_storage[AURORA_FS_V2_TEST_DEVICE_BYTES];
static uint8_t bitmap_block[AURORA_FS_V2_BLOCK_SIZE];
static uint8_t inode_block[AURORA_FS_V2_BLOCK_SIZE];
static uint8_t data_block[AURORA_FS_V2_BLOCK_SIZE];

_Static_assert(sizeof(struct aurora_fs_v2_extent_disk) == 24u,
               "AuroraFS v2 extent must be 24 bytes");
_Static_assert(sizeof(struct aurora_fs_v2_inode_disk) == AURORA_FS_V2_INODE_SIZE,
               "AuroraFS v2 inode must be 256 bytes");
_Static_assert(sizeof(struct aurora_fs_v2_superblock_disk) == AURORA_FS_V2_BLOCK_SIZE,
               "AuroraFS v2 superblock must fill one filesystem block");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) {
        bytes[i] = 0u;
    }
}

static bool mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || (a != 0u && b > UINT64_MAX / a)) {
        return false;
    }
    *out = a * b;
    return true;
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

static bool test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count ||
        (uint64_t)block_count > device->block_count - lba) {
        return false;
    }

    struct aurora_fs_v2_test_context *context = device->context;
    if (context == NULL || context->block_size != device->block_size) {
        return false;
    }

    uint64_t offset;
    uint64_t length;
    if (!mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > AURORA_FS_V2_TEST_DEVICE_BYTES ||
        length > AURORA_FS_V2_TEST_DEVICE_BYTES - offset) {
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
    if (device == NULL || buffer == NULL || device->read_only ||
        block_count == 0u || lba >= device->block_count ||
        (uint64_t)block_count > device->block_count - lba) {
        return false;
    }

    struct aurora_fs_v2_test_context *context = device->context;
    if (context == NULL || context->block_size != device->block_size) {
        return false;
    }

    uint64_t offset;
    uint64_t length;
    if (!mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > AURORA_FS_V2_TEST_DEVICE_BYTES ||
        length > AURORA_FS_V2_TEST_DEVICE_BYTES - offset) {
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

static bool read_bytes(
    struct aurora_block_device *device,
    uint64_t offset,
    void *buffer,
    size_t length
) {
    if (device == NULL || buffer == NULL || device->block_size == 0u ||
        device->block_size > AURORA_FS_V2_BLOCK_SIZE) {
        return false;
    }

    uint8_t scratch[AURORA_FS_V2_BLOCK_SIZE];
    uint8_t *out = buffer;
    size_t done = 0u;
    while (done < length) {
        uint64_t absolute = offset + done;
        uint64_t lba = absolute / device->block_size;
        size_t within = (size_t)(absolute % device->block_size);
        if (!block_device_read(device, lba, 1u, scratch)) {
            return false;
        }
        size_t take = device->block_size - within;
        if (take > length - done) {
            take = length - done;
        }
        for (size_t i = 0u; i < take; ++i) {
            out[done + i] = scratch[within + i];
        }
        done += take;
    }
    return true;
}

static bool write_bytes(
    struct aurora_block_device *device,
    uint64_t offset,
    const void *buffer,
    size_t length
) {
    if (device == NULL || buffer == NULL || device->read_only ||
        device->block_size == 0u || device->block_size > AURORA_FS_V2_BLOCK_SIZE) {
        return false;
    }

    uint8_t scratch[AURORA_FS_V2_BLOCK_SIZE];
    const uint8_t *source = buffer;
    size_t done = 0u;
    while (done < length) {
        uint64_t absolute = offset + done;
        uint64_t lba = absolute / device->block_size;
        size_t within = (size_t)(absolute % device->block_size);
        size_t take = device->block_size - within;
        if (take > length - done) {
            take = length - done;
        }
        if (within != 0u || take != device->block_size) {
            if (!block_device_read(device, lba, 1u, scratch)) {
                return false;
            }
        }
        for (size_t i = 0u; i < take; ++i) {
            scratch[within + i] = source[done + i];
        }
        if (!block_device_write(device, lba, 1u, scratch)) {
            return false;
        }
        done += take;
    }
    return true;
}

static uint64_t fs_block_offset(uint64_t block) {
    return AURORA_FS_V2_BASE_BYTES + block * AURORA_FS_V2_BLOCK_SIZE;
}

static bool read_fs_block(
    struct aurora_block_device *device,
    uint64_t block,
    void *buffer
) {
    return read_bytes(device, fs_block_offset(block), buffer, AURORA_FS_V2_BLOCK_SIZE);
}

static bool write_fs_block(
    struct aurora_block_device *device,
    uint64_t block,
    const void *buffer
) {
    return write_bytes(device, fs_block_offset(block), buffer, AURORA_FS_V2_BLOCK_SIZE);
}

static void bitmap_set(uint8_t *bitmap, uint64_t block) {
    bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
}

static bool bitmap_test(const uint8_t *bitmap, uint64_t block) {
    return (bitmap[block >> 3] & (uint8_t)(1u << (block & 7u))) != 0u;
}

static uint64_t ceil_div_u64(uint64_t value, uint64_t divisor) {
    return value / divisor + ((value % divisor) != 0u ? 1u : 0u);
}

static bool set_magic(struct aurora_fs_v2_superblock_disk *superblock) {
    static const uint8_t magic[8] = {
        AURORA_FS_V2_MAGIC_0, AURORA_FS_V2_MAGIC_1,
        AURORA_FS_V2_MAGIC_2, AURORA_FS_V2_MAGIC_3,
        AURORA_FS_V2_MAGIC_4, AURORA_FS_V2_MAGIC_5,
        AURORA_FS_V2_MAGIC_6, AURORA_FS_V2_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        superblock->magic[i] = magic[i];
    }
    return true;
}

static bool magic_valid(const struct aurora_fs_v2_superblock_disk *superblock) {
    static const uint8_t magic[8] = {
        AURORA_FS_V2_MAGIC_0, AURORA_FS_V2_MAGIC_1,
        AURORA_FS_V2_MAGIC_2, AURORA_FS_V2_MAGIC_3,
        AURORA_FS_V2_MAGIC_4, AURORA_FS_V2_MAGIC_5,
        AURORA_FS_V2_MAGIC_6, AURORA_FS_V2_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        if (superblock->magic[i] != magic[i]) {
            return false;
        }
    }
    return true;
}

static bool format_v2(
    struct aurora_block_device *device,
    struct aurora_fs_v2_superblock_disk *out_superblock
) {
    uint64_t device_bytes;
    if (device == NULL || device->read_only || out_superblock == NULL ||
        !mul_u64(device->block_count, device->block_size, &device_bytes) ||
        device_bytes <= AURORA_FS_V2_BASE_BYTES + AURORA_FS_V2_BLOCK_SIZE * 8u) {
        return false;
    }

    uint64_t total_blocks =
        (device_bytes - AURORA_FS_V2_BASE_BYTES) / AURORA_FS_V2_BLOCK_SIZE;
    uint64_t bitmap_bytes = ceil_div_u64(total_blocks, 8u);
    uint64_t bitmap_blocks = ceil_div_u64(bitmap_bytes, AURORA_FS_V2_BLOCK_SIZE);
    uint64_t inode_blocks = total_blocks / 128u;
    if (inode_blocks < AURORA_FS_V2_MIN_INODE_BLOCKS) {
        inode_blocks = AURORA_FS_V2_MIN_INODE_BLOCKS;
    }

    uint64_t bitmap_start = 1u;
    uint64_t inode_start = bitmap_start + bitmap_blocks;
    uint64_t data_start = inode_start + inode_blocks;
    if (data_start + 2u >= total_blocks || bitmap_blocks != 1u) {
        return false;
    }

    struct aurora_fs_v2_superblock_disk superblock;
    zero_bytes(&superblock, sizeof(superblock));
    set_magic(&superblock);
    superblock.version = AURORA_FS_V2_VERSION;
    superblock.block_size = AURORA_FS_V2_BLOCK_SIZE;
    superblock.total_blocks = total_blocks;
    superblock.generation = 1u;
    superblock.bitmap_start = bitmap_start;
    superblock.bitmap_blocks = bitmap_blocks;
    superblock.inode_start = inode_start;
    superblock.inode_blocks = inode_blocks;
    superblock.data_start = data_start;
    superblock.root_object_id = 1u;
    superblock.next_object_id = 3u;
    superblock.feature_flags = 0u;
    superblock.metadata_checksum = superblock_checksum(&superblock);

    zero_bytes(bitmap_block, sizeof(bitmap_block));
    for (uint64_t block = 0u; block < data_start + 2u; ++block) {
        bitmap_set(bitmap_block, block);
    }

    zero_bytes(inode_block, sizeof(inode_block));
    struct aurora_fs_v2_inode_disk *inodes =
        (struct aurora_fs_v2_inode_disk *)inode_block;

    inodes[0].object_id = 1u;
    inodes[0].parent_object_id = 1u;
    inodes[0].generation = 1u;
    inodes[0].type = AURORA_FS_V2_INODE_DIRECTORY;

    inodes[1].object_id = 2u;
    inodes[1].parent_object_id = 1u;
    inodes[1].size = AURORA_FS_V2_TEST_BYTES;
    inodes[1].allocated_bytes = AURORA_FS_V2_BLOCK_SIZE * 2u;
    inodes[1].generation = 1u;
    inodes[1].type = AURORA_FS_V2_INODE_FILE;
    inodes[1].extent_count = 1u;
    inodes[1].extents[0].logical_block = 0u;
    inodes[1].extents[0].physical_block = data_start;
    inodes[1].extents[0].block_count = 2u;

    if (!write_fs_block(device, 0u, &superblock) ||
        !write_fs_block(device, bitmap_start, bitmap_block) ||
        !write_fs_block(device, inode_start, inode_block)) {
        return false;
    }

    for (uint64_t block = 0u; block < 2u; ++block) {
        zero_bytes(data_block, sizeof(data_block));
        for (uint32_t i = 0u; i < AURORA_FS_V2_BLOCK_SIZE; ++i) {
            uint64_t file_offset = block * AURORA_FS_V2_BLOCK_SIZE + i;
            if (file_offset >= AURORA_FS_V2_TEST_BYTES) {
                break;
            }
            data_block[i] = (uint8_t)((file_offset * 37u + 11u) & 0xFFu);
        }
        if (!write_fs_block(device, data_start + block, data_block)) {
            return false;
        }
    }

    if (!block_device_flush(device)) {
        return false;
    }

    *out_superblock = superblock;
    return true;
}

static bool reopen_and_verify(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_superblock_disk *expected
) {
    struct aurora_fs_v2_superblock_disk superblock;
    if (!read_fs_block(device, 0u, &superblock) ||
        !magic_valid(&superblock) ||
        superblock.version != AURORA_FS_V2_VERSION ||
        superblock.block_size != AURORA_FS_V2_BLOCK_SIZE ||
        superblock.metadata_checksum != superblock_checksum(&superblock) ||
        superblock.total_blocks != expected->total_blocks ||
        superblock.root_object_id != 1u ||
        superblock.next_object_id != 3u) {
        return false;
    }

    if (!read_fs_block(device, superblock.bitmap_start, bitmap_block)) {
        return false;
    }
    for (uint64_t block = 0u; block < superblock.data_start + 2u; ++block) {
        if (!bitmap_test(bitmap_block, block)) {
            return false;
        }
    }
    if (bitmap_test(bitmap_block, superblock.data_start + 2u)) {
        return false;
    }

    if (!read_fs_block(device, superblock.inode_start, inode_block)) {
        return false;
    }
    const struct aurora_fs_v2_inode_disk *inodes =
        (const struct aurora_fs_v2_inode_disk *)inode_block;
    const struct aurora_fs_v2_inode_disk *root = &inodes[0];
    const struct aurora_fs_v2_inode_disk *file = &inodes[1];

    if (root->object_id != 1u || root->type != AURORA_FS_V2_INODE_DIRECTORY ||
        file->object_id != 2u || file->parent_object_id != 1u ||
        file->type != AURORA_FS_V2_INODE_FILE ||
        file->size != AURORA_FS_V2_TEST_BYTES ||
        file->allocated_bytes != AURORA_FS_V2_BLOCK_SIZE * 2u ||
        file->extent_count != 1u ||
        file->extents[0].logical_block != 0u ||
        file->extents[0].physical_block != superblock.data_start ||
        file->extents[0].block_count != 2u) {
        return false;
    }

    uint64_t verified = 0u;
    for (uint64_t block = 0u; block < file->extents[0].block_count; ++block) {
        if (!read_fs_block(
                device,
                file->extents[0].physical_block + block,
                data_block)) {
            return false;
        }
        for (uint32_t i = 0u;
             i < AURORA_FS_V2_BLOCK_SIZE && verified < file->size;
             ++i, ++verified) {
            uint8_t expected_byte = (uint8_t)((verified * 37u + 11u) & 0xFFu);
            if (data_block[i] != expected_byte) {
                return false;
            }
        }
    }

    return verified == AURORA_FS_V2_TEST_BYTES;
}

static bool run_geometry(uint32_t device_block_size) {
    if (device_block_size != 512u && device_block_size != 4096u) {
        return false;
    }

    zero_bytes(test_storage, sizeof(test_storage));

    struct aurora_fs_v2_test_context context = {
        .storage = test_storage,
        .block_size = device_block_size,
        .block_count = AURORA_FS_V2_TEST_DEVICE_BYTES / device_block_size
    };

    struct aurora_block_device device = {
        .name = "aurorafs-v2-test",
        .block_size = device_block_size,
        .block_count = context.block_count,
        .read_only = false,
        .context = &context,
        .read_blocks = test_read,
        .write_blocks = test_write,
        .flush = test_flush
    };

    struct aurora_fs_v2_superblock_disk superblock;
    return format_v2(&device, &superblock) &&
        reopen_and_verify(&device, &superblock);
}

bool aurora_fs_v2_layout_self_test(void) {
    return run_geometry(512u) && run_geometry(4096u);
}
