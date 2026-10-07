#ifndef AURORA_USB_HID_TRANSPORT_H
#define AURORA_USB_HID_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/usb_hid.h>

#define AURORA_USB_HID_MAX_BINDINGS 16u

typedef uint64_t aurora_usb_hid_binding_handle;

#define AURORA_USB_HID_BINDING_INVALID UINT64_C(0)

enum aurora_usb_hid_protocol {
    AURORA_USB_HID_PROTOCOL_NONE = 0,
    AURORA_USB_HID_PROTOCOL_BOOT_KEYBOARD,
    AURORA_USB_HID_PROTOCOL_BOOT_MOUSE
};

struct aurora_usb_hid_binding {
    enum aurora_usb_hid_protocol protocol;
    uint64_t device_id;
    uint32_t generation;
    bool used;

    union {
        struct aurora_usb_hid_keyboard keyboard;
        struct aurora_usb_hid_mouse mouse;
    } decoder;
};

struct aurora_usb_hid_transport {
    struct aurora_usb_hid_binding bindings[AURORA_USB_HID_MAX_BINDINGS];
    uint64_t next_device_sequence;
    bool initialized;
};

bool usb_hid_transport_init(
    struct aurora_usb_hid_transport *transport
);

bool usb_hid_transport_bind(
    struct aurora_usb_hid_transport *transport,
    enum aurora_usb_hid_protocol protocol,
    aurora_usb_hid_binding_handle *out_handle,
    uint64_t *out_device_id
);

bool usb_hid_transport_selftest(void);

#endif
