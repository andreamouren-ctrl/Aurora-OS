#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2_txn.h>
#include <aurora/block_device.h>

#define V2TX_MAGIC_0 'A'
#define V2TX_MAGIC_1 'U'
#define V2TX_MAGIC_2 'R'
#define V2TX_MAGIC_3 'T'
#define V2TX_MAGIC_4 'X'
#define V2TX_MAGIC_5 'N'
#define V2TX_MAGIC_6 '2'
#define V2TX_MAGIC_7 '\0'
#define V2TX_VERSION 1u
#define V2TX_SUPERBLOCK_CHECKSUM_OFFSET 96u
#define V2TX_RECORD_OFFSET 128u
#define V2TX_RECORD_SIZE 256u

struct v2tx_range_disk {
    uint64_t first_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2tx_record_disk {
    uint8_t magic[8];
    uint32_t version;
    uint32_t state;
    uint32_t operation;
    uint32_t range_count;
    uint64_t sequence;
    uint64_t inode_index;
    uint64_t old_root;
    uint64_t new_root;
    uint64_t old_size;
    uint64_t new_size;
    struct v2tx_range_disk ranges[AURORA_FS_V2_TXN_RANGE_CAPACITY];
    uint32_t checksum;
    uint8_t reserved[52];
} __attribute__((packed));

static uint8_t v2tx_superblock[AURORA_FS_V2_FS_BLOCK_SIZE];

_Static_assert(sizeof(struct v2tx_record_disk) == V2TX_RECORD_SIZE,
               "AuroraFS v2 transaction record must remain 256 bytes");
_Static_assert(V2TX_RECORD_OFFSET + V2TX_RECORD_SIZE <= AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 transaction record must fit in superblock reserved area");

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

static bool fs_block_geometry(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t *out_lba,
    uint32_t *out_count
) {
    if (device == NULL || out_lba == NULL || out_count == NULL ||
        device->block_size == 0u || device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) != 0u ||
        (base_bytes % device->block_size) != 0u) return false;

    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    uint64_t lba = base_bytes / device->block_size;
    if (count == 0u || count > UINT32_MAX || lba >= device->block_count ||
        count > device->block_count - lba) return false;
    *out_lba = lba;
    *out_count = (uint32_t)count;
    return true;
}

static bool read_superblock(struct aurora_block_device *device, uint64_t base_bytes) {
    uint64_t lba;
    uint32_t count;
    return fs_block_geometry(device, base_bytes, &lba, &count) &&
        block_device_read(device, lba, count, v2tx_superblock);
}

static bool write_superblock(struct aurora_block_device *device, uint64_t base_bytes) {
    uint64_t lba;
    uint32_t count;
    if (device == NULL || device->read_only ||
        !fs_block_geometry(device, base_bytes, &lba, &count)) return false;
    return block_device_write(device, lba, count, v2tx_superblock) &&
        block_device_flush(device);
}

static uint32_t record_checksum(struct v2tx_record_disk *record) {
    uint32_t saved = record->checksum;
    record->checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)record, sizeof(*record));
    record->checksum = saved;
    return checksum;
}

static void update_superblock_checksum(void) {
    uint32_t zero = 0u;
    for (size_t i = 0u; i < sizeof(zero); ++i)
        v2tx_superblock[V2TX_SUPERBLOCK_CHECKSUM_OFFSET + i] = 0u;
    uint32_t checksum = crc32_ieee(v2tx_superblock, sizeof(v2tx_superblock));
    for (size_t i = 0u; i < sizeof(checksum); ++i)
        v2tx_superblock[V2TX_SUPERBLOCK_CHECKSUM_OFFSET + i] =
            (uint8_t)(checksum >> (i * 8u));
}

static bool state_valid(uint32_t state) {
    return state <= (uint32_t)AURORA_FS_V2_TXN_COMMITTED;
}

static bool operation_valid(uint32_t operation) {
    return operation <= (uint32_t)AURORA_FS_V2_TXN_OP_RENAME;
}

static bool record_magic_valid(const struct v2tx_record_disk *record) {
    static const uint8_t magic[8] = {
        V2TX_MAGIC_0, V2TX_MAGIC_1, V2TX_MAGIC_2, V2TX_MAGIC_3,
        V2TX_MAGIC_4, V2TX_MAGIC_5, V2TX_MAGIC_6, V2TX_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i)
        if (record->magic[i] != magic[i]) return false;
    return true;
}

static void set_magic(struct v2tx_record_disk *record) {
    static const uint8_t magic[8] = {
        V2TX_MAGIC_0, V2TX_MAGIC_1, V2TX_MAGIC_2, V2TX_MAGIC_3,
        V2TX_MAGIC_4, V2TX_MAGIC_5, V2TX_MAGIC_6, V2TX_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) record->magic[i] = magic[i];
}

static struct v2tx_record_disk *disk_record(void) {
    return (struct v2tx_record_disk *)(v2tx_superblock + V2TX_RECORD_OFFSET);
}

bool aurora_fs_v2_txn_load(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    struct aurora_fs_v2_txn_record *out_record
) {
    if (out_record == NULL || !read_superblock(device, base_bytes)) return false;
    struct v2tx_record_disk *disk = disk_record();

    zero_bytes(out_record, sizeof(*out_record));
    if (!record_magic_valid(disk)) {
        out_record->state = AURORA_FS_V2_TXN_CLEAN;
        out_record->operation = AURORA_FS_V2_TXN_OP_NONE;
        return true;
    }
    if (disk->version != V2TX_VERSION || !state_valid(disk->state) ||
        !operation_valid(disk->operation) ||
        disk->range_count > AURORA_FS_V2_TXN_RANGE_CAPACITY ||
        disk->checksum != record_checksum(disk)) return false;

    out_record->state = (enum aurora_fs_v2_txn_state)disk->state;
    out_record->operation = (enum aurora_fs_v2_txn_operation)disk->operation;
    out_record->sequence = disk->sequence;
    out_record->inode_index = disk->inode_index;
    out_record->old_root = disk->old_root;
    out_record->new_root = disk->new_root;
    out_record->old_size = disk->old_size;
    out_record->new_size = disk->new_size;
    out_record->range_count = disk->range_count;
    for (uint32_t i = 0u; i < disk->range_count; ++i) {
        out_record->ranges[i].first_block = disk->ranges[i].first_block;
        out_record->ranges[i].block_count = disk->ranges[i].block_count;
    }
    return true;
}

bool aurora_fs_v2_txn_prepare(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    const struct aurora_fs_v2_txn_record *record
) {
    if (device == NULL || device->read_only || record == NULL || record->sequence == 0u ||
        record->state != AURORA_FS_V2_TXN_PREPARED ||
        record->operation == AURORA_FS_V2_TXN_OP_NONE ||
        record->range_count > AURORA_FS_V2_TXN_RANGE_CAPACITY ||
        !read_superblock(device, base_bytes)) return false;

    struct v2tx_record_disk *disk = disk_record();
    zero_bytes(disk, sizeof(*disk));
    set_magic(disk);
    disk->version = V2TX_VERSION;
    disk->state = (uint32_t)AURORA_FS_V2_TXN_PREPARED;
    disk->operation = (uint32_t)record->operation;
    disk->range_count = record->range_count;
    disk->sequence = record->sequence;
    disk->inode_index = record->inode_index;
    disk->old_root = record->old_root;
    disk->new_root = record->new_root;
    disk->old_size = record->old_size;
    disk->new_size = record->new_size;
    for (uint32_t i = 0u; i < record->range_count; ++i) {
        disk->ranges[i].first_block = record->ranges[i].first_block;
        disk->ranges[i].block_count = record->ranges[i].block_count;
    }
    disk->checksum = record_checksum(disk);
    update_superblock_checksum();
    return write_superblock(device, base_bytes);
}

bool aurora_fs_v2_txn_mark_committed(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t expected_sequence
) {
    if (expected_sequence == 0u || !read_superblock(device, base_bytes)) return false;
    struct v2tx_record_disk *disk = disk_record();
    if (!record_magic_valid(disk) || disk->version != V2TX_VERSION ||
        disk->state != (uint32_t)AURORA_FS_V2_TXN_PREPARED ||
        disk->sequence != expected_sequence || disk->checksum != record_checksum(disk)) return false;
    disk->state = (uint32_t)AURORA_FS_V2_TXN_COMMITTED;
    disk->checksum = record_checksum(disk);
    update_superblock_checksum();
    return write_superblock(device, base_bytes);
}

bool aurora_fs_v2_txn_clear(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t expected_sequence
) {
    if (!read_superblock(device, base_bytes)) return false;
    struct v2tx_record_disk *disk = disk_record();
    if (record_magic_valid(disk)) {
        if (disk->version != V2TX_VERSION || disk->checksum != record_checksum(disk) ||
            (expected_sequence != 0u && disk->sequence != expected_sequence)) return false;
    } else if (expected_sequence != 0u) {
        return false;
    }
    zero_bytes(disk, sizeof(*disk));
    update_superblock_checksum();
    return write_superblock(device, base_bytes);
}
