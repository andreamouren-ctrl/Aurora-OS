#ifndef AURORA_IDENTITY_CLIENT_H
#define AURORA_IDENTITY_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/ipc.h>
#include <aurora/identity_service_protocol.h>

enum aurora_identity_client_state {
    AURORA_IDENTITY_CLIENT_UNINITIALIZED = 0,
    AURORA_IDENTITY_CLIENT_READY,
    AURORA_IDENTITY_CLIENT_AUTHENTICATING,
    AURORA_IDENTITY_CLIENT_AUTH_FAILED,
    AURORA_IDENTITY_CLIENT_THROTTLED,
    AURORA_IDENTITY_CLIENT_VERIFIED,
    AURORA_IDENTITY_CLIENT_CREATING,
    AURORA_IDENTITY_CLIENT_CREATED,
    AURORA_IDENTITY_CLIENT_CREATE_EXISTS,
    AURORA_IDENTITY_CLIENT_CREATE_DENIED,
    AURORA_IDENTITY_CLIENT_UNAVAILABLE,
    AURORA_IDENTITY_CLIENT_ERROR
};

bool identity_client_init(void);

bool identity_client_begin_key_auth(
    const char *key,
    size_t key_length
);

bool identity_client_begin_create(
    const char *key,
    size_t key_length
);

void identity_client_pump(void);

void identity_client_reset_result(void);

enum aurora_identity_client_state identity_client_state(void);

uint64_t identity_client_retry_after_ms(void);

bool identity_client_take_session_grant(
    uint8_t out_grant[AURORA_IDENTITY_SERVICE_GRANT_TOKEN_SIZE]
);

struct aurora_ipc_endpoint *identity_client_session_peer_endpoint(void);

#endif
