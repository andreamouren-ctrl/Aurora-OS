#ifndef AURORA_LOGIN_UI_H
#define AURORA_LOGIN_UI_H

#include <stdbool.h>
#include <stddef.h>

#include <aurora/boot.h>

enum aurora_login_state {
    AURORA_LOGIN_IDLE = 0,
    AURORA_LOGIN_AUTHENTICATING,
    AURORA_LOGIN_UNKNOWN_IDENTITY,
    AURORA_LOGIN_CREATE_ENTRY,
    AURORA_LOGIN_CREATING,
    AURORA_LOGIN_CREATED,
    AURORA_LOGIN_CREATE_DENIED,
    AURORA_LOGIN_ERROR,
    AURORA_LOGIN_THROTTLED,
    AURORA_LOGIN_LOGGING_OUT,
    AURORA_LOGIN_LOCKING,
    AURORA_LOGIN_LOCKED,
    AURORA_LOGIN_UNLOCKING,
    AURORA_LOGIN_UNLOCK_FAILED,
    AURORA_LOGIN_SESSION_ACTIVE
};

void login_ui_init(
    const struct aurora_framebuffer *framebuffer
);

bool login_ui_is_initialized(void);

bool login_ui_activate_native_artwork(void);

void login_ui_set_state(
    enum aurora_login_state state
);

void login_ui_set_masked_length(
    size_t normalized_length
);

void login_ui_render(void);

#endif
