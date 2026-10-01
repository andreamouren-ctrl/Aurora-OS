#include <stddef.h>
#include <stdint.h>

#include <aurora/ahci.h>
#include <aurora/pci.h>

#define PCI_CLASS_MASS_STORAGE 0x01u
#define PCI_SUBCLASS_SATA      0x06u
#define PCI_PROGIF_AHCI        0x01u

bool ahci_probe(struct aurora_ahci_probe_result *out_result) {
    struct aurora_pci_device device;

    if (!pci_find_class(
            PCI_CLASS_MASS_STORAGE,
            PCI_SUBCLASS_SATA,
            PCI_PROGIF_AHCI,
            &device)) {
        if (out_result != NULL) {
            out_result->found = false;
            out_result->bus = 0u;
            out_result->slot = 0u;
            out_result->function = 0u;
            out_result->vendor_id = 0u;
            out_result->device_id = 0u;
            out_result->abar_physical = 0u;
        }
        return false;
    }

    uint32_t bar5 = pci_read_bar32(&device, 5u);

    if ((bar5 & 0x1u) != 0u) {
        return false;
    }

    uint64_t abar = (uint64_t)(bar5 & ~0xFu);

    if (out_result != NULL) {
        out_result->found = true;
        out_result->bus = device.bus;
        out_result->slot = device.slot;
        out_result->function = device.function;
        out_result->vendor_id = device.vendor_id;
        out_result->device_id = device.device_id;
        out_result->abar_physical = abar;
    }

    return abar != 0u;
}
