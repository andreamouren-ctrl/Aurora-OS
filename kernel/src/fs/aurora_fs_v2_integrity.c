#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2_integrity.h>
#include <aurora/aurora_fs_v2_metadata.h>
#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/aurora_fs_v2_txn.h>
#include <aurora/block_device.h>

#define V2I_MAGIC_0 'A'
#define V2I_MAGIC_1 'U'
#define V2I_MAGIC_2 'R'
#define V2I_MAGIC_3 'A'
#define V2I_MAGIC_4 'F'
#define V2I_MAGIC_5 'S'
#define V2I_MAGIC_6 '2'
#define V2I_MAGIC_7 '\0'
#define V2I_VERSION 2u
#define V2I_INODE_SIZE 256u
#define V2I_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2I_INODE_SIZE)
#define V2I_DIRECTORY_RECORD_SIZE 128u
#define V2I_DIRECTORY_NAME_MAX 108u
#define V2I_TEST_STORAGE_BYTES (1024u * 1024u)

struct v2i_superblock_disk {
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

struct v2i_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2i_inode_disk {
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
    struct v2i_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct v2i_directory_record_disk {
    uint64_t object_id;
    uint32_t type;
    uint16_t name_length;
    uint16_t flags;
    uint32_t checksum;
    uint8_t name[V2I_DIRECTORY_NAME_MAX];
} __attribute__((packed));

struct v2i_test_context {
    uint8_t *storage;
    uint64_t storage_bytes;
};

static uint8_t v2i_superblock[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2i_inode_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2i_directory_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2i_test_storage[V2I_TEST_STORAGE_BYTES];

_Static_assert(sizeof(struct v2i_superblock_disk) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 integrity superblock layout drifted");
_Static_assert(sizeof(struct v2i_inode_disk) == V2I_INODE_SIZE,
               "AuroraFS v2 integrity inode layout drifted");
_Static_assert(sizeof(struct v2i_directory_record_disk) == V2I_DIRECTORY_RECORD_SIZE,
               "AuroraFS v2 integrity directory layout drifted");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t i = 0u; i < length; ++i) out[i] = in[i];
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

static uint32_t superblock_checksum(struct v2i_superblock_disk *superblock) {
    uint32_t saved = superblock->metadata_checksum;
    superblock->metadata_checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)superblock, sizeof(*superblock));
    superblock->metadata_checksum = saved;
    return checksum;
}

static uint32_t directory_record_checksum(struct v2i_directory_record_disk *record) {
    uint32_t saved = record->checksum;
    record->checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)record, sizeof(*record));
    record->checksum = saved;
    return checksum;
}

static bool magic_valid(const struct v2i_superblock_disk *superblock) {
    static const uint8_t magic[8] = {
        V2I_MAGIC_0, V2I_MAGIC_1, V2I_MAGIC_2, V2I_MAGIC_3,
        V2I_MAGIC_4, V2I_MAGIC_5, V2I_MAGIC_6, V2I_MAGIC_7
    };
    if (superblock == NULL) return false;
    for (size_t i = 0u; i < sizeof(magic); ++i)
        if (superblock->magic[i] != magic[i]) return false;
    return true;
}

static bool type_valid(uint32_t type) {
    return type == (uint32_t)AURORA_FS_V2_OBJECT_FILE ||
        type == (uint32_t)AURORA_FS_V2_OBJECT_DIRECTORY;
}

static bool read_superblock(struct aurora_block_device *device, uint64_t base_bytes) {
    if (device == NULL || device->block_size == 0u ||
        device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) != 0u ||
        (base_bytes % device->block_size) != 0u) return false;
    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    uint64_t lba = base_bytes / device->block_size;
    if (count == 0u || count > UINT32_MAX || lba >= device->block_count ||
        count > device->block_count - lba) return false;
    return block_device_read(device, lba, (uint32_t)count, v2i_superblock);
}

static bool geometry_valid(
    const struct v2i_superblock_disk *superblock,
    struct aurora_block_device *device,
    uint64_t base_bytes
) {
    if (superblock == NULL || device == NULL || superblock->total_blocks == 0u ||
        superblock->bitmap_start != 1u || superblock->bitmap_blocks == 0u ||
        superblock->inode_blocks == 0u ||
        superblock->inode_start != superblock->bitmap_start + superblock->bitmap_blocks ||
        superblock->data_start != superblock->inode_start + superblock->inode_blocks ||
        superblock->data_start >= superblock->total_blocks ||
        superblock->root_object_id == 0u ||
        superblock->next_object_id <= superblock->root_object_id)
        return false;

    if (device->block_count > UINT64_MAX / device->block_size) return false;
    uint64_t device_bytes = device->block_count * device->block_size;
    if (base_bytes > device_bytes) return false;
    return superblock->total_blocks <=
        (device_bytes - base_bytes) / AURORA_FS_V2_FS_BLOCK_SIZE;
}

static bool fs_block_geometry(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t total_blocks,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_count
) {
    if (device == NULL || out_lba == NULL || out_count == NULL ||
        fs_block >= total_blocks || device->block_size == 0u ||
        device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) != 0u ||
        (base_bytes % device->block_size) != 0u) return false;
    uint64_t fs_bytes;
    uint64_t byte_offset;
    if (!mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_bytes) ||
        !add_u64(base_bytes, fs_bytes, &byte_offset)) return false;
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
    return buffer != NULL &&
        fs_block_geometry(device, geometry->base_bytes, geometry->total_fs_blocks,
                          fs_block, &lba, &count) &&
        block_device_read(device, lba, count, buffer);
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct v2i_inode_disk *out_inode
) {
    if (out_inode == NULL || inode_index >= geometry->inode_blocks * V2I_INODES_PER_BLOCK)
        return false;
    uint64_t fs_block = geometry->inode_start + inode_index / V2I_INODES_PER_BLOCK;
    uint32_t slot = (uint32_t)(inode_index % V2I_INODES_PER_BLOCK);
    if (!read_fs_block(device, geometry, fs_block, v2i_inode_block)) return false;
    *out_inode = ((const struct v2i_inode_disk *)v2i_inode_block)[slot];
    return true;
}

static bool inode_is_zero(const struct v2i_inode_disk *inode) {
    const uint8_t *bytes = (const uint8_t *)inode;
    for (size_t i = 0u; i < sizeof(*inode); ++i)
        if (bytes[i] != 0u) return false;
    return true;
}

static bool block_allocated(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t block
) {
    bool allocated = false;
    return aurora_fs_v2_allocator_is_allocated(allocator, block, &allocated) && allocated;
}

static bool inline_physical(
    const struct v2i_inode_disk *inode,
    uint64_t logical,
    uint64_t *out_physical
) {
    if (inode == NULL || out_physical == NULL || inode->extent_tree_root != 0u ||
        inode->extent_count > AURORA_FS_V2_INLINE_EXTENT_COUNT) return false;
    for (uint32_t i = 0u; i < inode->extent_count; ++i) {
        uint64_t end;
        if (inode->extents[i].block_count == 0u ||
            !add_u64(inode->extents[i].logical_block, inode->extents[i].block_count, &end))
            return false;
        if (logical >= inode->extents[i].logical_block && logical < end) {
            *out_physical = inode->extents[i].physical_block +
                (logical - inode->extents[i].logical_block);
            return true;
        }
    }
    return false;
}

static bool validate_inode(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    const struct v2i_inode_disk *inode,
    enum aurora_fs_v2_integrity_error *out_error
) {
    if (inode->object_id == 0u) {
        if (!inode_is_zero(inode)) {
            *out_error = AURORA_FS_V2_INTEGRITY_BAD_INODE;
            return false;
        }
        return true;
    }
    if (!type_valid(inode->type) || inode->parent_object_id == 0u ||
        inode->generation == 0u ||
        (inode->allocated_bytes % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u ||
        inode->size > inode->allocated_bytes) {
        *out_error = AURORA_FS_V2_INTEGRITY_BAD_INODE;
        return false;
    }

    if (inode->extent_tree_root != 0u) {
        if (inode->extent_tree_root < geometry->data_start ||
            inode->extent_tree_root >= geometry->total_fs_blocks ||
            !block_allocated(allocator, inode->extent_tree_root)) {
            *out_error = AURORA_FS_V2_INTEGRITY_BAD_BITMAP;
            return false;
        }
        return true;
    }

    if (inode->extent_count > AURORA_FS_V2_INLINE_EXTENT_COUNT) {
        *out_error = AURORA_FS_V2_INTEGRITY_BAD_INODE;
        return false;
    }
    uint64_t allocated_blocks = 0u;
    uint64_t previous_logical_end = 0u;
    for (uint32_t i = 0u; i < inode->extent_count; ++i) {
        const struct v2i_extent_disk *extent = &inode->extents[i];
        uint64_t logical_end;
        uint64_t physical_end;
        if (extent->block_count == 0u || extent->physical_block < geometry->data_start ||
            !add_u64(extent->logical_block, extent->block_count, &logical_end) ||
            !add_u64(extent->physical_block, extent->block_count, &physical_end) ||
            physical_end > geometry->total_fs_blocks ||
            (i != 0u && extent->logical_block < previous_logical_end) ||
            !add_u64(allocated_blocks, extent->block_count, &allocated_blocks)) {
            *out_error = AURORA_FS_V2_INTEGRITY_BAD_INODE;
            return false;
        }
        for (uint64_t b = 0u; b < extent->block_count; ++b) {
            if (!block_allocated(allocator, extent->physical_block + b)) {
                *out_error = AURORA_FS_V2_INTEGRITY_BAD_BITMAP;
                return false;
            }
        }
        previous_logical_end = logical_end;
    }
    if (allocated_blocks > UINT64_MAX / AURORA_FS_V2_FS_BLOCK_SIZE ||
        allocated_blocks * AURORA_FS_V2_FS_BLOCK_SIZE != inode->allocated_bytes) {
        *out_error = AURORA_FS_V2_INTEGRITY_BAD_INODE;
        return false;
    }
    return true;
}

static bool find_object(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t object_id,
    uint64_t *out_inode_index,
    struct v2i_inode_disk *out_inode
) {
    uint64_t inode_capacity = geometry->inode_blocks * V2I_INODES_PER_BLOCK;
    for (uint64_t index = 0u; index < inode_capacity; ++index) {
        struct v2i_inode_disk inode;
        if (!read_inode(device, geometry, index, &inode)) return false;
        if (inode.object_id == object_id) {
            if (out_inode_index != NULL) *out_inode_index = index;
            if (out_inode != NULL) *out_inode = inode;
            return true;
        }
    }
    return false;
}

static bool scan_directory(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct v2i_inode_disk *inode,
    struct aurora_fs_v2_integrity_report *report
) {
    if ((inode->size % V2I_DIRECTORY_RECORD_SIZE) != 0u) {
        report->error = AURORA_FS_V2_INTEGRITY_BAD_DIRECTORY;
        report->failing_inode_index = inode_index;
        return false;
    }
    uint64_t record_count = inode->size / V2I_DIRECTORY_RECORD_SIZE;
    uint64_t cached_physical = UINT64_MAX;
    for (uint64_t record_index = 0u; record_index < record_count; ++record_index) {
        uint64_t byte_offset = record_index * V2I_DIRECTORY_RECORD_SIZE;
        uint64_t logical = byte_offset / AURORA_FS_V2_FS_BLOCK_SIZE;
        uint64_t within = byte_offset % AURORA_FS_V2_FS_BLOCK_SIZE;
        uint64_t physical = 0u;
        if (inode->extent_tree_root != 0u) {
            uint64_t contiguous = 0u;
            if (!aurora_fs_v2_extent_tree_lookup_unified(
                    allocator, inode->extent_tree_root, logical, &physical, &contiguous)) {
                report->error = AURORA_FS_V2_INTEGRITY_BAD_DIRECTORY;
                report->failing_inode_index = inode_index;
                report->failing_record_index = record_index;
                return false;
            }
        } else if (!inline_physical(inode, logical, &physical)) {
            report->error = AURORA_FS_V2_INTEGRITY_BAD_DIRECTORY;
            report->failing_inode_index = inode_index;
            report->failing_record_index = record_index;
            return false;
        }
        if (!block_allocated(allocator, physical)) {
            report->error = AURORA_FS_V2_INTEGRITY_BAD_BITMAP;
            report->failing_inode_index = inode_index;
            report->failing_record_index = record_index;
            return false;
        }
        if (physical != cached_physical) {
            if (!read_fs_block(allocator->device, geometry, physical, v2i_directory_block)) {
                report->error = AURORA_FS_V2_INTEGRITY_IO_ERROR;
                report->failing_inode_index = inode_index;
                report->failing_record_index = record_index;
                return false;
            }
            cached_physical = physical;
        }
        struct v2i_directory_record_disk *record =
            (struct v2i_directory_record_disk *)(v2i_directory_block + within);
        if (record->object_id == 0u || !type_valid(record->type) ||
            record->name_length == 0u || record->name_length > V2I_DIRECTORY_NAME_MAX ||
            record->checksum != directory_record_checksum(record)) {
            report->error = AURORA_FS_V2_INTEGRITY_BAD_DIRECTORY;
            report->failing_inode_index = inode_index;
            report->failing_record_index = record_index;
            return false;
        }
        for (uint16_t i = 0u; i < record->name_length; ++i) {
            if (record->name[i] == 0u) {
                report->error = AURORA_FS_V2_INTEGRITY_BAD_DIRECTORY;
                report->failing_inode_index = inode_index;
                report->failing_record_index = record_index;
                return false;
            }
        }
        struct v2i_inode_disk child;
        uint64_t child_inode_index;
        if (!find_object(allocator->device, geometry, record->object_id,
                         &child_inode_index, &child) ||
            child_inode_index == inode_index || child.type != record->type ||
            child.parent_object_id != inode->object_id) {
            report->error = AURORA_FS_V2_INTEGRITY_BAD_REFERENCE;
            report->failing_inode_index = inode_index;
            report->failing_record_index = record_index;
            return false;
        }
        report->directory_records_scanned++;
    }
    report->directories_scanned++;
    return true;
}

bool aurora_fs_v2_integrity_check_core(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    struct aurora_fs_v2_integrity_report *out_report
) {
    if (out_report == NULL) return false;
    zero_bytes(out_report, sizeof(*out_report));
    out_report->error = AURORA_FS_V2_INTEGRITY_IO_ERROR;
    out_report->failing_inode_index = UINT64_MAX;
    out_report->failing_record_index = UINT64_MAX;

    if (!read_superblock(device, base_bytes)) return false;
    struct v2i_superblock_disk *superblock = (struct v2i_superblock_disk *)v2i_superblock;
    if (!magic_valid(superblock)) {
        out_report->error = AURORA_FS_V2_INTEGRITY_BAD_MAGIC;
        return false;
    }
    if (superblock->version != V2I_VERSION) {
        out_report->error = AURORA_FS_V2_INTEGRITY_BAD_VERSION;
        return false;
    }
    if (superblock->block_size != AURORA_FS_V2_FS_BLOCK_SIZE) {
        out_report->error = AURORA_FS_V2_INTEGRITY_BAD_BLOCK_SIZE;
        return false;
    }
    if (superblock->metadata_checksum != superblock_checksum(superblock)) {
        out_report->error = AURORA_FS_V2_INTEGRITY_BAD_SUPERBLOCK_CHECKSUM;
        return false;
    }
    if (!geometry_valid(superblock, device, base_bytes)) {
        out_report->error = AURORA_FS_V2_INTEGRITY_BAD_GEOMETRY;
        return false;
    }

    struct aurora_fs_v2_txn_record txn;
    if (!aurora_fs_v2_txn_load(device, base_bytes, &txn)) {
        out_report->error = AURORA_FS_V2_INTEGRITY_BAD_TRANSACTION;
        return false;
    }

    out_report->error = AURORA_FS_V2_INTEGRITY_OK;
    out_report->total_blocks = superblock->total_blocks;
    out_report->bitmap_start = superblock->bitmap_start;
    out_report->bitmap_blocks = superblock->bitmap_blocks;
    out_report->inode_start = superblock->inode_start;
    out_report->inode_blocks = superblock->inode_blocks;
    out_report->data_start = superblock->data_start;
    out_report->root_object_id = superblock->root_object_id;
    out_report->transaction_clean = txn.state == AURORA_FS_V2_TXN_CLEAN;
    return true;
}

bool aurora_fs_v2_integrity_check_full(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    struct aurora_fs_v2_integrity_report *out_report
) {
    if (!aurora_fs_v2_integrity_check_core(device, base_bytes, out_report)) return false;

    struct aurora_fs_v2_format_geometry geometry = {
        .base_bytes = base_bytes,
        .total_fs_blocks = out_report->total_blocks,
        .bitmap_start = out_report->bitmap_start,
        .bitmap_blocks = out_report->bitmap_blocks,
        .inode_start = out_report->inode_start,
        .inode_blocks = out_report->inode_blocks,
        .data_start = out_report->data_start
    };
    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator, device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) {
        out_report->error = AURORA_FS_V2_INTEGRITY_BAD_GEOMETRY;
        return false;
    }

    struct v2i_inode_disk root;
    if (!read_inode(device, &geometry, 0u, &root)) {
        out_report->error = AURORA_FS_V2_INTEGRITY_IO_ERROR;
        return false;
    }
    if (root.object_id != out_report->root_object_id ||
        root.type != (uint32_t)AURORA_FS_V2_OBJECT_DIRECTORY ||
        root.parent_object_id != root.object_id) {
        out_report->error = AURORA_FS_V2_INTEGRITY_BAD_INODE;
        out_report->failing_inode_index = 0u;
        return false;
    }

    uint64_t inode_capacity = geometry.inode_blocks * V2I_INODES_PER_BLOCK;
    for (uint64_t index = 0u; index < inode_capacity; ++index) {
        struct v2i_inode_disk inode;
        enum aurora_fs_v2_integrity_error error = AURORA_FS_V2_INTEGRITY_OK;
        if (!read_inode(device, &geometry, index, &inode)) {
            out_report->error = AURORA_FS_V2_INTEGRITY_IO_ERROR;
            out_report->failing_inode_index = index;
            return false;
        }
        if (!validate_inode(&allocator, &geometry, &inode, &error)) {
            out_report->error = error;
            out_report->failing_inode_index = index;
            return false;
        }
        if (inode.object_id != 0u) {
            struct aurora_fs_v2_metadata metadata;
            if (!aurora_fs_v2_metadata_read(device, &geometry, index, &metadata)) {
                out_report->error = AURORA_FS_V2_INTEGRITY_BAD_METADATA;
                out_report->failing_inode_index = index;
                return false;
            }
            out_report->active_inodes++;
        }
    }

    for (uint64_t index = 0u; index < inode_capacity; ++index) {
        struct v2i_inode_disk inode;
        if (!read_inode(device, &geometry, index, &inode)) {
            out_report->error = AURORA_FS_V2_INTEGRITY_IO_ERROR;
            out_report->failing_inode_index = index;
            return false;
        }
        if (inode.object_id != 0u &&
            inode.type == (uint32_t)AURORA_FS_V2_OBJECT_DIRECTORY &&
            !scan_directory(&allocator, &geometry, index, &inode, out_report))
            return false;
    }
    out_report->error = AURORA_FS_V2_INTEGRITY_OK;
    return true;
}

static bool test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t count,
    void *buffer
) {
    if (device == NULL || buffer == NULL || count == 0u ||
        lba >= device->block_count || (uint64_t)count > device->block_count - lba)
        return false;
    struct v2i_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset)
        return false;
    copy_bytes(buffer, context->storage + offset, (size_t)length);
    return true;
}

static bool test_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t count,
    const void *buffer
) {
    if (device == NULL || buffer == NULL || device->read_only || count == 0u ||
        lba >= device->block_count || (uint64_t)count > device->block_count - lba)
        return false;
    struct v2i_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset)
        return false;
    copy_bytes(context->storage + offset, buffer, (size_t)length);
    return true;
}

static bool test_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool expect_failure(
    struct aurora_block_device *device,
    enum aurora_fs_v2_integrity_error expected
) {
    struct aurora_fs_v2_integrity_report report;
    return !aurora_fs_v2_integrity_check_full(
        device, AURORA_FS_V2_DEFAULT_BASE_BYTES, &report) && report.error == expected;
}

static bool run_integrity_test(uint32_t block_size) {
    zero_bytes(v2i_test_storage, sizeof(v2i_test_storage));
    struct v2i_test_context context = {
        .storage = v2i_test_storage,
        .storage_bytes = sizeof(v2i_test_storage)
    };
    struct aurora_block_device device = {
        .name = "aurorafs-v2-integrity-test",
        .block_size = block_size,
        .block_count = sizeof(v2i_test_storage) / block_size,
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
            &allocator, &geometry, 0u, 1u, 2u,
            AURORA_FS_V2_OBJECT_FILE, "alpha") ||
        !aurora_fs_v2_create_child(
            &allocator, &geometry, 0u, 2u, 3u,
            AURORA_FS_V2_OBJECT_DIRECTORY, "docs")) return false;

    struct aurora_fs_v2_integrity_report report;
    if (!aurora_fs_v2_integrity_check_full(
            &device, AURORA_FS_V2_DEFAULT_BASE_BYTES, &report) ||
        report.error != AURORA_FS_V2_INTEGRITY_OK ||
        report.active_inodes != 3u || report.directories_scanned != 2u ||
        report.directory_records_scanned != 2u) return false;

    uint64_t inode1_offset = geometry.base_bytes +
        geometry.inode_start * AURORA_FS_V2_FS_BLOCK_SIZE + V2I_INODE_SIZE;
    struct v2i_inode_disk *inode1 =
        (struct v2i_inode_disk *)(v2i_test_storage + inode1_offset);
    uint32_t saved_type = inode1->type;
    inode1->type = 99u;
    if (!expect_failure(&device, AURORA_FS_V2_INTEGRITY_BAD_INODE)) return false;
    inode1->type = saved_type;

    if (!aurora_fs_v2_metadata_initialize(
            &device, &geometry, 1u, AURORA_FS_V2_OBJECT_FILE,
            7u, 9u, 0640u, 0u)) return false;
    if (!aurora_fs_v2_integrity_check_full(
            &device, AURORA_FS_V2_DEFAULT_BASE_BYTES, &report)) return false;
    uint8_t saved_metadata_magic = inode1->reserved1[0];
    inode1->reserved1[0] ^= 1u;
    if (!expect_failure(&device, AURORA_FS_V2_INTEGRITY_BAD_METADATA)) return false;
    inode1->reserved1[0] = saved_metadata_magic;

    uint64_t root_offset = geometry.base_bytes +
        geometry.inode_start * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct v2i_inode_disk *root =
        (struct v2i_inode_disk *)(v2i_test_storage + root_offset);
    if (root->extent_count == 0u) return false;
    uint64_t directory_physical = root->extents[0].physical_block;
    uint64_t record_offset = geometry.base_bytes +
        directory_physical * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct v2i_directory_record_disk *record =
        (struct v2i_directory_record_disk *)(v2i_test_storage + record_offset);
    struct v2i_directory_record_disk saved_record = *record;
    record->checksum ^= 1u;
    if (!expect_failure(&device, AURORA_FS_V2_INTEGRITY_BAD_DIRECTORY)) return false;
    *record = saved_record;

    uint64_t bitmap_offset = geometry.base_bytes +
        geometry.bitmap_start * AURORA_FS_V2_FS_BLOCK_SIZE + directory_physical / 8u;
    uint8_t saved_bitmap_byte = v2i_test_storage[bitmap_offset];
    v2i_test_storage[bitmap_offset] &=
        (uint8_t)~(1u << (directory_physical & 7u));
    if (!expect_failure(&device, AURORA_FS_V2_INTEGRITY_BAD_BITMAP)) return false;
    v2i_test_storage[bitmap_offset] = saved_bitmap_byte;

    record->object_id = 999u;
    record->checksum = directory_record_checksum(record);
    if (!expect_failure(&device, AURORA_FS_V2_INTEGRITY_BAD_REFERENCE)) return false;
    *record = saved_record;

    struct v2i_superblock_disk *superblock =
        (struct v2i_superblock_disk *)(v2i_test_storage + AURORA_FS_V2_DEFAULT_BASE_BYTES);
    superblock->generation ^= 1u;
    if (!expect_failure(&device, AURORA_FS_V2_INTEGRITY_BAD_SUPERBLOCK_CHECKSUM)) return false;
    superblock->generation ^= 1u;

    return aurora_fs_v2_integrity_check_full(
        &device, AURORA_FS_V2_DEFAULT_BASE_BYTES, &report);
}

bool aurora_fs_v2_integrity_self_test(void) {
    return run_integrity_test(512u) && run_integrity_test(4096u);
}
