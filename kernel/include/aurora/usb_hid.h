#ifndef AURORA_USB_HID_H
#define AURORA_USB_HID_H

#include <stdbool.h>
#include <stdint.h>

#define AURORA_USB_HID_BOOT_KEY_COUNT 6u

struct aurora_usb_hid_keyboard {
    uint64_t device_id;
    uint8_t previous_keys[AURORA_USB_HID_BOOT_KEY_COUNT];
    bool connected;
};

struct aurora_usb_hid_mouse {
    uint64_t device_id;
    uint8_t buttons;
    bool connected;
};

bool usb_hid_keyboard_attach(
    struct aurora_usb_hid_keyboard *keyboard,
    uint64_t device_id
);

bool usb_hid_keyboard_detach(
    struct aurora_usb_hid_keyboard *keyboard
);

bool usb_hid_keyboard_process_boot_report(
    struct aurora_usb_hid_keyboard *keyboard,
    const uint8_t report[8]
);

bool usb_hid_mouse_attach(
    struct aurora_usb_hid_mouse *mouse,
    uint64_t device_id
);

bool usb_hid_mouse_detach(
    struct aurora_usb_hid_mouse *mouse
);

bool usb_hid_mouse_process_boot_report(
    struct aurora_usb_hid_mouse *mouse,
    const uint8_t report[4]
);

bool usb_hid_selftest(void);

#endif
