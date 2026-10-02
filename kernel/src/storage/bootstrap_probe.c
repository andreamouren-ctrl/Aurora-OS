#include <stddef.h>

#include <aurora/ahci.h>
#include <aurora/ata_pio.h>
#include <aurora/aurora_fs.h>
#include <aurora/block_device.h>
#include <aurora/bootstrap_probe.h>
#include <aurora/exfat.h>
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

static bool verify_vfs_file(
    const char *absolute_path,
    const uint8_t *expected,
    size_t expected_length
) {
    uint8_t buffer[128];
    size_t read = 0u;
    struct aurora_vfs_stat stat;

    if (absolute_path == NULL || expected == NULL ||
        expected_length > sizeof(buffer)) {
        return false;
    }

    if (!vfs_stat(absolute_path, &stat) ||
        stat.type != AURORA_VFS_NODE_FILE ||
        stat.size != expected_length) {
        return false;
    }

    if (!vfs_read_file(absolute_path, buffer, sizeof(buffer), &read)) {
        return false;
    }

    return read == expected_length && bytes_equal(buffer, expected, expected_length);
}

static void verify_aurora_fs_device(struct aurora_block_device *device) {
    if (device == NULL) {
        kernel_panic("AuroraFS device unavailable for common mount");
    }

    struct aurora_partition whole = { 0 };
    whole.device = device;
    whole.scheme = AURORA_PARTITION_SCHEME_WHOLE_DEVICE;
    whole.index = 0u;
    whole.first_lba = 0u;
    whole.block_count = device->block_count;

    struct aurora_fs_mount *mount = NULL;
    if (!fs_mount_partition("/system", &whole, &mount) || mount == NULL ||
        mount->driver != aurora_fs_driver()) {
        kernel_panic("AuroraFS common filesystem mount failed");
    }

    static const uint8_t expected[] = "AURORA-FS-PERSIST";
    if (!verify_vfs_file(
            "/system/aurora.boot-probe",
            expected,
            sizeof(expected) - 1u)) {
        kernel_panic("AuroraFS VFS persistent file verification failed");
    }

    log_line("[aurorafs] mounted at /system via common filesystem framework");
    log_line("[vfs] AuroraFS mounted-path routing verified");
}

static void verify_fat32_partition(const struct aurora_partition *partition) {
    struct aurora_fs_mount *mount = NULL;
    if (!fs_mount_partition("/media/fat32-test", partition, &mount) || mount == NULL) {
        kernel_panic("FAT32 test partition mount failed");
    }

    static const uint8_t expected_short[] = "AURORA-FAT32-EXTERNAL-IMAGE";
    if (!verify_vfs_file(
            "/media/fat32-test/AURORA.TXT",
            expected_short,
            sizeof(expected_short) - 1u)) {
        kernel_panic("FAT32 VFS external image file verification failed");
    }

    static const uint8_t expected_lfn[] = "AURORA-FAT32-LFN-UNICODE";
    if (!verify_vfs_file(
            "/media/fat32-test/Documento Aurora \xC3\xA8.txt",
            expected_lfn,
            sizeof(expected_lfn) - 1u)) {
        kernel_panic("FAT32 VFS long filename verification failed");
    }

    log_line("[fat32] external image mounted read-only");
    log_line("[fat32] external file read verified");
    log_line("[fat32] VFAT long Unicode filename verified");
    log_line("[vfs] FAT32 mounted-path routing verified");
}

static void verify_exfat_partition(const struct aurora_partition *partition) {
    struct aurora_fs_mount *mount = NULL;
    if (!fs_mount_partition("/media/exfat-test", partition, &mount) || mount == NULL) {
        kernel_panic("exFAT test partition mount failed");
    }

    static const uint8_t expected[] = "AURORA-EXFAT-EXTERNAL-IMAGE";
    if (!verify_vfs_file(
            "/media/exfat-test/Grande Aurora.txt",
            expected,
            sizeof(expected) - 1u)) {
        kernel_panic("exFAT VFS external image file verification failed");
    }

    log_line("[exfat] external image mounted read-only");
    log_line("[exfat] external file read verified");
    log_line("[exfat] 64-bit file length path active");
    log_line("[vfs] exFAT mounted-path routing verified");
}

static void bootstrap_foreign_fs_probe(struct aurora_block_device *device) {
    struct aurora_partition partitions[BOOTSTRAP_PARTITION_MAX];
    size_t partition_count = partition_scan(device, partitions, BOOTSTRAP_PARTITION_MAX);

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

        if (match.driver == fat32_driver()) {
            verify_fat32_partition(&partitions[i]);
        } else if (match.driver == exfat_driver()) {
            verify_exfat_partition(&partitions[i]);
        }
    }
}

static void bootstrap_native_fs_probe(
    struct aurora_block_device *device,
    const char *transport
) {
    struct aurora_fs_bootstrap_result fs_result;
    if (!aurora_fs_bootstrap_probe(device, &fs_result)) {
        kernel_panic("AuroraFS persistent bootstrap probe failed");
    }

    log_write("[aurorafs] transport: ");
    log_line(transport);
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

    verify_aurora_fs_device(device);
    bootstrap_foreign_fs_probe(device);
}

void bootstrap_storage_probe_device(
    struct aurora_block_device *device,
    const char *transport
) {
    if (device == NULL || transport == NULL) {
        kernel_panic("Common storage filesystem probe received invalid device");
    }

    bootstrap_native_fs_probe(device, transport);

    log_write("[storage] ");
    log_write(transport);
    log_line(" partition/filesystem/VFS traversal completed");
}

void bootstrap_storage_probe(void) {
    if (!block_device_self_test()) {
        kernel_panic("Block-device abstraction self-test failed");
    }

    log_line("[storage] block-device abstraction + registry/flush self-test passed");
    block_device_registry_init();

    if (!partition_self_test()) {
        kernel_panic("GPT integrity/backup partition self-test failed");
    }

    log_line("[partition] GPT integrity + backup fallback verified on 512 and 4096-byte logical blocks");

    if (!exfat_4kn_self_test()) {
        kernel_panic("exFAT 4096-byte logical-block self-test failed");
    }
    log_line("[exfat] synthetic 4096-byte logical-block probe/mount self-test passed");

    if (!fat32_4kn_self_test()) {
        kernel_panic("FAT32 4096-byte logical-block self-test failed");
    }
    log_line("[fat32] synthetic 4096-byte logical-block probe/mount self-test passed");

    if (!aurora_fs_4kn_self_test()) {
        kernel_panic("AuroraFS 4096-byte logical-block persistence self-test failed");
    }
    log_line("[aurorafs] synthetic 4096-byte logical-block persistence self-test passed");

    fs_driver_registry_init();
    fs_mount_manager_init();

    if (!vfs_init()) {
        kernel_panic("VFS bootstrap initialization failed");
    }

    if (!fs_driver_register(aurora_fs_driver())) {
        kernel_panic("AuroraFS filesystem driver registration failed");
    }
    if (!fs_driver_register(fat32_driver())) {
        kernel_panic("FAT32 filesystem driver registration failed");
    }
    if (!fs_driver_register(exfat_driver())) {
        kernel_panic("exFAT filesystem driver registration failed");
    }

    log_line("[fs] filesystem driver registry initialized");
    log_line("[fs] AuroraFS read-write driver registered");
    log_line("[fs] FAT32 read-only driver registered");
    log_line("[fs] exFAT read-only driver registered");

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

        log_write("[ahci] active SATA ports mask: ");
        log_hex64(ahci.sata_ports_active);
        log_line("");

        if (ahci.sata_ports_active != 0u) {
            struct aurora_ahci_identify_result identify;
            if (!ahci_identify_first(&identify)) {
                kernel_panic("AHCI active SATA port IDENTIFY DEVICE failed");
            }

            log_write("[ahci] IDENTIFY DEVICE passed on port ");
            log_u64(identify.port);
            log_write(" sectors: ");
            log_u64(identify.sector_count);
            log_write(" logical-sector: ");
            log_u64(identify.logical_sector_size);
            log_line("");

            log_write("[ahci] model: ");
            log_line(identify.model);

            if (!ahci_rw_block_device_init()) {
                kernel_panic("AHCI read-write block-device initialization failed");
            }

            struct aurora_block_device *ahci_disk = ahci_rw_block_device();
            if (ahci_disk == NULL || !block_device_register(ahci_disk) ||
                block_device_find("ahci-sata0") != ahci_disk) {
                kernel_panic("AHCI block-device registry integration failed");
            }

            uint8_t first_block[4096];
            if (ahci_disk->block_size > sizeof(first_block) ||
                !block_device_read(ahci_disk, 0u, 1u, first_block)) {
                kernel_panic("AHCI READ DMA EXT LBA0 verification failed");
            }

            log_line("[storage] block device registered: ahci-sata0");
            log_line("[ahci] READ DMA EXT LBA0 via block layer verified");

            if (ahci_rw_signed_probe()) {
                log_line("[ahci] signed WRITE DMA EXT + FLUSH CACHE EXT probe passed and restored");
            } else {
                log_line("[ahci] signed write/flush probe skipped: CI signature absent or verification failed");
            }

            bootstrap_storage_probe_device(ahci_disk, "AHCI");
        }
    } else {
        log_line("[storage] AHCI controller unavailable");
    }

    if (ata_pio_primary_master_init()) {
        struct aurora_block_device *ata = ata_pio_primary_master_device();

        if (ata == NULL || !block_device_register(ata) ||
            block_device_find("ata-primary-master") != ata) {
            kernel_panic("ATA block-device registry integration failed");
        }

        log_line("[storage] block device registered: ata-primary-master");
        log_write("[ata] primary master sectors: ");
        log_u64(ata->block_count);
        log_line("");

        if (ata_pio_ci_probe()) {
            log_line("[ata] signed test disk read/write/flush probe passed");
            bootstrap_storage_probe_device(ata, "ATA PIO");
        } else {
            log_line("[ata] primary master detected; signed write probe skipped");
        }
    } else {
        log_line("[ata] primary PIO disk unavailable");
    }

    if (!vfs_self_test()) {
        kernel_panic("VFS bootstrap self-test failed");
    }

    log_line("[vfs] volatile bootstrap filesystem self-test passed");
}
