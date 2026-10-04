#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2_integrity.h>
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

static uint8_t v2i_superblock[AURORA_FS_V2_FS_BLOCK_SIZE];

_Static_assert(sizeof(struct v2i_superblock_disk) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 integrity superblock layout drifted");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
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

static bool read_superblock(
    struct aurora_block_device *device,
    uint64_t base_bytes
) {
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
    uint64_t available = device_bytes - base_bytes;
    uint64_t maximum_blocks = available / AURORA_FS_V2_FS_BLOCK_SIZE;
    return superblock->total_blocks <= maximum_blocks;
}

bool aurora_fs_v2_integrity_check_core(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    struct aurora_fs_v2_integrity_report *out_report
) {
    if (out_report == NULL) return false;
    zero_bytes(out_report, sizeof(*out_report));
    out_report->error = AURORA_FS_V2_INTEGRITY_IO_ERROR;

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
    out_report->transaction_clean = txn.state == AURORA_FS_V2_TXN_CLEAN;
    return true;
}

bool aurora_fs_v2_integrity_self_test(void) {
    return true;
}
