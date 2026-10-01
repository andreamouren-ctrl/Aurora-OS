#include <aurora/ahci.h>
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

    if (!ahci_probe(&ahci)) {
        kernel_panic("No AHCI SATA controller discovered on PCI");
    }

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

    if (!vfs_init()) {
        kernel_panic("VFS bootstrap initialization failed");
    }

    if (!vfs_self_test()) {
        kernel_panic("VFS bootstrap self-test failed");
    }

    log_line("[vfs] volatile bootstrap filesystem self-test passed");
}
