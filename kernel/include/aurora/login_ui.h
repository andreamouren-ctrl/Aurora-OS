#ifndef AURORA_LOGIN_UI_H
#define AURORA_LOGIN_UI_H

#include <stdbool.h>
#include <stddef.h>

#include <aurora/boot.h>

enum aurora_login_state {
    AURORA_LOGIN_IDLE = 0,
    AURORA_LOGIN_AUTHENTICATING,
    AURORA_LOGIN_UNKNOWN_IDENTITY,
    AURORA_LOGIN_ERROR,
    AURORA_LOGIN_THROTTLED
};

void login_ui_init(
    const struct aurora_framebuffer *framebuffer
);

bool login_ui_is_initialized(void);

void login_ui_set_state(
    enum aurora_login_state state
);

void login_ui_set_masked_length(
    size_t normalized_length
);

void login_ui_render(void);

#endif
