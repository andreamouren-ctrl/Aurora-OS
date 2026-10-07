#ifndef AURORA_SESSION_MANAGER_CLIENT_H
#define AURORA_SESSION_MANAGER_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/capability_abi.h>

#include <aurora/session_manager_protocol.h>

enum aurora_session_manager_client_state {
    AURORA_SESSION_CLIENT_UNINITIALIZED = 0,
    AURORA_SESSION_CLIENT_READY,
    AURORA_SESSION_CLIENT_STARTING,
    AURORA_SESSION_CLIENT_ACTIVE,
    AURORA_SESSION_CLIENT_LOGGING_OUT,
    AURORA_SESSION_CLIENT_REJECTED,
    AURORA_SESSION_CLIENT_UNAVAILABLE,
    AURORA_SESSION_CLIENT_ERROR
};

bool session_manager_client_init(void);

bool session_manager_client_begin(
    const uint8_t grant[AURORA_SESSION_MANAGER_GRANT_SIZE]
);

bool session_manager_client_logout(void);

void session_manager_client_pump(void);

enum aurora_session_manager_client_state session_manager_client_state(void);

uint64_t session_manager_client_generation(void);

const uint8_t *session_manager_client_user_id(void);

aurora_cap_handle session_manager_client_profile_handle(void);

#endif
