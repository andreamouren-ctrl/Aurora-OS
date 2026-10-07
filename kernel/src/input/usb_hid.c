#include <stddef.h>
#include <stdint.h>

#include <aurora/input.h>
#include <aurora/usb_hid.h>

static void clear_keyboard(
    struct aurora_usb_hid_keyboard *keyboard
) {
    if (keyboard == NULL) return;

    keyboard->device_id = AURORA_INPUT_DEVICE_UNSPECIFIED;
    keyboard->connected = false;

    for (uint32_t i = 0u;
         i < AURORA_USB_HID_BOOT_KEY_COUNT;
         ++i) {
        keyboard->previous_keys[i] = 0u;
    }
}

static enum aurora_key_code usage_to_key(uint8_t usage) {
    if (usage >= 0x04u && usage <= 0x1Du) {
        return (enum aurora_key_code)(
            AURORA_KEY_A + (usage - 0x04u)
        );
    }

    if (usage >= 0x1Eu && usage <= 0x26u) {
        return (enum aurora_key_code)(
            AURORA_KEY_1 + (usage - 0x1Eu)
        );
    }

    switch (usage) {
        case 0x27u: return AURORA_KEY_0;
        case 0x28u: return AURORA_KEY_ENTER;
        case 0x29u: return AURORA_KEY_ESCAPE;
        case 0x2Au: return AURORA_KEY_BACKSPACE;
        default: return AURORA_KEY_NONE;
    }
}

static bool contains_usage(
    const uint8_t keys[AURORA_USB_HID_BOOT_KEY_COUNT],
    uint8_t usage
) {
    if (usage == 0u) return false;

    for (uint32_t i = 0u;
         i < AURORA_USB_HID_BOOT_KEY_COUNT;
         ++i) {
        if (keys[i] == usage) return true;
    }

    return false;
}

static bool report_has_rollover(const uint8_t report[8]) {
    for (uint32_t i = 2u; i < 8u; ++i) {
        if (report[i] >= 0x01u && report[i] <= 0x03u) {
            return true;
        }
    }

    return false;
}

static bool emit_key(
    const struct aurora_usb_hid_keyboard *keyboard,
    uint8_t usage,
    bool pressed
) {
    enum aurora_key_code key = usage_to_key(usage);

    if (key == AURORA_KEY_NONE) {
        return true;
    }

    const struct aurora_input_event event = {
        .type = AURORA_INPUT_EVENT_KEY,
        .source = AURORA_INPUT_SOURCE_USB_HID,
        .device_id = keyboard->device_id,
        .key = key,
        .pressed = pressed
    };

    return input_push_event(&event);
}

bool usb_hid_keyboard_attach(
    struct aurora_usb_hid_keyboard *keyboard,
    uint64_t device_id
) {
    if (keyboard == NULL ||
        device_id < AURORA_INPUT_DEVICE_USB_BASE ||
        keyboard->connected) {
        return false;
    }

    clear_keyboard(keyboard);
    keyboard->device_id = device_id;

    const struct aurora_input_event added = {
        .type = AURORA_INPUT_EVENT_DEVICE_ADDED,
        .source = AURORA_INPUT_SOURCE_USB_HID,
        .device_id = device_id
    };

    if (!input_push_event(&added)) {
        clear_keyboard(keyboard);
        return false;
    }

    keyboard->connected = true;
    return true;
}

bool usb_hid_keyboard_detach(
    struct aurora_usb_hid_keyboard *keyboard
) {
    if (keyboard == NULL || !keyboard->connected) {
        return false;
    }

    const struct aurora_input_event removed = {
        .type = AURORA_INPUT_EVENT_DEVICE_REMOVED,
        .source = AURORA_INPUT_SOURCE_USB_HID,
        .device_id = keyboard->device_id
    };

    if (!input_push_event(&removed)) {
        return false;
    }

    clear_keyboard(keyboard);
    return true;
}

bool usb_hid_keyboard_process_boot_report(
    struct aurora_usb_hid_keyboard *keyboard,
    const uint8_t report[8]
) {
    if (keyboard == NULL ||
        report == NULL ||
        !keyboard->connected ||
        report_has_rollover(report)) {
        return false;
    }

    const uint8_t *keys = &report[2];

    for (uint32_t i = 0u;
         i < AURORA_USB_HID_BOOT_KEY_COUNT;
         ++i) {
        uint8_t usage = keyboard->previous_keys[i];

        if (usage != 0u &&
            !contains_usage(keys, usage) &&
            !emit_key(keyboard, usage, false)) {
            return false;
        }
    }

    for (uint32_t i = 0u;
         i < AURORA_USB_HID_BOOT_KEY_COUNT;
         ++i) {
        uint8_t usage = keys[i];

        if (usage != 0u &&
            !contains_usage(keyboard->previous_keys, usage) &&
            !emit_key(keyboard, usage, true)) {
            return false;
        }
    }

    for (uint32_t i = 0u;
         i < AURORA_USB_HID_BOOT_KEY_COUNT;
         ++i) {
        keyboard->previous_keys[i] = keys[i];
    }

    return true;
}

bool usb_hid_selftest(void) {
    input_init();

    static struct aurora_usb_hid_keyboard keyboard;
    clear_keyboard(&keyboard);

    const uint64_t device_id =
        AURORA_INPUT_DEVICE_USB_BASE + UINT64_C(7);

    const uint8_t press_a[8] = {
        0u, 0u, 0x04u, 0u, 0u, 0u, 0u, 0u
    };
    const uint8_t release_all[8] = {
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u
    };

    if (!usb_hid_keyboard_attach(&keyboard, device_id) ||
        !usb_hid_keyboard_process_boot_report(
            &keyboard,
            press_a) ||
        !usb_hid_keyboard_process_boot_report(
            &keyboard,
            release_all) ||
        !usb_hid_keyboard_detach(&keyboard)) {
        return false;
    }

    struct aurora_input_event event = {0};

    if (!input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_DEVICE_ADDED ||
        event.source != AURORA_INPUT_SOURCE_USB_HID ||
        event.device_id != device_id) {
        return false;
    }

    if (!input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_KEY ||
        event.device_id != device_id ||
        event.key != AURORA_KEY_A ||
        !event.pressed) {
        return false;
    }

    if (!input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_KEY ||
        event.device_id != device_id ||
        event.key != AURORA_KEY_A ||
        event.pressed) {
        return false;
    }

    if (!input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_DEVICE_REMOVED ||
        event.device_id != device_id ||
        input_poll_event(&event)) {
        return false;
    }

    return true;
}
