#include <stddef.h>
#include <stdint.h>

#include <aurora/block_device.h>
#include <aurora/partition.h>

#define TEST_BLOCK_SIZE 512u
#define TEST_BLOCK_COUNT 64u
#define TEST_PRIMARY_HEADER_LBA 1u
#define TEST_PRIMARY_ENTRIES_LBA 2u
#define TEST_BACKUP_ENTRIES_LBA 62u
#define TEST_BACKUP_HEADER_LBA 63u
#define TEST_ENTRY_COUNT 4u
#define TEST_ENTRY_SIZE 128u
#define TEST_FIRST_USABLE_LBA 3u
#define TEST_LAST_USABLE_LBA 61u
#define TEST_PARTITION_FIRST_LBA 10u
#define TEST_PARTITION_LAST_LBA 20u

struct partition_test_context {
    uint8_t *storage;
    size_t size;
};

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    for (size_t i = 0u; i < length; ++i) {
        bytes[i] = 0u;
    }
}

static void write_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8u) & 0xFFu);
    p[2] = (uint8_t)((value >> 16u) & 0xFFu);
    p[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void write_le64(uint8_t *p, uint64_t value) {
    write_le32(p, (uint32_t)(value & 0xFFFFFFFFu));
    write_le32(p + 4u, (uint32_t)(value >> 32u));
}

static uint32_t crc32_update(
    uint32_t crc,
    const uint8_t *data,
    size_t length
) {
    for (size_t i = 0u; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
    return ~crc32_update(0xFFFFFFFFu, data, length);
}

static bool test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    struct partition_test_context *context =
        (struct partition_test_context *)device->context;

    if (context == NULL || context->storage == NULL || buffer == NULL ||
        lba >= device->block_count ||
        (uint64_t)block_count > device->block_count - lba) {
        return false;
    }

    size_t offset = (size_t)lba * device->block_size;
    size_t length = (size_t)block_count * device->block_size;
    if (offset > context->size || length > context->size - offset) {
        return false;
    }

    uint8_t *destination = (uint8_t *)buffer;
    for (size_t i = 0u; i < length; ++i) {
        destination[i] = context->storage[offset + i];
    }
    return true;
}

static void build_protective_mbr(uint8_t *storage) {
    uint8_t *mbr = storage;
    mbr[446u + 4u] = 0xEEu;
    write_le32(mbr + 446u + 8u, 1u);
    write_le32(mbr + 446u + 12u, TEST_BLOCK_COUNT - 1u);
    mbr[510u] = 0x55u;
    mbr[511u] = 0xAAu;
}

static void build_entry_array(uint8_t *entries) {
    zero_bytes(entries, TEST_BLOCK_SIZE);

    entries[0u] = 0xA1u;
    entries[16u] = 0xB2u;
    write_le64(entries + 32u, TEST_PARTITION_FIRST_LBA);
    write_le64(entries + 40u, TEST_PARTITION_LAST_LBA);

    static const char name[] = "AURORA_TEST";
    for (size_t i = 0u; i < sizeof(name) - 1u; ++i) {
        entries[56u + i * 2u] = (uint8_t)name[i];
        entries[56u + i * 2u + 1u] = 0u;
    }
}

static void build_gpt_header(
    uint8_t *header,
    uint64_t current_lba,
    uint64_t backup_lba,
    uint64_t entries_lba,
    uint32_t entries_crc
) {
    zero_bytes(header, TEST_BLOCK_SIZE);

    static const uint8_t signature[8] = {'E','F','I',' ','P','A','R','T'};
    for (size_t i = 0u; i < sizeof(signature); ++i) {
        header[i] = signature[i];
    }

    write_le32(header + 8u, 0x00010000u);
    write_le32(header + 12u, 92u);
    write_le32(header + 16u, 0u);
    write_le64(header + 24u, current_lba);
    write_le64(header + 32u, backup_lba);
    write_le64(header + 40u, TEST_FIRST_USABLE_LBA);
    write_le64(header + 48u, TEST_LAST_USABLE_LBA);
    header[56u] = 0x5Au;
    write_le64(header + 72u, entries_lba);
    write_le32(header + 80u, TEST_ENTRY_COUNT);
    write_le32(header + 84u, TEST_ENTRY_SIZE);
    write_le32(header + 88u, entries_crc);

    uint32_t header_crc = crc32_bytes(header, 92u);
    write_le32(header + 16u, header_crc);
}

static bool expect_test_partition(
    struct aurora_block_device *device
) {
    struct aurora_partition partitions[4];
    size_t count = partition_scan(device, partitions, 4u);

    return count == 1u &&
        partitions[0].scheme == AURORA_PARTITION_SCHEME_GPT &&
        partitions[0].index == 1u &&
        partitions[0].first_lba == TEST_PARTITION_FIRST_LBA &&
        partitions[0].block_count ==
            TEST_PARTITION_LAST_LBA - TEST_PARTITION_FIRST_LBA + 1u;
}

bool partition_self_test(void) {
    static uint8_t storage[TEST_BLOCK_SIZE * TEST_BLOCK_COUNT];
    zero_bytes(storage, sizeof(storage));

    build_protective_mbr(storage);

    uint8_t *primary_entries =
        storage + TEST_PRIMARY_ENTRIES_LBA * TEST_BLOCK_SIZE;
    uint8_t *backup_entries =
        storage + TEST_BACKUP_ENTRIES_LBA * TEST_BLOCK_SIZE;

    build_entry_array(primary_entries);
    for (size_t i = 0u; i < TEST_BLOCK_SIZE; ++i) {
        backup_entries[i] = primary_entries[i];
    }

    uint32_t entries_crc = crc32_bytes(primary_entries, TEST_BLOCK_SIZE);

    build_gpt_header(
        storage + TEST_PRIMARY_HEADER_LBA * TEST_BLOCK_SIZE,
        TEST_PRIMARY_HEADER_LBA,
        TEST_BACKUP_HEADER_LBA,
        TEST_PRIMARY_ENTRIES_LBA,
        entries_crc
    );
    build_gpt_header(
        storage + TEST_BACKUP_HEADER_LBA * TEST_BLOCK_SIZE,
        TEST_BACKUP_HEADER_LBA,
        TEST_PRIMARY_HEADER_LBA,
        TEST_BACKUP_ENTRIES_LBA,
        entries_crc
    );

    struct partition_test_context context = {
        .storage = storage,
        .size = sizeof(storage)
    };

    struct aurora_block_device device = {
        .name = "partition-gpt-self-test",
        .block_size = TEST_BLOCK_SIZE,
        .block_count = TEST_BLOCK_COUNT,
        .read_only = true,
        .context = &context,
        .read_blocks = test_read,
        .write_blocks = NULL
    };

    if (!expect_test_partition(&device)) {
        return false;
    }

    storage[TEST_PRIMARY_HEADER_LBA * TEST_BLOCK_SIZE + 16u] ^= 0x01u;
    if (!expect_test_partition(&device)) {
        return false;
    }

    backup_entries[0u] ^= 0x01u;

    struct aurora_partition partitions[2];
    size_t count = partition_scan(&device, partitions, 2u);
    if (count != 1u ||
        partitions[0].scheme != AURORA_PARTITION_SCHEME_WHOLE_DEVICE) {
        return false;
    }

    return true;
}
