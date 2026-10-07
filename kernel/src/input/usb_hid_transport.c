#include <stddef.h>
#include <stdint.h>

#include <aurora/input.h>
#include <aurora/usb_hid_transport.h>

static void clear_bytes(void *ptr, uint64_t size) {
    uint8_t *bytes = (uint8_t *)ptr;

    for (uint64_t i = 0u; i < size; ++i) {
        bytes[i] = 0u;
    }
}

static aurora_usb_hid_binding_handle make_handle(
    uint32_t slot,
    uint32_t generation
) {
    if (slot >= AURORA_USB_HID_MAX_BINDINGS ||
        generation == 0u) {
        return AURORA_USB_HID_BINDING_INVALID;
    }

    return
        ((uint64_t)generation << 32u) |
        (uint64_t)(slot + 1u);
}

static struct aurora_usb_hid_binding *binding_from_handle(
    struct aurora_usb_hid_transport *transport,
    aurora_usb_hid_binding_handle handle
) {
    if (transport == NULL ||
        !transport->initialized ||
        handle == AURORA_USB_HID_BINDING_INVALID) {
        return NULL;
    }

    uint32_t encoded_slot = (uint32_t)(handle & UINT32_MAX);
    uint32_t generation = (uint32_t)(handle >> 32u);

    if (encoded_slot == 0u ||
        encoded_slot > AURORA_USB_HID_MAX_BINDINGS ||
        generation == 0u) {
        return NULL;
    }

    struct aurora_usb_hid_binding *binding =
        &transport->bindings[encoded_slot - 1u];

    if (!binding->used ||
        binding->generation != generation) {
        return NULL;
    }

    return binding;
}

static uint64_t allocate_device_id(
    struct aurora_usb_hid_transport *transport
) {
    if (transport == NULL) {
        return AURORA_INPUT_DEVICE_UNSPECIFIED;
    }

    uint64_t sequence = transport->next_device_sequence++;

    if (sequence == 0u) {
        sequence = transport->next_device_sequence++;
    }

    if (sequence >
        UINT64_MAX - AURORA_INPUT_DEVICE_USB_BASE) {
        return AURORA_INPUT_DEVICE_UNSPECIFIED;
    }

    return AURORA_INPUT_DEVICE_USB_BASE + sequence;
}

bool usb_hid_transport_init(
    struct aurora_usb_hid_transport *transport
) {
    if (transport == NULL) return false;

    clear_bytes(transport, sizeof(*transport));
    transport->next_device_sequence = 1u;
    transport->initialized = true;
    return true;
}

bool usb_hid_transport_bind(
    struct aurora_usb_hid_transport *transport,
    enum aurora_usb_hid_protocol protocol,
    aurora_usb_hid_binding_handle *out_handle,
    uint64_t *out_device_id
) {
    if (out_handle != NULL) {
        *out_handle = AURORA_USB_HID_BINDING_INVALID;
    }
    if (out_device_id != NULL) {
        *out_device_id = AURORA_INPUT_DEVICE_UNSPECIFIED;
    }

    if (transport == NULL ||
        !transport->initialized ||
        out_handle == NULL ||
        out_device_id == NULL ||
        (protocol != AURORA_USB_HID_PROTOCOL_BOOT_KEYBOARD &&
         protocol != AURORA_USB_HID_PROTOCOL_BOOT_MOUSE)) {
        return false;
    }

    uint32_t slot = AURORA_USB_HID_MAX_BINDINGS;

    for (uint32_t i = 0u; i < AURORA_USB_HID_MAX_BINDINGS; ++i) {
        if (!transport->bindings[i].used) {
            slot = i;
            break;
        }
    }

    if (slot == AURORA_USB_HID_MAX_BINDINGS) {
        return false;
    }

    struct aurora_usb_hid_binding *binding =
        &transport->bindings[slot];

    uint32_t generation = binding->generation + 1u;
    if (generation == 0u) generation = 1u;

    uint64_t device_id = allocate_device_id(transport);
    if (device_id == AURORA_INPUT_DEVICE_UNSPECIFIED) {
        return false;
    }

    *binding = (struct aurora_usb_hid_binding){
        .protocol = protocol,
        .device_id = device_id,
        .generation = generation,
        .used = true
    };

    bool attached =
        protocol == AURORA_USB_HID_PROTOCOL_BOOT_KEYBOARD
            ? usb_hid_keyboard_attach(
                &binding->decoder.keyboard,
                device_id)
            : usb_hid_mouse_attach(
                &binding->decoder.mouse,
                device_id);

    if (!attached) {
        binding->used = false;
        binding->protocol = AURORA_USB_HID_PROTOCOL_NONE;
        binding->device_id = AURORA_INPUT_DEVICE_UNSPECIFIED;
        return false;
    }

    aurora_usb_hid_binding_handle handle =
        make_handle(slot, generation);

    if (handle == AURORA_USB_HID_BINDING_INVALID) {
        binding->used = false;
        return false;
    }

    *out_handle = handle;
    *out_device_id = device_id;
    return true;
}

bool usb_hid_transport_submit_report(
    struct aurora_usb_hid_transport *transport,
    aurora_usb_hid_binding_handle handle,
    const uint8_t *report,
    size_t report_size
) {
    struct aurora_usb_hid_binding *binding =
        binding_from_handle(transport, handle);

    if (binding == NULL || report == NULL) {
        return false;
    }

    if (binding->protocol ==
        AURORA_USB_HID_PROTOCOL_BOOT_KEYBOARD) {
        if (report_size != 8u) return false;

        return usb_hid_keyboard_process_boot_report(
            &binding->decoder.keyboard,
            report
        );
    }

    if (binding->protocol ==
        AURORA_USB_HID_PROTOCOL_BOOT_MOUSE) {
        if (report_size != 4u) return false;

        return usb_hid_mouse_process_boot_report(
            &binding->decoder.mouse,
            report
        );
    }

    return false;
}

bool usb_hid_transport_unbind(
    struct aurora_usb_hid_transport *transport,
    aurora_usb_hid_binding_handle handle
) {
    struct aurora_usb_hid_binding *binding =
        binding_from_handle(transport, handle);

    if (binding == NULL) return false;

    bool detached =
        binding->protocol ==
        AURORA_USB_HID_PROTOCOL_BOOT_KEYBOARD
            ? usb_hid_keyboard_detach(
                &binding->decoder.keyboard)
            : binding->protocol ==
                AURORA_USB_HID_PROTOCOL_BOOT_MOUSE
                ? usb_hid_mouse_detach(
                    &binding->decoder.mouse)
                : false;

    if (!detached) return false;

    uint32_t generation = binding->generation;

    *binding = (struct aurora_usb_hid_binding){
        .generation = generation
    };

    return true;
}

bool usb_hid_transport_selftest(void) {
    input_init();

    static struct aurora_usb_hid_transport transport;

    if (!usb_hid_transport_init(&transport)) {
        return false;
    }

    aurora_usb_hid_binding_handle keyboard =
        AURORA_USB_HID_BINDING_INVALID;
    aurora_usb_hid_binding_handle mouse =
        AURORA_USB_HID_BINDING_INVALID;
    uint64_t keyboard_id = 0u;
    uint64_t mouse_id = 0u;

    if (!usb_hid_transport_bind(
            &transport,
            AURORA_USB_HID_PROTOCOL_BOOT_KEYBOARD,
            &keyboard,
            &keyboard_id) ||
        !usb_hid_transport_bind(
            &transport,
            AURORA_USB_HID_PROTOCOL_BOOT_MOUSE,
            &mouse,
            &mouse_id) ||
        keyboard == AURORA_USB_HID_BINDING_INVALID ||
        mouse == AURORA_USB_HID_BINDING_INVALID ||
        keyboard == mouse ||
        keyboard_id < AURORA_INPUT_DEVICE_USB_BASE ||
        mouse_id < AURORA_INPUT_DEVICE_USB_BASE ||
        keyboard_id == mouse_id) {
        return false;
    }

    struct aurora_input_event event = {0};

    if (!input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_DEVICE_ADDED ||
        event.device_id != keyboard_id ||
        !input_poll_event(&event) ||
        event.type != AURORA_INPUT_EVENT_DEVICE_ADDED ||
        event.device_id != mouse_id ||
        input_poll_event(&event)) {
        return false;
    }

    const uint8_t keyboard_press[8] = {
        0u, 0u, 0x04u, 0u, 0u, 0u, 0u, 0u
    };
    const uint8_t mouse_motion[4] = {
        0x01u, 3u, (uint8_t)-2, 0u
    };

    if (usb_hid_transport_submit_report(
            &transport,
            keyboard,
            keyboard_press,
            7u) ||
        !usb_hid_transport_submit_report(
            &transport,
            keyboard,
            keyboard_press,
            sizeof(keyboard_press)) ||
        !usb_hid_transport_submit_report(
            &transport,
            mouse,
            mouse_motion,
            sizeof(mouse_motion)) ||
        !usb_hid_transport_unbind(
            &transport,
            keyboard) ||
        usb_hid_transport_submit_report(
            &transport,
            keyboard,
            keyboard_press,
            sizeof(keyboard_press)) ||
        usb_hid_transport_unbind(
            &transport,
            keyboard) ||
        !usb_hid_transport_unbind(
            &transport,
            mouse)) {
        return false;
    }

    aurora_usb_hid_binding_handle replacement =
        AURORA_USB_HID_BINDING_INVALID;
    uint64_t replacement_id = 0u;

    if (!usb_hid_transport_bind(
            &transport,
            AURORA_USB_HID_PROTOCOL_BOOT_KEYBOARD,
            &replacement,
            &replacement_id) ||
        replacement == keyboard ||
        replacement_id == keyboard_id ||
        usb_hid_transport_submit_report(
            &transport,
            keyboard,
            keyboard_press,
            sizeof(keyboard_press)) ||
        !usb_hid_transport_unbind(
            &transport,
            replacement)) {
        return false;
    }

    bool saw_key_down = false;
    bool saw_key_up_before_remove = false;
    bool saw_motion = false;
    bool saw_button_down = false;
    bool saw_button_up_before_remove = false;
    bool keyboard_removed = false;
    bool mouse_removed = false;
    bool replacement_added = false;
    bool replacement_removed = false;

    while (input_poll_event(&event)) {
        if (event.type == AURORA_INPUT_EVENT_KEY &&
            event.device_id == keyboard_id &&
            event.key == AURORA_KEY_A) {
            if (event.pressed) {
                saw_key_down = true;
            } else if (!keyboard_removed) {
                saw_key_up_before_remove = true;
            }
        }

        if (event.type == AURORA_INPUT_EVENT_POINTER_RELATIVE &&
            event.device_id == mouse_id &&
            event.delta_x == 3 &&
            event.delta_y == -2) {
            saw_motion = true;
        }

        if (event.type == AURORA_INPUT_EVENT_POINTER_BUTTON &&
            event.device_id == mouse_id &&
            event.button == AURORA_POINTER_BUTTON_LEFT) {
            if (event.pressed) {
                saw_button_down = true;
            } else if (!mouse_removed) {
                saw_button_up_before_remove = true;
            }
        }

        if (event.type == AURORA_INPUT_EVENT_DEVICE_REMOVED &&
            event.device_id == keyboard_id) {
            keyboard_removed = true;
        }

        if (event.type == AURORA_INPUT_EVENT_DEVICE_REMOVED &&
            event.device_id == mouse_id) {
            mouse_removed = true;
        }

        if (event.type == AURORA_INPUT_EVENT_DEVICE_ADDED &&
            event.device_id == replacement_id) {
            replacement_added = true;
        }

        if (event.type == AURORA_INPUT_EVENT_DEVICE_REMOVED &&
            event.device_id == replacement_id) {
            replacement_removed = true;
        }
    }

    return
        saw_key_down &&
        saw_key_up_before_remove &&
        saw_motion &&
        saw_button_down &&
        saw_button_up_before_remove &&
        keyboard_removed &&
        mouse_removed &&
        replacement_added &&
        replacement_removed;
}
