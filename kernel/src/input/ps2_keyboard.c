#include <stdbool.h>
#include <stdint.h>

#include <aurora/apic.h>
#include <aurora/input.h>
#include <aurora/interrupts.h>
#include <aurora/ioapic.h>
#include <aurora/ps2_keyboard.h>

#define PS2_DATA_PORT   0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_STATUS_OUTPUT_FULL 0x01u
#define PS2_STATUS_AUX_DATA    0x20u
#define PS2_LEGACY_IRQ  1u

static bool extended_prefix;

static inline uint8_t port_in8(uint16_t port) {
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static enum aurora_key_code decode_set1_key(uint8_t make_code) {
    switch (make_code) {
        case 0x1Eu: return AURORA_KEY_A;
        case 0x30u: return AURORA_KEY_B;
        case 0x2Eu: return AURORA_KEY_C;
        case 0x20u: return AURORA_KEY_D;
        case 0x12u: return AURORA_KEY_E;
        case 0x21u: return AURORA_KEY_F;
        case 0x22u: return AURORA_KEY_G;
        case 0x23u: return AURORA_KEY_H;
        case 0x17u: return AURORA_KEY_I;
        case 0x24u: return AURORA_KEY_J;
        case 0x25u: return AURORA_KEY_K;
        case 0x26u: return AURORA_KEY_L;
        case 0x32u: return AURORA_KEY_M;
        case 0x31u: return AURORA_KEY_N;
        case 0x18u: return AURORA_KEY_O;
        case 0x19u: return AURORA_KEY_P;
        case 0x10u: return AURORA_KEY_Q;
        case 0x13u: return AURORA_KEY_R;
        case 0x1Fu: return AURORA_KEY_S;
        case 0x14u: return AURORA_KEY_T;
        case 0x16u: return AURORA_KEY_U;
        case 0x2Fu: return AURORA_KEY_V;
        case 0x11u: return AURORA_KEY_W;
        case 0x2Du: return AURORA_KEY_X;
        case 0x15u: return AURORA_KEY_Y;
        case 0x2Cu: return AURORA_KEY_Z;

        case 0x0Bu: return AURORA_KEY_0;
        case 0x02u: return AURORA_KEY_1;
        case 0x03u: return AURORA_KEY_2;
        case 0x04u: return AURORA_KEY_3;
        case 0x05u: return AURORA_KEY_4;
        case 0x06u: return AURORA_KEY_5;
        case 0x07u: return AURORA_KEY_6;
        case 0x08u: return AURORA_KEY_7;
        case 0x09u: return AURORA_KEY_8;
        case 0x0Au: return AURORA_KEY_9;

        case 0x0Eu: return AURORA_KEY_BACKSPACE;
        case 0x1Cu: return AURORA_KEY_ENTER;
        case 0x01u: return AURORA_KEY_ESCAPE;

        default: return AURORA_KEY_NONE;
    }
}

static void consume_scancode(uint8_t scancode) {
    if (scancode == 0xE0u || scancode == 0xE1u) {
        extended_prefix = true;
        return;
    }

    if (extended_prefix) {
        extended_prefix = false;
        return;
    }

    bool pressed = (scancode & 0x80u) == 0u;
    uint8_t make_code = (uint8_t)(scancode & 0x7Fu);

    enum aurora_key_code key = decode_set1_key(make_code);

    if (key == AURORA_KEY_NONE) {
        return;
    }

    struct aurora_input_event event = {
        .type = AURORA_INPUT_EVENT_KEY,
        .source = AURORA_INPUT_SOURCE_PS2_KEYBOARD,
        .device_id = AURORA_INPUT_DEVICE_PS2_KEYBOARD,
        .key = key,
        .pressed = pressed
    };

    (void)input_push_event_from_irq(&event);
}

static struct interrupt_frame *ps2_keyboard_interrupt(
    struct interrupt_frame *frame
) {
    uint8_t status = port_in8(PS2_STATUS_PORT);

    if ((status & PS2_STATUS_OUTPUT_FULL) != 0u) {
        consume_scancode(port_in8(PS2_DATA_PORT));
    }

    lapic_eoi();
    return frame;
}

bool ps2_keyboard_init(
    uint32_t destination_apic_id
) {
    extended_prefix = false;

    /*
     * Discard stale controller output left by firmware before the IRQ is
     * routed. Bound the drain so a broken controller cannot stall boot.
     */
    for (uint32_t i = 0u; i < 32u; ++i) {
        uint8_t status = port_in8(PS2_STATUS_PORT);

        if ((status & PS2_STATUS_OUTPUT_FULL) == 0u) {
            break;
        }

        /* Leave auxiliary-device bytes for the IRQ12 mouse path. */
        if ((status & PS2_STATUS_AUX_DATA) != 0u) {
            break;
        }

        (void)port_in8(PS2_DATA_PORT);
    }

    if (!interrupt_register_handler(
            AURORA_VECTOR_KEYBOARD,
            ps2_keyboard_interrupt) ||
        !ioapic_route_legacy_irq(
            PS2_LEGACY_IRQ,
            AURORA_VECTOR_KEYBOARD,
            destination_apic_id)) {
        return false;
    }

    const struct aurora_input_event added = {
        .type = AURORA_INPUT_EVENT_DEVICE_ADDED,
        .source = AURORA_INPUT_SOURCE_PS2_KEYBOARD,
        .device_id = AURORA_INPUT_DEVICE_PS2_KEYBOARD
    };
    (void)input_push_event(&added);
    return true;
}
