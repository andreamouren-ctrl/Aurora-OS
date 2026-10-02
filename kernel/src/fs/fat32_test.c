#include <stddef.h>
#include <stdint.h>

#include <aurora/block_device.h>
#include <aurora/fat32.h>
#include <aurora/fs_driver.h>
#include <aurora/partition.h>

#define FAT32_TEST_BLOCK_SIZE 4096u
#define FAT32_TEST_TOTAL_SECTORS 66584u

static uint8_t fat32_boot[FAT32_TEST_BLOCK_SIZE];

static void put_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static bool fat32_test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (device == NULL || buffer == NULL || block_count != 1u ||
        lba >= device->block_count) {
        return false;
    }

    uint8_t *out = (uint8_t *)buffer;
    for (size_t i = 0u; i < FAT32_TEST_BLOCK_SIZE; ++i) {
        out[i] = lba == 0u ? fat32_boot[i] : 0u;
    }
    return true;
}

static void build_boot(uint16_t bytes_per_sector) {
    for (size_t i = 0u; i < sizeof(fat32_boot); ++i) {
        fat32_boot[i] = 0u;
    }

    fat32_boot[0u] = 0xEBu;
    fat32_boot[1u] = 0x58u;
    fat32_boot[2u] = 0x90u;
    put_le16(fat32_boot + 11u, bytes_per_sector);
    fat32_boot[13u] = 1u;                 /* sectors per cluster */
    put_le16(fat32_boot + 14u, 32u);      /* reserved sectors */
    fat32_boot[16u] = 2u;                 /* FAT count */
    put_le16(fat32_boot + 17u, 0u);       /* FAT32 root entry count */
    put_le16(fat32_boot + 19u, 0u);       /* use TotSec32 */
    put_le16(fat32_boot + 22u, 0u);       /* FAT16 size */
    put_le32(fat32_boot + 32u, FAT32_TEST_TOTAL_SECTORS);
    put_le32(fat32_boot + 36u, 513u);     /* sectors per FAT */
    put_le32(fat32_boot + 44u, 2u);       /* root cluster */
    fat32_boot[510u] = 0x55u;
    fat32_boot[511u] = 0xAAu;
}

bool fat32_4kn_self_test(void) {
    struct aurora_block_device device = {
        .name = "fat32-4kn-self-test",
        .block_size = FAT32_TEST_BLOCK_SIZE,
        .block_count = FAT32_TEST_TOTAL_SECTORS,
        .read_only = true,
        .context = NULL,
        .read_blocks = fat32_test_read,
        .write_blocks = NULL,
        .flush = NULL
    };

    struct aurora_partition partition = { 0 };
    partition.device = &device;
    partition.scheme = AURORA_PARTITION_SCHEME_WHOLE_DEVICE;
    partition.index = 0u;
    partition.first_lba = 0u;
    partition.block_count = device.block_count;

    const struct aurora_fs_driver *driver = fat32_driver();
    if (driver == NULL || driver->probe == NULL || driver->mount == NULL ||
        driver->stat == NULL) {
        return false;
    }

    build_boot(4096u);
    if (driver->probe(&partition) != AURORA_FS_PROBE_MATCH_READ_ONLY) {
        return false;
    }

    void *context = NULL;
    if (!driver->mount(&partition, &context) || context == NULL) {
        return false;
    }

    struct aurora_fs_stat root;
    if (!driver->stat(context, "/", &root) ||
        root.type != AURORA_FS_ENTRY_DIRECTORY || root.filesystem_id != 2u) {
        return false;
    }

    if (driver->unmount != NULL) {
        driver->unmount(context);
    }

    /* A 512-byte FAT sector cannot be backed by a 4096-byte logical block. */
    build_boot(512u);
    return driver->probe(&partition) == AURORA_FS_PROBE_NO_MATCH;
}
