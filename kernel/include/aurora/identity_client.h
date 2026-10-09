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
    AURORA_IDENTITY_CLIENT_REAUTHENTICATING,
    AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED,
    AURORA_IDENTITY_CLIENT_REAUTH_FAILED,
    AURORA_IDENTITY_CLIENT_REAUTH_THROTTLED,
    AURORA_IDENTITY_CLIENT_ROTATING_KEY,
    AURORA_IDENTITY_CLIENT_KEY_ROTATED,
    AURORA_IDENTITY_CLIENT_ROTATE_KEY_EXISTS,
    AURORA_IDENTITY_CLIENT_READING_ACTIVITY,
    AURORA_IDENTITY_CLIENT_ACTIVITY_RECORD,
    AURORA_IDENTITY_CLIENT_ACTIVITY_END,
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

bool identity_client_begin_reauth(
    const char *key,
    size_t key_length,
    uint32_t purpose
);

bool identity_client_begin_rotate_key(
    const uint8_t proof[AURORA_IDENTITY_SERVICE_REAUTH_PROOF_SIZE],
    const char *new_key,
    size_t new_key_length
);

struct aurora_security_activity_record {
    uint32_t record_version;
    uint32_t event_type;
    uint32_t outcome;
    uint32_t reason_code;
    uint64_t sequence;
    uint64_t monotonic_ms;
    uint64_t session_generation;
};

bool identity_client_begin_security_activity_read(uint64_t before_sequence);

bool identity_client_take_security_activity_record(
    struct aurora_security_activity_record *out_record);

void identity_client_pump(void);

void identity_client_reset_result(void);

/* Discards only a completed Security Activity reply. Never clears auth grants. */
bool identity_client_discard_completed_security_activity(void);

enum aurora_identity_client_state identity_client_state(void);

uint64_t identity_client_retry_after_ms(void);

bool identity_client_take_session_grant(
    uint8_t out_grant[AURORA_IDENTITY_SERVICE_GRANT_TOKEN_SIZE]
);

bool identity_client_take_reauth_proof(
    uint8_t out_proof[AURORA_IDENTITY_SERVICE_REAUTH_PROOF_SIZE],
    uint32_t *out_purpose,
    uint64_t *out_expires_at_ms
);

struct aurora_ipc_endpoint *identity_client_session_peer_endpoint(void);

#endif
