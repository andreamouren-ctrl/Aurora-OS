#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs.h>
#include <aurora/block_device.h>

#define V2_BLOCK_SIZE 4096u
#define V2_BASE_BYTES 4096u
#define V2_VERSION 2u
#define V2_INODE_SIZE 256u
#define V2_INLINE_EXTENTS 4u
#define V2_INODE_FILE 1u
#define V2_INODE_DIRECTORY 2u
#define V2_DIRECTORY_RECORD_SIZE 128u
#define V2_DIRECTORY_NAME_MAX 108u
#define V2_TEST_DEVICE_BYTES (1024u * 1024u)
#define V2_INODE_BLOCKS 4u

struct v2_extent_disk {
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
    struct v2_extent_disk extents[V2_INLINE_EXTENTS];
    uint8_t reserved1[96];
} __attribute__((packed));

struct v2_superblock_disk {
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

struct v2_directory_record_disk {
    uint64_t object_id;
    uint32_t type;
    uint16_t name_length;
    uint16_t flags;
    uint32_t checksum;
    uint8_t name[V2_DIRECTORY_NAME_MAX];
} __attribute__((packed));

struct v2_test_context {
    uint8_t *storage;
    uint32_t block_size;
    uint64_t block_count;
};

static uint8_t directory_test_storage[V2_TEST_DEVICE_BYTES];
static uint8_t io_block[V2_BLOCK_SIZE];
static uint8_t inode_block[V2_BLOCK_SIZE];
static uint8_t bitmap_block[V2_BLOCK_SIZE];

static const uint8_t nested_payload[] = "AURORAFS-V2-NESTED-PERSIST";

_Static_assert(sizeof(struct v2_extent_disk) == 24u,
               "AuroraFS v2 extent layout drifted");
_Static_assert(sizeof(struct v2_inode_disk) == V2_INODE_SIZE,
               "AuroraFS v2 inode layout drifted");
_Static_assert(sizeof(struct v2_superblock_disk) == V2_BLOCK_SIZE,
               "AuroraFS v2 superblock layout drifted");
_Static_assert(sizeof(struct v2_directory_record_disk) == V2_DIRECTORY_RECORD_SIZE,
               "AuroraFS v2 directory record must be 128 bytes");

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

static uint32_t superblock_checksum(struct v2_superblock_disk *superblock) {
    uint32_t saved = superblock->metadata_checksum;
    superblock->metadata_checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)superblock, sizeof(*superblock));
    superblock->metadata_checksum = saved;
    return checksum;
}

static uint32_t directory_record_checksum(struct v2_directory_record_disk *record) {
    uint32_t saved = record->checksum;
    record->checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)record, sizeof(*record));
    record->checksum = saved;
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

    struct v2_test_context *context = device->context;
    if (context == NULL || context->block_size != device->block_size) {
        return false;
    }

    uint64_t offset;
    uint64_t length;
    if (!mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > V2_TEST_DEVICE_BYTES || length > V2_TEST_DEVICE_BYTES - offset) {
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

    struct v2_test_context *context = device->context;
    if (context == NULL || context->block_size != device->block_size) {
        return false;
    }

    uint64_t offset;
    uint64_t length;
    if (!mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > V2_TEST_DEVICE_BYTES || length > V2_TEST_DEVICE_BYTES - offset) {
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
        device->block_size > V2_BLOCK_SIZE) {
        return false;
    }

    uint8_t scratch[V2_BLOCK_SIZE];
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
        device->block_size == 0u || device->block_size > V2_BLOCK_SIZE) {
        return false;
    }

    uint8_t scratch[V2_BLOCK_SIZE];
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
    return V2_BASE_BYTES + block * V2_BLOCK_SIZE;
}

static bool read_fs_block(
    struct aurora_block_device *device,
    uint64_t block,
    void *buffer
) {
    return read_bytes(device, fs_block_offset(block), buffer, V2_BLOCK_SIZE);
}

static bool write_fs_block(
    struct aurora_block_device *device,
    uint64_t block,
    const void *buffer
) {
    return write_bytes(device, fs_block_offset(block), buffer, V2_BLOCK_SIZE);
}

static void bitmap_set(uint8_t *bitmap, uint64_t block) {
    bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
}

static bool bitmap_test(const uint8_t *bitmap, uint64_t block) {
    return (bitmap[block >> 3] & (uint8_t)(1u << (block & 7u))) != 0u;
}

static bool set_name(
    struct v2_directory_record_disk *record,
    const char *name,
    uint64_t object_id,
    uint32_t type
) {
    if (record == NULL || name == NULL || object_id == 0u) {
        return false;
    }

    zero_bytes(record, sizeof(*record));
    size_t length = 0u;
    while (name[length] != '\0') {
        if (length >= V2_DIRECTORY_NAME_MAX) {
            return false;
        }
        record->name[length] = (uint8_t)name[length];
        ++length;
    }

    record->object_id = object_id;
    record->type = type;
    record->name_length = (uint16_t)length;
    record->checksum = directory_record_checksum(record);
    return true;
}

static bool name_equals(
    const struct v2_directory_record_disk *record,
    const char *name
) {
    if (record == NULL || name == NULL || record->name_length > V2_DIRECTORY_NAME_MAX) {
        return false;
    }

    size_t length = 0u;
    while (name[length] != '\0') {
        ++length;
    }
    if (length != record->name_length) {
        return false;
    }

    for (size_t i = 0u; i < length; ++i) {
        if (record->name[i] != (uint8_t)name[i]) {
            return false;
        }
    }
    return true;
}

static bool write_superblock_and_metadata(struct aurora_block_device *device) {
    uint64_t device_bytes;
    if (!mul_u64(device->block_count, device->block_size, &device_bytes) ||
        device_bytes <= V2_BASE_BYTES + 16u * V2_BLOCK_SIZE) {
        return false;
    }

    uint64_t total_blocks = (device_bytes - V2_BASE_BYTES) / V2_BLOCK_SIZE;
    uint64_t bitmap_start = 1u;
    uint64_t inode_start = 2u;
    uint64_t data_start = inode_start + V2_INODE_BLOCKS;

    struct v2_superblock_disk superblock;
    zero_bytes(&superblock, sizeof(superblock));
    static const uint8_t magic[8] = { 'A', 'U', 'R', 'A', 'F', 'S', '2', 0 };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        superblock.magic[i] = magic[i];
    }
    superblock.version = V2_VERSION;
    superblock.block_size = V2_BLOCK_SIZE;
    superblock.total_blocks = total_blocks;
    superblock.generation = 2u;
    superblock.bitmap_start = bitmap_start;
    superblock.bitmap_blocks = 1u;
    superblock.inode_start = inode_start;
    superblock.inode_blocks = V2_INODE_BLOCKS;
    superblock.data_start = data_start;
    superblock.root_object_id = 1u;
    superblock.next_object_id = 4u;
    superblock.metadata_checksum = superblock_checksum(&superblock);

    zero_bytes(bitmap_block, sizeof(bitmap_block));
    for (uint64_t block = 0u; block < data_start + 4u; ++block) {
        bitmap_set(bitmap_block, block);
    }

    zero_bytes(inode_block, sizeof(inode_block));
    struct v2_inode_disk *inodes = (struct v2_inode_disk *)inode_block;

    inodes[0].object_id = 1u;
    inodes[0].parent_object_id = 1u;
    inodes[0].size = V2_BLOCK_SIZE + V2_DIRECTORY_RECORD_SIZE;
    inodes[0].allocated_bytes = V2_BLOCK_SIZE * 2u;
    inodes[0].generation = 2u;
    inodes[0].type = V2_INODE_DIRECTORY;
    inodes[0].extent_count = 1u;
    inodes[0].extents[0].logical_block = 0u;
    inodes[0].extents[0].physical_block = data_start;
    inodes[0].extents[0].block_count = 2u;

    inodes[1].object_id = 2u;
    inodes[1].parent_object_id = 1u;
    inodes[1].size = V2_DIRECTORY_RECORD_SIZE;
    inodes[1].allocated_bytes = V2_BLOCK_SIZE;
    inodes[1].generation = 2u;
    inodes[1].type = V2_INODE_DIRECTORY;
    inodes[1].extent_count = 1u;
    inodes[1].extents[0].logical_block = 0u;
    inodes[1].extents[0].physical_block = data_start + 2u;
    inodes[1].extents[0].block_count = 1u;

    inodes[2].object_id = 3u;
    inodes[2].parent_object_id = 2u;
    inodes[2].size = sizeof(nested_payload) - 1u;
    inodes[2].allocated_bytes = V2_BLOCK_SIZE;
    inodes[2].generation = 2u;
    inodes[2].type = V2_INODE_FILE;
    inodes[2].extent_count = 1u;
    inodes[2].extents[0].logical_block = 0u;
    inodes[2].extents[0].physical_block = data_start + 3u;
    inodes[2].extents[0].block_count = 1u;

    if (!write_fs_block(device, 0u, &superblock) ||
        !write_fs_block(device, bitmap_start, bitmap_block) ||
        !write_fs_block(device, inode_start, inode_block)) {
        return false;
    }

    zero_bytes(io_block, sizeof(io_block));
    if (!write_fs_block(device, data_start, io_block)) {
        return false;
    }

    zero_bytes(io_block, sizeof(io_block));
    struct v2_directory_record_disk *root_tail =
        (struct v2_directory_record_disk *)io_block;
    if (!set_name(&root_tail[0], "docs", 2u, V2_INODE_DIRECTORY) ||
        !write_fs_block(device, data_start + 1u, io_block)) {
        return false;
    }

    zero_bytes(io_block, sizeof(io_block));
    struct v2_directory_record_disk *docs_records =
        (struct v2_directory_record_disk *)io_block;
    if (!set_name(&docs_records[0], "note.txt", 3u, V2_INODE_FILE) ||
        !write_fs_block(device, data_start + 2u, io_block)) {
        return false;
    }

    zero_bytes(io_block, sizeof(io_block));
    for (size_t i = 0u; i < sizeof(nested_payload) - 1u; ++i) {
        io_block[i] = nested_payload[i];
    }
    return write_fs_block(device, data_start + 3u, io_block) &&
        block_device_flush(device);
}

static bool read_superblock(
    struct aurora_block_device *device,
    struct v2_superblock_disk *superblock
) {
    static const uint8_t magic[8] = { 'A', 'U', 'R', 'A', 'F', 'S', '2', 0 };
    if (!read_fs_block(device, 0u, superblock)) {
        return false;
    }
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        if (superblock->magic[i] != magic[i]) {
            return false;
        }
    }
    return superblock->version == V2_VERSION &&
        superblock->block_size == V2_BLOCK_SIZE &&
        superblock->metadata_checksum == superblock_checksum(superblock);
}

static bool find_inode(
    struct aurora_block_device *device,
    const struct v2_superblock_disk *superblock,
    uint64_t object_id,
    struct v2_inode_disk *out_inode
) {
    if (object_id == 0u || out_inode == NULL) {
        return false;
    }

    uint64_t entries_per_block = V2_BLOCK_SIZE / V2_INODE_SIZE;
    for (uint64_t block = 0u; block < superblock->inode_blocks; ++block) {
        if (!read_fs_block(device, superblock->inode_start + block, inode_block)) {
            return false;
        }
        const struct v2_inode_disk *inodes =
            (const struct v2_inode_disk *)inode_block;
        for (uint64_t i = 0u; i < entries_per_block; ++i) {
            if (inodes[i].object_id == object_id) {
                *out_inode = inodes[i];
                return true;
            }
        }
    }
    return false;
}

static bool lookup_child(
    struct aurora_block_device *device,
    const struct v2_inode_disk *directory,
    const char *name,
    uint64_t *out_object_id,
    uint32_t *out_type
) {
    if (directory == NULL || name == NULL || out_object_id == NULL ||
        directory->type != V2_INODE_DIRECTORY ||
        directory->extent_count > V2_INLINE_EXTENTS) {
        return false;
    }

    uint64_t remaining_records =
        (directory->size + V2_DIRECTORY_RECORD_SIZE - 1u) /
        V2_DIRECTORY_RECORD_SIZE;
    uint64_t records_per_block = V2_BLOCK_SIZE / V2_DIRECTORY_RECORD_SIZE;

    for (uint32_t extent_index = 0u;
         extent_index < directory->extent_count && remaining_records > 0u;
         ++extent_index) {
        const struct v2_extent_disk *extent = &directory->extents[extent_index];
        for (uint64_t block = 0u;
             block < extent->block_count && remaining_records > 0u;
             ++block) {
            if (!read_fs_block(device, extent->physical_block + block, io_block)) {
                return false;
            }

            struct v2_directory_record_disk *records =
                (struct v2_directory_record_disk *)io_block;
            uint64_t records_here = remaining_records;
            if (records_here > records_per_block) {
                records_here = records_per_block;
            }

            for (uint64_t i = 0u; i < records_here; ++i) {
                if (records[i].object_id == 0u) {
                    continue;
                }
                if (records[i].name_length == 0u ||
                    records[i].name_length > V2_DIRECTORY_NAME_MAX ||
                    records[i].checksum != directory_record_checksum(&records[i])) {
                    return false;
                }
                if (name_equals(&records[i], name)) {
                    *out_object_id = records[i].object_id;
                    if (out_type != NULL) {
                        *out_type = records[i].type;
                    }
                    return true;
                }
            }
            remaining_records -= records_here;
        }
    }
    return false;
}

static bool read_file_payload(
    struct aurora_block_device *device,
    const struct v2_inode_disk *file
) {
    if (file == NULL || file->type != V2_INODE_FILE ||
        file->extent_count != 1u || file->size != sizeof(nested_payload) - 1u) {
        return false;
    }

    if (!read_fs_block(device, file->extents[0].physical_block, io_block)) {
        return false;
    }
    for (size_t i = 0u; i < sizeof(nested_payload) - 1u; ++i) {
        if (io_block[i] != nested_payload[i]) {
            return false;
        }
    }
    return true;
}

static bool reopen_and_resolve_nested_path(struct aurora_block_device *device) {
    struct v2_superblock_disk superblock;
    if (!read_superblock(device, &superblock) ||
        superblock.root_object_id != 1u || superblock.next_object_id != 4u ||
        superblock.bitmap_blocks != 1u) {
        return false;
    }

    if (!read_fs_block(device, superblock.bitmap_start, bitmap_block)) {
        return false;
    }
    for (uint64_t block = 0u; block < superblock.data_start + 4u; ++block) {
        if (!bitmap_test(bitmap_block, block)) {
            return false;
        }
    }
    if (bitmap_test(bitmap_block, superblock.data_start + 4u)) {
        return false;
    }

    struct v2_inode_disk root;
    struct v2_inode_disk docs;
    struct v2_inode_disk note;
    if (!find_inode(device, &superblock, 1u, &root) ||
        root.type != V2_INODE_DIRECTORY || root.extents[0].block_count != 2u) {
        return false;
    }

    uint64_t docs_id = 0u;
    uint32_t docs_type = 0u;
    if (!lookup_child(device, &root, "docs", &docs_id, &docs_type) ||
        docs_id != 2u || docs_type != V2_INODE_DIRECTORY ||
        !find_inode(device, &superblock, docs_id, &docs)) {
        return false;
    }

    uint64_t note_id = 0u;
    uint32_t note_type = 0u;
    if (!lookup_child(device, &docs, "note.txt", &note_id, &note_type) ||
        note_id != 3u || note_type != V2_INODE_FILE ||
        !find_inode(device, &superblock, note_id, &note)) {
        return false;
    }

    return read_file_payload(device, &note);
}

static bool run_geometry(uint32_t device_block_size) {
    if (device_block_size != 512u && device_block_size != 4096u) {
        return false;
    }

    zero_bytes(directory_test_storage, sizeof(directory_test_storage));
    struct v2_test_context context = {
        .storage = directory_test_storage,
        .block_size = device_block_size,
        .block_count = V2_TEST_DEVICE_BYTES / device_block_size
    };
    struct aurora_block_device device = {
        .name = "aurorafs-v2-directory-test",
        .block_size = device_block_size,
        .block_count = context.block_count,
        .read_only = false,
        .context = &context,
        .read_blocks = test_read,
        .write_blocks = test_write,
        .flush = test_flush
    };

    return write_superblock_and_metadata(&device) &&
        reopen_and_resolve_nested_path(&device);
}

bool aurora_fs_v2_directory_self_test(void) {
    return run_geometry(512u) && run_geometry(4096u);
}
