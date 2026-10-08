#ifndef AURORA_XHCI_H
#define AURORA_XHCI_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/usb_hid.h>

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
    uint16_t ep0_enqueue;
    bool ep0_cycle;

    uint64_t hid_ring_physical;
    uint8_t hid_endpoint_id;
    uint16_t hid_enqueue;
    bool hid_cycle;
    bool hid_endpoint_running;
    bool addressed_slot_disabled;

    uint16_t command_enqueue;
    bool command_cycle;
    uint16_t event_dequeue;
    bool event_cycle;

    bool dma_ready;
    bool running;
};

struct aurora_usb_device_descriptor {
    uint16_t usb_version_bcd;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t max_packet_size0;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version_bcd;
    uint8_t manufacturer_string;
    uint8_t product_string;
    uint8_t serial_string;
    uint8_t configuration_count;
};

struct aurora_usb_hid_endpoint_descriptor {
    uint8_t configuration_value;
    uint8_t interface_number;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t endpoint_address;
    uint16_t max_packet_size;
    uint8_t interval;
    uint16_t total_configuration_length;
    uint16_t report_descriptor_length;
};


struct aurora_xhci_hid_device {
    uint8_t port_id;
    uint8_t speed_id;
    uint8_t slot_id;
    struct aurora_usb_device_descriptor device;
    struct aurora_usb_hid_endpoint_descriptor endpoint;
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

bool xhci_disable_slot(
    struct aurora_xhci_controller_state *state,
    uint8_t slot_id
);

bool xhci_release_addressed_device(
    struct aurora_xhci_controller_state *state,
    uint8_t slot_id
);

bool xhci_wait_command_completion(
    struct aurora_xhci_controller_state *state,
    uint64_t command_trb_physical,
    uint8_t *out_slot_id
);

bool xhci_wait_port_status_change(
    struct aurora_xhci_controller_state *state,
    uint8_t expected_port_id,
    uint32_t *out_portsc
);

bool xhci_reset_connected_port_after(
    const struct aurora_xhci_probe_result *probe,
    uint8_t after_port_id,
    uint8_t *out_port_id,
    uint8_t *out_speed_id
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

bool xhci_submit_address_device(
    struct aurora_xhci_controller_state *state,
    uint8_t slot_id,
    uint64_t *out_command_trb_physical
);

bool xhci_validate_addressed_device(
    struct aurora_xhci_controller_state *state,
    uint8_t slot_id
);

bool xhci_control_in(
    struct aurora_xhci_controller_state *state,
    uint8_t request_type,
    uint8_t request,
    uint16_t value,
    uint16_t index,
    void *buffer,
    uint16_t length
);

bool xhci_get_device_descriptor(
    struct aurora_xhci_controller_state *state,
    struct aurora_usb_device_descriptor *out_descriptor
);

bool xhci_find_boot_hid_endpoint(
    struct aurora_xhci_controller_state *state,
    struct aurora_usb_hid_endpoint_descriptor *out_endpoint
);

bool xhci_get_hid_report_layout(
    struct aurora_xhci_controller_state *state,
    const struct aurora_usb_hid_endpoint_descriptor *endpoint,
    struct aurora_usb_hid_mouse_report_layout *out_layout
);

bool xhci_set_configuration_and_boot_protocol(
    struct aurora_xhci_controller_state *state,
    const struct aurora_usb_hid_endpoint_descriptor *endpoint
);

bool xhci_set_hid_report_protocol(
    struct aurora_xhci_controller_state *state,
    const struct aurora_usb_hid_endpoint_descriptor *endpoint
);

bool xhci_configure_hid_interrupt_endpoint(
    struct aurora_xhci_controller_state *state,
    const struct aurora_usb_hid_endpoint_descriptor *endpoint,
    uint8_t speed_id
);

bool xhci_receive_hid_interrupt_report(
    struct aurora_xhci_controller_state *state,
    uint8_t *report,
    uint16_t report_size
);

bool xhci_enumerate_boot_hid_after_port(
    const struct aurora_xhci_probe_result *probe,
    struct aurora_xhci_controller_state *state,
    uint8_t after_port_id,
    struct aurora_xhci_hid_device *out_device
);

#endif
