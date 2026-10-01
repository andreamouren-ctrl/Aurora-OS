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

struct aurora_input_event {
    enum aurora_key_code key;
    bool pressed;
};

void input_init(void);

bool input_push_event_from_irq(
    const struct aurora_input_event *event
);

bool input_poll_event(
    struct aurora_input_event *event
);

#endif
