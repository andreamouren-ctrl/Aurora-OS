#ifndef AURORA_XHCI_H
#define AURORA_XHCI_H

#include <stdbool.h>
#include <stdint.h>

struct aurora_xhci_probe_result {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;

    uint64_t mmio_physical;
    uint8_t capability_length;
    uint16_t interface_version;
    uint8_t max_device_slots;
    uint16_t max_interrupters;
    uint8_t max_ports;
    uint32_t doorbell_offset;
    uint32_t runtime_offset;

    bool has_msi;
    bool has_msix;
};

bool xhci_probe(struct aurora_xhci_probe_result *out_result);

#endif
