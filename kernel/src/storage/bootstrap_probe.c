#include <stddef.h>

#include <aurora/ahci.h>
#include <aurora/ata_pio.h>
#include <aurora/aurora_fs.h>
#include <aurora/block_device.h>
#include <aurora/bootstrap_probe.h>
#include <aurora/fat32.h>
#include <aurora/fs_driver.h>
#include <aurora/fs_mount.h>
#include <aurora/log.h>
#include <aurora/panic.h>
#include <aurora/partition.h>
#include <aurora/vfs.h>

#define BOOTSTRAP_PARTITION_MAX 8u

static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t length) {
    for (size_t i = 0u; i < length; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static bool verify_file(
    const struct aurora_fs_mount *mount,
    const char *path,
    const uint8_t *expected,
    size_t expected_length
) {
    uint8_t buffer[96];
    size_t read = 0u;

    if (mount == NULL || mount->driver == NULL || mount->driver->read == NULL ||
        expected_length > sizeof(buffer)) {
        return false;
    }

    if (!mount->driver->read(
            mount->context,
            path,
            0u,
            buffer,
            sizeof(buffer),
            &read)) {
        return false;
    }

    return read == expected_length && bytes_equal(buffer, expected, expected_length);
}

static void bootstrap_fat32_probe(struct aurora_block_device *device) {
    struct aurora_partition partitions[BOOTSTRAP_PARTITION_MAX];
    size_t partition_count = partition_scan(
        device,
        partitions,
        BOOTSTRAP_PARTITION_MAX
    );

    log_write("[partition] discovered: ");
    log_u64(partition_count);
    log_line("");

    for (size_t i = 0u; i < partition_count; ++i) {
        struct aurora_fs_match match;
        if (!fs_driver_detect(&partitions[i], &match) || match.driver == NULL) {
            continue;
        }

        log_write("[fs] detected ");
        log_write(match.driver->name);
        log_write(" on partition ");
        log_u64(partitions[i].index);
        log_line("");

        if (match.driver != fat32_driver()) {
            continue;
        }

        struct aurora_fs_mount *mount = NULL;
        if (!fs_mount_partition("/media/fat32-test", &partitions[i], &mount) ||
            mount == NULL) {
            kernel_panic("FAT32 test partition mount failed");
        }

        static const uint8_t expected_short[] = "AURORA-FAT32-EXTERNAL-IMAGE";
        if (!verify_file(
                mount,
                "/AURORA.TXT",
                expected_short,
                sizeof(expected_short) - 1u)) {
            kernel_panic("FAT32 external image file verification failed");
        }

        static const uint8_t expected_lfn[] = "AURORA-FAT32-LFN-UNICODE";
        if (!verify_file(
                mount,
                "/Documento Aurora \xC3\xA8.txt",
                expected_lfn,
                sizeof(expected_lfn) - 1u)) {
            kernel_panic("FAT32 VFAT long filename verification failed");
        }

        log_line("[fat32] external image mounted read-only");
        log_line("[fat32] external file read verified");
        log_line("[fat32] VFAT long Unicode filename verified");
        return;
    }
}

void bootstrap_storage_probe(void) {
    if (!block_device_self_test()) {
        kernel_panic("Block-device abstraction self-test failed");
    }

    log_line("[storage] block-device abstraction self-test passed");

    fs_driver_registry_init();
    fs_mount_manager_init();

    if (!fs_driver_register(fat32_driver())) {
        kernel_panic("FAT32 filesystem driver registration failed");
    }

    log_line("[fs] filesystem driver registry initialized");
    log_line("[fs] FAT32 read-only driver registered");

    struct aurora_ahci_probe_result ahci;

    if (ahci_probe(&ahci)) {
        log_write("[storage] AHCI controller PCI ");
        log_u64(ahci.bus);
        log_putc(':');
        log_u64(ahci.slot);
        log_putc('.');
        log_u64(ahci.function);
        log_write(" vendor/device ");
        log_hex64(((uint64_t)ahci.vendor_id << 16) | ahci.device_id);
        log_line("");

        log_write("[storage] AHCI ABAR physical: ");
        log_hex64(ahci.abar_physical);
        log_line("");
    } else {
        log_line("[storage] AHCI controller unavailable");
    }

    /*
     * ATA PIO is currently a bounded compatibility/test path. Writes remain
     * gated by the explicit AURORA-STORAGE-TEST-V1 signature in the MBR boot
     * area. The CI disk may also contain a real partition table and FAT32
     * volume beginning well after the low bootstrap sectors.
     */
    if (ata_pio_primary_master_init()) {
        struct aurora_block_device *ata = ata_pio_primary_master_device();

        log_write("[ata] primary master sectors: ");
        log_u64(ata != NULL ? ata->block_count : 0u);
        log_line("");

        if (ata_pio_ci_probe()) {
            log_line("[ata] signed test disk read/write probe passed");

            struct aurora_fs_bootstrap_result fs_result;
            if (!aurora_fs_bootstrap_probe(ata, &fs_result)) {
                kernel_panic("AuroraFS persistent bootstrap probe failed");
            }

            log_write("[aurorafs] generation: ");
            log_u64(fs_result.generation);
            log_line("");

            if (fs_result.formatted) {
                log_line("[aurorafs] formatted bootstrap filesystem");
            }

            if (fs_result.reopened_existing_file) {
                log_line("[aurorafs] persistent file reopened");
            } else {
                log_line("[aurorafs] persistent file created");
            }

            bootstrap_fat32_probe(ata);
        } else {
            log_line("[ata] primary master detected; signed write probe skipped");
        }
    } else {
        log_line("[ata] primary PIO disk unavailable");
    }

    if (!vfs_init()) {
        kernel_panic("VFS bootstrap initialization failed");
    }

    if (!vfs_self_test()) {
        kernel_panic("VFS bootstrap self-test failed");
    }

    log_line("[vfs] volatile bootstrap filesystem self-test passed");
}
