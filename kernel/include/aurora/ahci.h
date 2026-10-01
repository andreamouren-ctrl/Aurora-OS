#ifndef AURORA_AHCI_H
#define AURORA_AHCI_H

#include <stdbool.h>
#include <stdint.h>

struct aurora_ahci_probe_result {
    bool found;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t abar_physical;
};

bool ahci_probe(struct aurora_ahci_probe_result *out_result);

#endif
