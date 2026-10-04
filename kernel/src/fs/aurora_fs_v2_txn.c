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
#define V2TX_VERSION 2u
#define V2TX_SUPERBLOCK_CHECKSUM_OFFSET 96u
#define V2TX_RECORD_OFFSET 128u
#define V2TX_RECORD_SIZE 768u

struct v2tx_range_disk {
    uint64_t first_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2tx_namespace_slot_disk {
    uint64_t record_index;
    uint8_t before[AURORA_FS_V2_TXN_NAMESPACE_RECORD_SIZE];
    uint8_t after[AURORA_FS_V2_TXN_NAMESPACE_RECORD_SIZE];
} __attribute__((packed));

struct v2tx_record_disk {
    uint8_t magic[8];
    uint32_t version;
    uint32_t state;
    uint32_t operation;
    uint32_t rollback_range_count;
    uint32_t cleanup_range_count;
    uint64_t sequence;
    uint64_t inode_index;
    uint64_t old_root;
    uint64_t new_root;
    uint64_t old_size;
    uint64_t new_size;
    struct v2tx_range_disk rollback_ranges[AURORA_FS_V2_TXN_ROLLBACK_RANGE_CAPACITY];
    struct v2tx_range_disk cleanup_ranges[AURORA_FS_V2_TXN_CLEANUP_RANGE_CAPACITY];
    uint64_t parent_inode_index;
    uint64_t child_inode_index;
    uint64_t child_object_id;
    uint32_t namespace_slot_count;
    uint32_t namespace_flags;
    struct v2tx_namespace_slot_disk namespace_slots[AURORA_FS_V2_TXN_NAMESPACE_SLOT_CAPACITY];
    uint32_t checksum;
} __attribute__((packed));

static uint8_t v2tx_superblock[AURORA_FS_V2_FS_BLOCK_SIZE];

_Static_assert(sizeof(struct v2tx_record_disk) == V2TX_RECORD_SIZE,
               "AuroraFS v2 transaction record must remain 768 bytes");
_Static_assert(V2TX_RECORD_OFFSET + V2TX_RECORD_SIZE <= AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 transaction record must fit in superblock reserved area");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t i = 0u; i < length; ++i) out[i] = in[i];
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
        disk->rollback_range_count > AURORA_FS_V2_TXN_ROLLBACK_RANGE_CAPACITY ||
        disk->cleanup_range_count > AURORA_FS_V2_TXN_CLEANUP_RANGE_CAPACITY ||
        disk->namespace_slot_count > AURORA_FS_V2_TXN_NAMESPACE_SLOT_CAPACITY ||
        disk->checksum != record_checksum(disk)) return false;

    out_record->state = (enum aurora_fs_v2_txn_state)disk->state;
    out_record->operation = (enum aurora_fs_v2_txn_operation)disk->operation;
    out_record->sequence = disk->sequence;
    out_record->inode_index = disk->inode_index;
    out_record->old_root = disk->old_root;
    out_record->new_root = disk->new_root;
    out_record->old_size = disk->old_size;
    out_record->new_size = disk->new_size;
    out_record->rollback_range_count = disk->rollback_range_count;
    out_record->cleanup_range_count = disk->cleanup_range_count;
    for (uint32_t i = 0u; i < disk->rollback_range_count; ++i) {
        out_record->rollback_ranges[i].first_block = disk->rollback_ranges[i].first_block;
        out_record->rollback_ranges[i].block_count = disk->rollback_ranges[i].block_count;
    }
    for (uint32_t i = 0u; i < disk->cleanup_range_count; ++i) {
        out_record->cleanup_ranges[i].first_block = disk->cleanup_ranges[i].first_block;
        out_record->cleanup_ranges[i].block_count = disk->cleanup_ranges[i].block_count;
    }

    out_record->parent_inode_index = disk->parent_inode_index;
    out_record->child_inode_index = disk->child_inode_index;
    out_record->child_object_id = disk->child_object_id;
    out_record->namespace_slot_count = disk->namespace_slot_count;
    out_record->namespace_flags = disk->namespace_flags;
    for (uint32_t i = 0u; i < disk->namespace_slot_count; ++i) {
        out_record->namespace_slots[i].record_index = disk->namespace_slots[i].record_index;
        copy_bytes(out_record->namespace_slots[i].before, disk->namespace_slots[i].before,
                   AURORA_FS_V2_TXN_NAMESPACE_RECORD_SIZE);
        copy_bytes(out_record->namespace_slots[i].after, disk->namespace_slots[i].after,
                   AURORA_FS_V2_TXN_NAMESPACE_RECORD_SIZE);
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
        record->rollback_range_count > AURORA_FS_V2_TXN_ROLLBACK_RANGE_CAPACITY ||
        record->cleanup_range_count > AURORA_FS_V2_TXN_CLEANUP_RANGE_CAPACITY ||
        record->namespace_slot_count > AURORA_FS_V2_TXN_NAMESPACE_SLOT_CAPACITY ||
        !read_superblock(device, base_bytes)) return false;

    struct v2tx_record_disk *disk = disk_record();
    zero_bytes(disk, sizeof(*disk));
    set_magic(disk);
    disk->version = V2TX_VERSION;
    disk->state = (uint32_t)AURORA_FS_V2_TXN_PREPARED;
    disk->operation = (uint32_t)record->operation;
    disk->rollback_range_count = record->rollback_range_count;
    disk->cleanup_range_count = record->cleanup_range_count;
    disk->sequence = record->sequence;
    disk->inode_index = record->inode_index;
    disk->old_root = record->old_root;
    disk->new_root = record->new_root;
    disk->old_size = record->old_size;
    disk->new_size = record->new_size;
    for (uint32_t i = 0u; i < record->rollback_range_count; ++i) {
        disk->rollback_ranges[i].first_block = record->rollback_ranges[i].first_block;
        disk->rollback_ranges[i].block_count = record->rollback_ranges[i].block_count;
    }
    for (uint32_t i = 0u; i < record->cleanup_range_count; ++i) {
        disk->cleanup_ranges[i].first_block = record->cleanup_ranges[i].first_block;
        disk->cleanup_ranges[i].block_count = record->cleanup_ranges[i].block_count;
    }

    disk->parent_inode_index = record->parent_inode_index;
    disk->child_inode_index = record->child_inode_index;
    disk->child_object_id = record->child_object_id;
    disk->namespace_slot_count = record->namespace_slot_count;
    disk->namespace_flags = record->namespace_flags;
    for (uint32_t i = 0u; i < record->namespace_slot_count; ++i) {
        disk->namespace_slots[i].record_index = record->namespace_slots[i].record_index;
        copy_bytes(disk->namespace_slots[i].before, record->namespace_slots[i].before,
                   AURORA_FS_V2_TXN_NAMESPACE_RECORD_SIZE);
        copy_bytes(disk->namespace_slots[i].after, record->namespace_slots[i].after,
                   AURORA_FS_V2_TXN_NAMESPACE_RECORD_SIZE);
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
