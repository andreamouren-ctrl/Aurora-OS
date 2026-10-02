#ifndef AURORA_PCI_H
#define AURORA_PCI_H

#include <stdbool.h>
#include <stdint.h>

struct aurora_pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;
};

uint32_t pci_config_read32(
    uint8_t bus,
    uint8_t slot,
    uint8_t function,
    uint8_t offset
);

void pci_config_write32(
    uint8_t bus,
    uint8_t slot,
    uint8_t function,
    uint8_t offset,
    uint32_t value
);

bool pci_find_class(
    uint8_t class_code,
    uint8_t subclass,
    uint8_t prog_if,
    struct aurora_pci_device *out_device
);

uint32_t pci_read_bar32(
    const struct aurora_pci_device *device,
    uint8_t bar_index
);

bool pci_enable_memory_bus_master(
    const struct aurora_pci_device *device
);

#endif
