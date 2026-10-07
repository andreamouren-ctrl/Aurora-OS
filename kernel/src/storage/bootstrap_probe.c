#include <stddef.h>

#include <aurora/ahci.h>
#include <aurora/ata_pio.h>
#include <aurora/aurora_fs.h>
#include <aurora/aurora_fs_v2_driver.h>
#include <aurora/aurora_fs_v2_metadata.h>
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
#define AURORA_SYSTEM_MIN_BYTES (8u * 1024u * 1024u)
#define AURORA_SYSTEM_BOOTSTRAP_BYTES (16u * 1024u * 1024u)
#define AURORA_V2_METADATA_TEST_UID 1000u
#define AURORA_V2_METADATA_TEST_GID 100u
#define AURORA_V2_METADATA_TEST_MODE 0640u

static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t length) {
    for (size_t i = 0u; i < length; ++i) {
        if (a[i] != b[i]) return false;
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
    if (absolute_path == NULL || expected == NULL || expected_length > sizeof(buffer))
        return false;
    if (!vfs_stat(absolute_path, &stat) || stat.type != AURORA_VFS_NODE_FILE ||
        stat.size != expected_length) return false;
    if (!vfs_read_file(absolute_path, buffer, sizeof(buffer), &read)) return false;
    return read == expected_length && bytes_equal(buffer, expected, expected_length);
}

static bool choose_system_partition(
    struct aurora_block_device *device,
    struct aurora_partition *out_partition
) {
    if (device == NULL || out_partition == NULL || device->block_size == 0u) return false;

    struct aurora_partition partitions[BOOTSTRAP_PARTITION_MAX];
    size_t count = partition_scan(device, partitions, BOOTSTRAP_PARTITION_MAX);
    if (count == 0u) return false;

    if (count == 1u && partitions[0].scheme == AURORA_PARTITION_SCHEME_WHOLE_DEVICE) {
        *out_partition = partitions[0];
        return device->block_count >= AURORA_SYSTEM_MIN_BYTES / device->block_size;
    }

    uint64_t tail_start = 0u;
    for (size_t i = 0u; i < count; ++i) {
        if (partitions[i].first_lba > UINT64_MAX - partitions[i].block_count) return false;
        uint64_t end = partitions[i].first_lba + partitions[i].block_count;
        if (end > tail_start) tail_start = end;
    }
    if (tail_start >= device->block_count) return false;
    uint64_t tail_blocks = device->block_count - tail_start;
    uint64_t minimum_blocks =
        (AURORA_SYSTEM_MIN_BYTES + device->block_size - 1u) / device->block_size;
    if (tail_blocks < minimum_blocks) return false;

    uint64_t bootstrap_blocks =
        (AURORA_SYSTEM_BOOTSTRAP_BYTES + device->block_size - 1u) / device->block_size;
    if (bootstrap_blocks > tail_blocks) bootstrap_blocks = tail_blocks;

    for (size_t i = 0u; i < sizeof(*out_partition); ++i)
        ((uint8_t *)out_partition)[i] = 0u;
    out_partition->device = device;
    out_partition->scheme = AURORA_PARTITION_SCHEME_MBR;
    out_partition->index = 0u;
    out_partition->first_lba = tail_start;
    out_partition->block_count = bootstrap_blocks;
    return true;
}

static void verify_aurora_fs_v2_metadata_persistence(void) {
    static const char probe_path[] = "/system/aurora.boot-probe";
    struct aurora_vfs_stat stat;
    if (!vfs_stat(probe_path, &stat) || stat.type != AURORA_VFS_NODE_FILE)
        kernel_panic("AuroraFS v2 metadata stat verification failed");

    bool legacy_defaults =
        stat.uid == 0u && stat.gid == 0u &&
        stat.mode == AURORA_FS_V2_MODE_FILE_DEFAULT;

    if (legacy_defaults) {
        if (!vfs_chown(
                probe_path, AURORA_V2_METADATA_TEST_UID,
                AURORA_V2_METADATA_TEST_GID) ||
            !vfs_chmod(probe_path, AURORA_V2_METADATA_TEST_MODE) ||
            !vfs_stat(probe_path, &stat) ||
            stat.uid != AURORA_V2_METADATA_TEST_UID ||
            stat.gid != AURORA_V2_METADATA_TEST_GID ||
            stat.mode != AURORA_V2_METADATA_TEST_MODE ||
            stat.link_count != 1u) {
            kernel_panic("AuroraFS v2 metadata initialization verification failed");
        }
        log_line("[vfs] AuroraFS v2 ownership/mode metadata initialized and reread");
        return;
    }

    if (stat.uid != AURORA_V2_METADATA_TEST_UID ||
        stat.gid != AURORA_V2_METADATA_TEST_GID ||
        stat.mode != AURORA_V2_METADATA_TEST_MODE ||
        stat.link_count != 1u) {
        kernel_panic("AuroraFS v2 metadata persistence verification failed");
    }
    log_line("[vfs] AuroraFS v2 ownership/mode metadata persisted across reboot");
}

static void verify_aurora_fs_v2_mount(const struct aurora_partition *system_partition) {
    struct aurora_fs_mount *mount = NULL;
    if (!fs_mount_partition("/system", system_partition, &mount) || mount == NULL ||
        mount->driver != aurora_fs_v2_driver()) {
        kernel_panic("AuroraFS v2 common filesystem mount failed");
    }

#if AURORA_BOOT_VALIDATION
    static const char probe_path[] = "/system/aurora.boot-probe";
    static const uint8_t expected[] = "AURORA-FS-V2-PERSIST";
    if (!verify_vfs_file(probe_path, expected, sizeof(expected) - 1u)) {
        kernel_panic("AuroraFS v2 VFS persistent file verification failed");
    }

    if (!vfs_fdatasync(probe_path) ||
        !vfs_fsync(probe_path) ||
        !vfs_sync("/system")) {
        kernel_panic("AuroraFS v2 explicit durability sync verification failed");
    }
    log_line("[vfs] AuroraFS v2 fdatasync/fsync/sync flush path verified on persistent /system file");

    verify_aurora_fs_v2_metadata_persistence();

    static const uint8_t mutation_payload[] = "AURORA-V2-VFS-MUTATION";
    if (!vfs_create_file("/system/.v2-mutation-test") ||
        !vfs_write_file(
            "/system/.v2-mutation-test", mutation_payload,
            sizeof(mutation_payload) - 1u) ||
        !vfs_truncate_file("/system/.v2-mutation-test", 9u) ||
        !vfs_rename(
            "/system/.v2-mutation-test", "/system/.v2-mutation-renamed") ||
        !verify_vfs_file(
            "/system/.v2-mutation-renamed", mutation_payload, 9u) ||
        !vfs_remove("/system/.v2-mutation-renamed") ||
        vfs_stat("/system/.v2-mutation-renamed", &(struct aurora_vfs_stat){0})) {
        kernel_panic("AuroraFS v2 mounted-path mutation verification failed");
    }

    if (!vfs_create_directory("/system/.v2-dir-test") ||
        !vfs_remove("/system/.v2-dir-test")) {
        kernel_panic("AuroraFS v2 mounted-path directory mutation verification failed");
    }

    log_line("[vfs] AuroraFS v2 mounted-path mutation routing verified");
#endif

    log_line("[aurorafs-v2] mounted at /system via common filesystem framework");
}

static void verify_fat32_partition(const struct aurora_partition *partition) {
    struct aurora_fs_mount *mount = NULL;
    if (!fs_mount_partition("/media/fat32-test", partition, &mount) || mount == NULL)
        kernel_panic("FAT32 test partition mount failed");

    static const uint8_t expected_short[] = "AURORA-FAT32-EXTERNAL-IMAGE";
    if (!verify_vfs_file(
            "/media/fat32-test/AURORA.TXT", expected_short,
            sizeof(expected_short) - 1u))
        kernel_panic("FAT32 VFS external image file verification failed");

    static const uint8_t expected_lfn[] = "AURORA-FAT32-LFN-UNICODE";
    if (!verify_vfs_file(
            "/media/fat32-test/Documento Aurora \xC3\xA8.txt", expected_lfn,
            sizeof(expected_lfn) - 1u))
        kernel_panic("FAT32 VFS long filename verification failed");

    log_line("[fat32] external image mounted read-only");
    log_line("[fat32] external file read verified");
    log_line("[fat32] VFAT long Unicode filename verified");
    log_line("[vfs] FAT32 mounted-path routing verified");
}

static void verify_exfat_partition(const struct aurora_partition *partition) {
    struct aurora_fs_mount *mount = NULL;
    if (!fs_mount_partition("/media/exfat-test", partition, &mount) || mount == NULL)
        kernel_panic("exFAT test partition mount failed");

    static const uint8_t expected[] = "AURORA-EXFAT-EXTERNAL-IMAGE";
    if (!verify_vfs_file(
            "/media/exfat-test/Grande Aurora.txt", expected,
            sizeof(expected) - 1u))
        kernel_panic("exFAT VFS external image file verification failed");

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
        if (!fs_driver_detect(&partitions[i], &match) || match.driver == NULL) continue;

        log_write("[fs] detected ");
        log_write(match.driver->name);
        log_write(" on partition ");
        log_u64(partitions[i].index);
        log_line("");

        if (match.driver == fat32_driver()) verify_fat32_partition(&partitions[i]);
        else if (match.driver == exfat_driver()) verify_exfat_partition(&partitions[i]);
    }
}

static void bootstrap_native_fs_probe(
    struct aurora_block_device *device,
    const char *transport
) {
    struct aurora_partition system_partition;
    if (!choose_system_partition(device, &system_partition))
        kernel_panic("AuroraFS v2 system range unavailable");

    bool formatted = false;
    if (!aurora_fs_v2_prepare_system_partition(&system_partition, &formatted))
        kernel_panic("AuroraFS v2 system filesystem prepare/recovery failed");

    log_write("[aurorafs-v2] transport: ");
    log_line(transport);
    log_write("[aurorafs-v2] range first LBA: ");
    log_u64(system_partition.first_lba);
    log_write(" blocks: ");
    log_u64(system_partition.block_count);
    log_line("");

    if (formatted) {
        log_line("[aurorafs-v2] formatted system filesystem");
        log_line("[aurorafs-v2] persistent file created");
    } else {
        log_line("[aurorafs-v2] persistent file reopened");
    }

    verify_aurora_fs_v2_mount(&system_partition);
#if AURORA_BOOT_VALIDATION
    bootstrap_foreign_fs_probe(device);
#endif
}

void bootstrap_storage_probe_device(
    struct aurora_block_device *device,
    const char *transport
) {
    if (device == NULL || transport == NULL)
        kernel_panic("Common storage filesystem probe received invalid device");

    bootstrap_native_fs_probe(device, transport);

    log_write("[storage] ");
    log_write(transport);
    log_line(" partition/filesystem/VFS traversal completed");
}

void bootstrap_storage_probe(void) {
#if AURORA_BOOT_VALIDATION
    if (!block_device_self_test())
        kernel_panic("Block-device abstraction self-test failed");
    log_line("[storage] block-device abstraction + registry/flush self-test passed");

    if (!partition_self_test())
        kernel_panic("GPT integrity/backup partition self-test failed");
    log_line("[partition] GPT integrity + backup fallback verified on 512 and 4096-byte logical blocks");

    if (!exfat_4kn_self_test())
        kernel_panic("exFAT 4096-byte logical-block self-test failed");
    log_line("[exfat] synthetic 4096-byte logical-block probe/mount self-test passed");

    if (!fat32_4kn_self_test())
        kernel_panic("FAT32 4096-byte logical-block self-test failed");
    log_line("[fat32] synthetic 4096-byte logical-block probe/mount self-test passed");

    if (!aurora_fs_4kn_self_test())
        kernel_panic("AuroraFS regression self-test failed");
    log_line("[aurorafs] legacy + v2 filesystem regression self-tests passed");
#endif

    block_device_registry_init();
    fs_driver_registry_init();
    fs_mount_manager_init();
    if (!vfs_init()) kernel_panic("VFS bootstrap initialization failed");

    if (!fs_driver_register(aurora_fs_v2_driver()))
        kernel_panic("AuroraFS v2 filesystem driver registration failed");
    if (!fs_driver_register(fat32_driver()))
        kernel_panic("FAT32 filesystem driver registration failed");
    if (!fs_driver_register(exfat_driver()))
        kernel_panic("exFAT filesystem driver registration failed");

    log_line("[fs] filesystem driver registry initialized");
    log_line("[fs] AuroraFS v2 read-write driver registered");
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
            if (!ahci_identify_first(&identify))
                kernel_panic("AHCI active SATA port IDENTIFY DEVICE failed");
            log_write("[ahci] IDENTIFY DEVICE passed on port ");
            log_u64(identify.port);
            log_write(" sectors: ");
            log_u64(identify.sector_count);
            log_write(" logical-sector: ");
            log_u64(identify.logical_sector_size);
            log_line("");
            log_write("[ahci] model: ");
            log_line(identify.model);

            if (!ahci_rw_block_device_init())
                kernel_panic("AHCI read-write block-device initialization failed");
            struct aurora_block_device *ahci_disk = ahci_rw_block_device();
            if (ahci_disk == NULL || !block_device_register(ahci_disk) ||
                block_device_find("ahci-sata0") != ahci_disk)
                kernel_panic("AHCI block-device registry integration failed");

            log_line("[storage] block device registered: ahci-sata0");

#if AURORA_BOOT_VALIDATION
            uint8_t first_block[4096];
            if (ahci_disk->block_size > sizeof(first_block) ||
                !block_device_read(ahci_disk, 0u, 1u, first_block))
                kernel_panic("AHCI READ DMA EXT LBA0 verification failed");

            log_line("[ahci] READ DMA EXT LBA0 via block layer verified");
            if (ahci_rw_signed_probe())
                log_line("[ahci] signed WRITE DMA EXT + FLUSH CACHE EXT probe passed and restored");
            else
                log_line("[ahci] signed write/flush probe skipped: CI signature absent or verification failed");
#endif

            bootstrap_storage_probe_device(ahci_disk, "AHCI");
        }
    } else {
        log_line("[storage] AHCI controller unavailable");
    }

    if (ata_pio_primary_master_init()) {
        struct aurora_block_device *ata = ata_pio_primary_master_device();
        if (ata == NULL || !block_device_register(ata) ||
            block_device_find("ata-primary-master") != ata)
            kernel_panic("ATA block-device registry integration failed");

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

#if AURORA_BOOT_VALIDATION
    if (!vfs_self_test()) kernel_panic("VFS bootstrap self-test failed");
    log_line("[vfs] volatile bootstrap filesystem self-test passed");
#endif
}
