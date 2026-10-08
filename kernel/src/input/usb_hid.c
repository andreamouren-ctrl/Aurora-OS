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

    /*
     * A disappearing keyboard must not leave logical keys stuck down. Clear
     * each successfully published release immediately so a retry cannot emit
     * duplicate releases after queue backpressure.
     */
    for (uint32_t i = 0u;
         i < AURORA_USB_HID_BOOT_KEY_COUNT;
         ++i) {
        uint8_t usage = keyboard->previous_keys[i];

        if (usage == 0u) continue;

        if (!emit_key(keyboard, usage, false)) {
            return false;
        }

        keyboard->previous_keys[i] = 0u;
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


static void clear_mouse(
    struct aurora_usb_hid_mouse *mouse
) {
    if (mouse == NULL) return;
    mouse->device_id = AURORA_INPUT_DEVICE_UNSPECIFIED;
    mouse->buttons = 0u;
    mouse->connected = false;
}

static bool emit_mouse_button(
    const struct aurora_usb_hid_mouse *mouse,
    enum aurora_pointer_button button,
    bool pressed
) {
    const struct aurora_input_event event = {
        .type = AURORA_INPUT_EVENT_POINTER_BUTTON,
        .source = AURORA_INPUT_SOURCE_USB_HID,
        .device_id = mouse->device_id,
        .button = button,
        .pressed = pressed
    };

    return input_push_event(&event);
}

bool usb_hid_mouse_attach(
    struct aurora_usb_hid_mouse *mouse,
    uint64_t device_id
) {
    if (mouse == NULL ||
        device_id < AURORA_INPUT_DEVICE_USB_BASE ||
        mouse->connected) {
        return false;
    }

    clear_mouse(mouse);
    mouse->device_id = device_id;

    const struct aurora_input_event added = {
        .type = AURORA_INPUT_EVENT_DEVICE_ADDED,
        .source = AURORA_INPUT_SOURCE_USB_HID,
        .device_id = device_id
    };

    if (!input_push_event(&added)) {
        clear_mouse(mouse);
        return false;
    }

    mouse->connected = true;
    return true;
}

bool usb_hid_mouse_detach(
    struct aurora_usb_hid_mouse *mouse
) {
    if (mouse == NULL || !mouse->connected) {
        return false;
    }

    /*
     * Publish button releases before DEVICE_REMOVED so focus/capture policy
     * never observes a permanently pressed button after unplug/revocation.
     * State is cleared per successful release for retry safety.
     */
    const enum aurora_pointer_button buttons[3] = {
        AURORA_POINTER_BUTTON_LEFT,
        AURORA_POINTER_BUTTON_RIGHT,
        AURORA_POINTER_BUTTON_MIDDLE
    };

    for (uint32_t i = 0u; i < 3u; ++i) {
        uint8_t mask = (uint8_t)(1u << i);

        if ((mouse->buttons & mask) == 0u) continue;

        if (!emit_mouse_button(mouse, buttons[i], false)) {
            return false;
        }

        mouse->buttons &= (uint8_t)~mask;
    }

    const struct aurora_input_event removed = {
        .type = AURORA_INPUT_EVENT_DEVICE_REMOVED,
        .source = AURORA_INPUT_SOURCE_USB_HID,
        .device_id = mouse->device_id
    };

    if (!input_push_event(&removed)) {
        return false;
    }

    clear_mouse(mouse);
    return true;
}

bool usb_hid_mouse_process_boot_report(
    struct aurora_usb_hid_mouse *mouse,
    const uint8_t report[4]
) {
    if (mouse == NULL || report == NULL || !mouse->connected) {
        return false;
    }

    uint8_t new_buttons = (uint8_t)(report[0] & 0x07u);
    uint8_t changed = (uint8_t)(new_buttons ^ mouse->buttons);
    int32_t dx = (int32_t)(int8_t)report[1];
    int32_t dy = (int32_t)(int8_t)report[2];
    int32_t wheel = (int32_t)(int8_t)report[3];

    if (dx != 0 || dy != 0) {
        const struct aurora_input_event motion = {
            .type = AURORA_INPUT_EVENT_POINTER_RELATIVE,
            .source = AURORA_INPUT_SOURCE_USB_HID,
            .device_id = mouse->device_id,
            .delta_x = dx,
            .delta_y = dy
        };

        if (!input_push_event(&motion)) return false;
    }

    if ((changed & 0x01u) != 0u &&
        !emit_mouse_button(
            mouse,
            AURORA_POINTER_BUTTON_LEFT,
            (new_buttons & 0x01u) != 0u)) {
        return false;
    }

    if ((changed & 0x02u) != 0u &&
        !emit_mouse_button(
            mouse,
            AURORA_POINTER_BUTTON_RIGHT,
            (new_buttons & 0x02u) != 0u)) {
        return false;
    }

    if ((changed & 0x04u) != 0u &&
        !emit_mouse_button(
            mouse,
            AURORA_POINTER_BUTTON_MIDDLE,
            (new_buttons & 0x04u) != 0u)) {
        return false;
    }

    if (wheel != 0) {
        const struct aurora_input_event scroll = {
            .type = AURORA_INPUT_EVENT_SCROLL,
            .source = AURORA_INPUT_SOURCE_USB_HID,
            .device_id = mouse->device_id,
            .scroll_y = wheel
        };

        if (!input_push_event(&scroll)) return false;
    }

    mouse->buttons = new_buttons;
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

    input_init();

    static struct aurora_usb_hid_mouse mouse;
    clear_mouse(&mouse);

    const uint64_t mouse_device_id =
        AURORA_INPUT_DEVICE_USB_BASE + UINT64_C(8);
    const uint8_t mouse_press_move_scroll[4] = {
        0x01u, 5u, (uint8_t)-3, 1u
    };
    const uint8_t mouse_release[4] = {
        0u, 0u, 0u, 0u
    };

    if (!usb_hid_mouse_attach(&mouse, mouse_device_id) ||
        !usb_hid_mouse_process_boot_report(
            &mouse,
            mouse_press_move_scroll) ||
        !usb_hid_mouse_process_boot_report(
            &mouse,
            mouse_release) ||
        !usb_hid_mouse_detach(&mouse)) {
        return false;
    }

    if (!input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_DEVICE_ADDED ||
        event.device_id != mouse_device_id ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_POINTER_RELATIVE ||
        event.device_id != mouse_device_id ||
        event.delta_x != 5 ||
        event.delta_y != -3 ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_POINTER_BUTTON ||
        event.button != AURORA_POINTER_BUTTON_LEFT ||
        !event.pressed ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_SCROLL ||
        event.scroll_y != 1 ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_POINTER_BUTTON ||
        event.button != AURORA_POINTER_BUTTON_LEFT ||
        event.pressed ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_DEVICE_REMOVED ||
        event.device_id != mouse_device_id ||
        input_poll_event(&event)) {
        return false;
    }

    input_init();

    static struct aurora_usb_hid_mouse right_mouse;
    clear_mouse(&right_mouse);

    const uint64_t right_mouse_id =
        AURORA_INPUT_DEVICE_USB_BASE + UINT64_C(9);
    const uint8_t right_press[4] = {0x02u, 0u, 0u, 0u};
    const uint8_t right_release[4] = {0u, 0u, 0u, 0u};

    if (!usb_hid_mouse_attach(&right_mouse, right_mouse_id) ||
        !usb_hid_mouse_process_boot_report(&right_mouse, right_press) ||
        !usb_hid_mouse_process_boot_report(&right_mouse, right_release) ||
        !usb_hid_mouse_detach(&right_mouse)) {
        return false;
    }

    if (!input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_DEVICE_ADDED ||
        event.device_id != right_mouse_id ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_POINTER_BUTTON ||
        event.button != AURORA_POINTER_BUTTON_RIGHT ||
        !event.pressed ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_POINTER_BUTTON ||
        event.button != AURORA_POINTER_BUTTON_RIGHT ||
        event.pressed ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_DEVICE_REMOVED ||
        event.device_id != right_mouse_id ||
        input_poll_event(&event)) {
        return false;
    }

    input_init();

    static struct aurora_usb_hid_mouse middle_mouse;
    clear_mouse(&middle_mouse);

    const uint64_t middle_mouse_id =
        AURORA_INPUT_DEVICE_USB_BASE + UINT64_C(10);
    const uint8_t middle_press[4] = {0x04u, 0u, 0u, 0u};
    const uint8_t middle_release[4] = {0u, 0u, 0u, 0u};

    if (!usb_hid_mouse_attach(&middle_mouse, middle_mouse_id) ||
        !usb_hid_mouse_process_boot_report(&middle_mouse, middle_press) ||
        !usb_hid_mouse_process_boot_report(&middle_mouse, middle_release) ||
        !usb_hid_mouse_detach(&middle_mouse)) {
        return false;
    }

    if (!input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_DEVICE_ADDED ||
        event.device_id != middle_mouse_id ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_POINTER_BUTTON ||
        event.button != AURORA_POINTER_BUTTON_MIDDLE ||
        !event.pressed ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_POINTER_BUTTON ||
        event.button != AURORA_POINTER_BUTTON_MIDDLE ||
        event.pressed ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_DEVICE_REMOVED ||
        event.device_id != middle_mouse_id ||
        input_poll_event(&event)) {
        return false;
    }

    input_init();

    static struct aurora_usb_hid_mouse wheel_mouse;
    clear_mouse(&wheel_mouse);

    const uint64_t wheel_mouse_id =
        AURORA_INPUT_DEVICE_USB_BASE + UINT64_C(11);
    const uint8_t wheel_up[4] = {0u, 0u, 0u, 1u};
    const uint8_t wheel_down[4] = {0u, 0u, 0u, (uint8_t)-1};

    if (!usb_hid_mouse_attach(&wheel_mouse, wheel_mouse_id) ||
        !usb_hid_mouse_process_boot_report(&wheel_mouse, wheel_up) ||
        !usb_hid_mouse_process_boot_report(&wheel_mouse, wheel_down) ||
        !usb_hid_mouse_detach(&wheel_mouse)) {
        return false;
    }

    if (!input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_DEVICE_ADDED ||
        event.device_id != wheel_mouse_id ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_SCROLL ||
        event.scroll_y != 1 ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_SCROLL ||
        event.scroll_y != -1 ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_DEVICE_REMOVED ||
        event.device_id != wheel_mouse_id ||
        input_poll_event(&event)) {
        return false;
    }

    return true;
}
