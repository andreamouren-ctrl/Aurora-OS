#include <stdbool.h>
#include <stdint.h>

#include <aurora/apic.h>
#include <aurora/input.h>
#include <aurora/interrupts.h>
#include <aurora/ioapic.h>
#include <aurora/ps2_mouse.h>

#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_COMMAND_PORT 0x64u
#define PS2_STATUS_OUTPUT_FULL 0x01u
#define PS2_STATUS_INPUT_FULL 0x02u
#define PS2_STATUS_AUX_DATA 0x20u
#define PS2_LEGACY_MOUSE_IRQ 12u

#define PS2_CMD_ENABLE_AUX 0xA8u
#define PS2_CMD_READ_CONFIG 0x20u
#define PS2_CMD_WRITE_CONFIG 0x60u
#define PS2_CMD_WRITE_AUX 0xD4u

#define PS2_MOUSE_SET_DEFAULTS 0xF6u
#define PS2_MOUSE_ENABLE_STREAMING 0xF4u
#define PS2_MOUSE_ACK 0xFAu

#define PS2_WAIT_LIMIT 100000u

static uint8_t packet[3];
static uint8_t packet_index;
static uint8_t button_state;

static inline uint8_t port_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void port_out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static bool wait_input_clear(void) {
    for (uint32_t i = 0u; i < PS2_WAIT_LIMIT; ++i) {
        if ((port_in8(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0u) {
            return true;
        }
    }
    return false;
}

static bool wait_output_full(void) {
    for (uint32_t i = 0u; i < PS2_WAIT_LIMIT; ++i) {
        if ((port_in8(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0u) {
            return true;
        }
    }
    return false;
}

static bool write_command(uint8_t command) {
    if (!wait_input_clear()) return false;
    port_out8(PS2_COMMAND_PORT, command);
    return true;
}

static bool write_data(uint8_t value) {
    if (!wait_input_clear()) return false;
    port_out8(PS2_DATA_PORT, value);
    return true;
}

static bool write_mouse_command(uint8_t command) {
    if (!write_command(PS2_CMD_WRITE_AUX) ||
        !write_data(command) ||
        !wait_output_full()) {
        return false;
    }

    return port_in8(PS2_DATA_PORT) == PS2_MOUSE_ACK;
}

static void emit_button(
    enum aurora_pointer_button button,
    bool pressed
) {
    const struct aurora_input_event event = {
        .type = AURORA_INPUT_EVENT_POINTER_BUTTON,
        .source = AURORA_INPUT_SOURCE_PS2_MOUSE,
        .device_id = AURORA_INPUT_DEVICE_PS2_MOUSE,
        .button = button,
        .pressed = pressed
    };
    (void)input_push_event_from_irq(&event);
}

static void decode_packet(
    uint8_t first,
    uint8_t second,
    uint8_t third
) {
    if ((first & 0x08u) == 0u) return;

    /* Ignore motion values explicitly marked overflow by the device. */
    bool overflow_x = (first & 0x40u) != 0u;
    bool overflow_y = (first & 0x80u) != 0u;

    int32_t dx = (int32_t)(int8_t)second;
    int32_t dy = (int32_t)(int8_t)third;

    if (!overflow_x || !overflow_y) {
        if (overflow_x) dx = 0;
        if (overflow_y) dy = 0;

        if (dx != 0 || dy != 0) {
            const struct aurora_input_event motion = {
                .type = AURORA_INPUT_EVENT_POINTER_RELATIVE,
                .source = AURORA_INPUT_SOURCE_PS2_MOUSE,
        .device_id = AURORA_INPUT_DEVICE_PS2_MOUSE,
                .delta_x = dx,
                /* PS/2 positive Y is up; Aurora screen coordinates grow down. */
                .delta_y = -dy
            };
            (void)input_push_event_from_irq(&motion);
        }
    }

    uint8_t new_buttons = (uint8_t)(first & 0x07u);
    uint8_t changed = (uint8_t)(new_buttons ^ button_state);

    if ((changed & 0x01u) != 0u) {
        emit_button(
            AURORA_POINTER_BUTTON_LEFT,
            (new_buttons & 0x01u) != 0u
        );
    }
    if ((changed & 0x02u) != 0u) {
        emit_button(
            AURORA_POINTER_BUTTON_RIGHT,
            (new_buttons & 0x02u) != 0u
        );
    }
    if ((changed & 0x04u) != 0u) {
        emit_button(
            AURORA_POINTER_BUTTON_MIDDLE,
            (new_buttons & 0x04u) != 0u
        );
    }

    button_state = new_buttons;
}

static void consume_byte(uint8_t value) {
    if (packet_index == 0u && (value & 0x08u) == 0u) {
        return;
    }

    packet[packet_index++] = value;

    if (packet_index == 3u) {
        decode_packet(packet[0], packet[1], packet[2]);
        packet_index = 0u;
    }
}

static struct interrupt_frame *ps2_mouse_interrupt(
    struct interrupt_frame *frame
) {
    uint8_t status = port_in8(PS2_STATUS_PORT);

    if ((status & (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_DATA)) ==
        (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_DATA)) {
        consume_byte(port_in8(PS2_DATA_PORT));
    }

    lapic_eoi();
    return frame;
}

bool ps2_mouse_init(uint32_t destination_apic_id) {
    packet_index = 0u;
    button_state = 0u;

    if (!write_command(PS2_CMD_ENABLE_AUX) ||
        !write_command(PS2_CMD_READ_CONFIG) ||
        !wait_output_full()) {
        return false;
    }

    uint8_t config = port_in8(PS2_DATA_PORT);
    config |= 0x02u;  /* IRQ12 enable. */
    config &= (uint8_t)~0x20u; /* second-port clock enabled. */

    if (!write_command(PS2_CMD_WRITE_CONFIG) ||
        !write_data(config) ||
        !write_mouse_command(PS2_MOUSE_SET_DEFAULTS) ||
        !write_mouse_command(PS2_MOUSE_ENABLE_STREAMING) ||
        !interrupt_register_handler(
            AURORA_VECTOR_MOUSE,
            ps2_mouse_interrupt)) {
        return false;
    }

    if (!ioapic_route_legacy_irq(
            PS2_LEGACY_MOUSE_IRQ,
            AURORA_VECTOR_MOUSE,
            destination_apic_id)) {
        return false;
    }

    const struct aurora_input_event added = {
        .type = AURORA_INPUT_EVENT_DEVICE_ADDED,
        .source = AURORA_INPUT_SOURCE_PS2_MOUSE
    };
    (void)input_push_event(&added);
    return true;
}

bool ps2_mouse_selftest(void) {
    input_init();
    packet_index = 0u;
    button_state = 0u;

    consume_byte(0x09u); /* sync + left */
    consume_byte(5u);
    consume_byte((uint8_t)-3);

    struct aurora_input_event motion = {0};
    struct aurora_input_event button = {0};

    if (!input_poll_event(&motion) ||
        !input_poll_event(&button)) {
        return false;
    }

    return
        motion.type == AURORA_INPUT_EVENT_POINTER_RELATIVE &&
        motion.delta_x == 5 &&
        motion.delta_y == 3 &&
        button.type == AURORA_INPUT_EVENT_POINTER_BUTTON &&
        button.button == AURORA_POINTER_BUTTON_LEFT &&
        button.pressed &&
        button.sequence > motion.sequence;
}
