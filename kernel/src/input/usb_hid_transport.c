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

    return
        input_poll_event(&event) &&
        event.type == AURORA_INPUT_EVENT_DEVICE_ADDED &&
        event.device_id == keyboard_id &&
        input_poll_event(&event) &&
        event.type == AURORA_INPUT_EVENT_DEVICE_ADDED &&
        event.device_id == mouse_id &&
        !input_poll_event(&event);
}
