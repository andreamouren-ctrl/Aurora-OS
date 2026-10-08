#ifndef AURORA_XHCI_H
#define AURORA_XHCI_H

#include <stdbool.h>
#include <stdint.h>

struct aurora_xhci_controller_state {
    uint8_t context_size;
    uint16_t scratchpad_count;
    uint32_t page_size_mask;
    uint64_t operational_physical;
    uint64_t runtime_physical;
    uint64_t doorbell_physical;
    bool supports_4k_pages;

    uint64_t dcbaa_physical;
    uint64_t scratchpad_array_physical;
    uint64_t command_ring_physical;
    uint64_t event_ring_physical;
    uint64_t erst_physical;

    uint64_t device_context_physical;
    uint64_t input_context_physical;
    uint64_t ep0_ring_physical;
    uint8_t addressed_slot_id;
    uint8_t usb_device_address;
    uint8_t ep0_state;

    uint16_t command_enqueue;
    bool command_cycle;
    uint16_t event_dequeue;
    bool event_cycle;

    bool dma_ready;
    bool running;
};

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

bool xhci_read_controller_state(
    const struct aurora_xhci_probe_result *probe,
    struct aurora_xhci_controller_state *out_state
);

bool xhci_prepare_controller(
    const struct aurora_xhci_probe_result *probe,
    struct aurora_xhci_controller_state *state
);

bool xhci_bootstrap_dma(
    const struct aurora_xhci_probe_result *probe,
    struct aurora_xhci_controller_state *state
);

bool xhci_submit_enable_slot(
    struct aurora_xhci_controller_state *state,
    uint64_t *out_command_trb_physical
);

bool xhci_wait_command_completion(
    struct aurora_xhci_controller_state *state,
    uint64_t command_trb_physical,
    uint8_t *out_slot_id
);

bool xhci_reset_first_connected_port(
    const struct aurora_xhci_probe_result *probe,
    uint8_t *out_port_id,
    uint8_t *out_speed_id
);

bool xhci_prepare_address_device(
    struct aurora_xhci_controller_state *state,
    uint8_t slot_id,
    uint8_t port_id,
    uint8_t speed_id
);

#endif
