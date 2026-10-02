#include <stddef.h>
#include <stdint.h>

#include <aurora/block_device.h>
#include <aurora/exfat.h>
#include <aurora/fs_driver.h>
#include <aurora/partition.h>

#define EXFAT_TEST_BLOCK_SIZE 4096u
#define EXFAT_TEST_BLOCK_COUNT 16u

static uint8_t exfat_test_storage[EXFAT_TEST_BLOCK_SIZE * EXFAT_TEST_BLOCK_COUNT];

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void put_le64(uint8_t *p, uint64_t value) {
    put_le32(p, (uint32_t)(value & 0xFFFFFFFFu));
    put_le32(p + 4u, (uint32_t)(value >> 32));
}

static bool exfat_test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (device == NULL || buffer == NULL ||
        lba > EXFAT_TEST_BLOCK_COUNT ||
        block_count > EXFAT_TEST_BLOCK_COUNT - lba) {
        return false;
    }

    uint8_t *out = (uint8_t *)buffer;
    size_t offset = (size_t)lba * EXFAT_TEST_BLOCK_SIZE;
    size_t length = (size_t)block_count * EXFAT_TEST_BLOCK_SIZE;

    for (size_t i = 0u; i < length; ++i) {
        out[i] = exfat_test_storage[offset + i];
    }

    return true;
}

static void clear_storage(void) {
    for (size_t i = 0u; i < sizeof(exfat_test_storage); ++i) {
        exfat_test_storage[i] = 0u;
    }
}

static void build_minimal_boot_record(uint8_t sector_shift) {
    clear_storage();

    uint8_t *boot = exfat_test_storage;
    boot[0u] = 0xEBu;
    boot[1u] = 0x76u;
    boot[2u] = 0x90u;

    static const char name[] = "EXFAT   ";
    for (size_t i = 0u; i < 8u; ++i) {
        boot[3u + i] = (uint8_t)name[i];
    }

    put_le64(boot + 64u, 0u);                         /* PartitionOffset */
    put_le64(boot + 72u, EXFAT_TEST_BLOCK_COUNT);    /* VolumeLength */
    put_le32(boot + 80u, 1u);                        /* FatOffset */
    put_le32(boot + 84u, 1u);                        /* FatLength */
    put_le32(boot + 88u, 2u);                        /* ClusterHeapOffset */
    put_le32(boot + 92u, 8u);                        /* ClusterCount */
    put_le32(boot + 96u, 2u);                        /* FirstClusterOfRootDirectory */
    put_le32(boot + 100u, 0x41555234u);              /* VolumeSerialNumber */
    boot[104u] = 0x00u;
    boot[105u] = 0x01u;                              /* FileSystemRevision 1.00 */
    boot[106u] = 0u;
    boot[107u] = 0u;                                 /* VolumeFlags */
    boot[108u] = sector_shift;                       /* BytesPerSectorShift */
    boot[109u] = 0u;                                 /* SectorsPerClusterShift */
    boot[110u] = 1u;                                 /* NumberOfFats */
    boot[111u] = 0x80u;                              /* DriveSelect */
    boot[112u] = 0u;                                 /* PercentInUse */
    boot[510u] = 0x55u;
    boot[511u] = 0xAAu;
}

bool exfat_4kn_self_test(void) {
    struct aurora_block_device device = {
        .name = "exfat-4kn-self-test",
        .block_size = EXFAT_TEST_BLOCK_SIZE,
        .block_count = EXFAT_TEST_BLOCK_COUNT,
        .read_only = true,
        .context = NULL,
        .read_blocks = exfat_test_read,
        .write_blocks = NULL,
        .flush = NULL
    };

    struct aurora_partition partition = { 0 };
    partition.device = &device;
    partition.scheme = AURORA_PARTITION_SCHEME_WHOLE_DEVICE;
    partition.index = 0u;
    partition.first_lba = 0u;
    partition.block_count = EXFAT_TEST_BLOCK_COUNT;

    const struct aurora_fs_driver *driver = exfat_driver();
    if (driver == NULL || driver->probe == NULL || driver->mount == NULL ||
        driver->stat == NULL) {
        return false;
    }

    build_minimal_boot_record(12u);
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

    /* A 512-byte exFAT sector cannot be backed by a 4096-byte logical block. */
    build_minimal_boot_record(9u);
    return driver->probe(&partition) == AURORA_FS_PROBE_NO_MATCH;
}
