#ifndef AURORA_AHCI_H
#define AURORA_AHCI_H

#include <stdbool.h>
#include <stdint.h>

struct aurora_ahci_probe_result {
    bool found;
    bool mmio_ready;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t abar_physical;
    uint32_t capabilities;
    uint32_t version;
    uint32_t ports_implemented;
    uint32_t sata_ports_active;
};

bool ahci_probe(struct aurora_ahci_probe_result *out_result);

#endif
