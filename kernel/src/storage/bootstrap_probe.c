#include <aurora/ahci.h>
#include <aurora/ata_pio.h>
#include <aurora/block_device.h>
#include <aurora/bootstrap_probe.h>
#include <aurora/log.h>
#include <aurora/panic.h>
#include <aurora/vfs.h>

void bootstrap_storage_probe(void) {
    if (!block_device_self_test()) {
        kernel_panic("Block-device abstraction self-test failed");
    }

    log_line("[storage] block-device abstraction self-test passed");

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
     * ATA PIO is currently a bounded compatibility/test path. The driver may
     * detect and read a primary-master disk, but its write verification is
     * gated inside ata_pio_ci_probe() by the explicit
     * AURORA-STORAGE-TEST-V1 signature in LBA 0. Ordinary disks are never
     * modified by this bootstrap probe.
     */
    if (ata_pio_primary_master_init()) {
        struct aurora_block_device *ata = ata_pio_primary_master_device();

        log_write("[ata] primary master sectors: ");
        log_u64(ata != NULL ? ata->block_count : 0u);
        log_line("");

        if (ata_pio_ci_probe()) {
            log_line("[ata] signed test disk read/write probe passed");
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
