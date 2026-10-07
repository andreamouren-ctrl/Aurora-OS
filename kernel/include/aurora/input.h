#ifndef AURORA_INPUT_H
#define AURORA_INPUT_H

#include <stdbool.h>
#include <stdint.h>

enum aurora_key_code {
    AURORA_KEY_NONE = 0,

    AURORA_KEY_A,
    AURORA_KEY_B,
    AURORA_KEY_C,
    AURORA_KEY_D,
    AURORA_KEY_E,
    AURORA_KEY_F,
    AURORA_KEY_G,
    AURORA_KEY_H,
    AURORA_KEY_I,
    AURORA_KEY_J,
    AURORA_KEY_K,
    AURORA_KEY_L,
    AURORA_KEY_M,
    AURORA_KEY_N,
    AURORA_KEY_O,
    AURORA_KEY_P,
    AURORA_KEY_Q,
    AURORA_KEY_R,
    AURORA_KEY_S,
    AURORA_KEY_T,
    AURORA_KEY_U,
    AURORA_KEY_V,
    AURORA_KEY_W,
    AURORA_KEY_X,
    AURORA_KEY_Y,
    AURORA_KEY_Z,

    AURORA_KEY_0,
    AURORA_KEY_1,
    AURORA_KEY_2,
    AURORA_KEY_3,
    AURORA_KEY_4,
    AURORA_KEY_5,
    AURORA_KEY_6,
    AURORA_KEY_7,
    AURORA_KEY_8,
    AURORA_KEY_9,

    AURORA_KEY_BACKSPACE,
    AURORA_KEY_ENTER,
    AURORA_KEY_ESCAPE
};

enum aurora_input_event_type {
    AURORA_INPUT_EVENT_NONE = 0,
    AURORA_INPUT_EVENT_KEY,
    AURORA_INPUT_EVENT_POINTER_RELATIVE,
    AURORA_INPUT_EVENT_POINTER_ABSOLUTE,
    AURORA_INPUT_EVENT_POINTER_BUTTON,
    AURORA_INPUT_EVENT_SCROLL,
    AURORA_INPUT_EVENT_DEVICE_ADDED,
    AURORA_INPUT_EVENT_DEVICE_REMOVED
};

enum aurora_input_source {
    AURORA_INPUT_SOURCE_UNKNOWN = 0,
    AURORA_INPUT_SOURCE_PS2_KEYBOARD,
    AURORA_INPUT_SOURCE_PS2_MOUSE,
    AURORA_INPUT_SOURCE_USB_HID,
    AURORA_INPUT_SOURCE_SYNTHETIC
};

enum aurora_pointer_button {
    AURORA_POINTER_BUTTON_NONE = 0,
    AURORA_POINTER_BUTTON_LEFT = 1,
    AURORA_POINTER_BUTTON_RIGHT = 2,
    AURORA_POINTER_BUTTON_MIDDLE = 3,
    AURORA_POINTER_BUTTON_BACK = 4,
    AURORA_POINTER_BUTTON_FORWARD = 5
};

struct aurora_input_event {
    enum aurora_input_event_type type;
    enum aurora_input_source source;
    uint64_t sequence;
    bool synthetic;

    /* Key/button transition compatibility fields. */
    enum aurora_key_code key;
    enum aurora_pointer_button button;
    bool pressed;

    /* Pointer/axis payload. */
    int32_t delta_x;
    int32_t delta_y;
    int32_t absolute_x;
    int32_t absolute_y;
    int32_t scroll_x;
    int32_t scroll_y;
};

void input_init(void);

bool input_push_event_from_irq(
    const struct aurora_input_event *event
);

bool input_poll_event(
    struct aurora_input_event *event
);

bool input_push_event(
    const struct aurora_input_event *event
);

uint64_t input_last_sequence(void);

bool input_selftest(void);

#endif
