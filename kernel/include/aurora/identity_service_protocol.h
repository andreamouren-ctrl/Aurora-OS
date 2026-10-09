#ifndef AURORA_IDENTITY_SERVICE_PROTOCOL_H
#define AURORA_IDENTITY_SERVICE_PROTOCOL_H

#include <stdint.h>

#define AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION 8u
#define AURORA_IDENTITY_SERVICE_MESSAGE_SIZE 16u
#define AURORA_IDENTITY_SERVICE_KEY_MAX_LEN 32u
#define AURORA_IDENTITY_SERVICE_GRANT_TOKEN_SIZE 32u
#define AURORA_IDENTITY_SERVICE_USER_ID_SIZE 16u
#define AURORA_IDENTITY_SERVICE_CREDENTIAL_ID_SIZE 16u
#define AURORA_IDENTITY_SERVICE_REAUTH_PROOF_SIZE 32u

enum aurora_identity_service_message_type {
    AURORA_IDENTITY_SERVICE_READY = 1,
    AURORA_IDENTITY_SERVICE_PING = 2,
    AURORA_IDENTITY_SERVICE_PONG = 3,
    AURORA_IDENTITY_SERVICE_SHUTDOWN = 4,
    AURORA_IDENTITY_SERVICE_SHUTDOWN_ACK = 5,

    AURORA_IDENTITY_SERVICE_BEGIN_KEY_AUTH = 16,
    AURORA_IDENTITY_SERVICE_AUTH_PENDING = 17,
    AURORA_IDENTITY_SERVICE_QUERY_AUTH = 18,
    AURORA_IDENTITY_SERVICE_AUTH_RESULT = 19,
    AURORA_IDENTITY_SERVICE_CANCEL_AUTH = 20,
    AURORA_IDENTITY_SERVICE_AUTH_CANCELLED = 21,

    AURORA_IDENTITY_SERVICE_BEGIN_CREATE = 32,
    AURORA_IDENTITY_SERVICE_CREATE_PENDING = 33,
    AURORA_IDENTITY_SERVICE_QUERY_CREATE = 34,
    AURORA_IDENTITY_SERVICE_CREATE_RESULT = 35,
    AURORA_IDENTITY_SERVICE_CANCEL_CREATE = 36,
    AURORA_IDENTITY_SERVICE_CREATE_CANCELLED = 37,

    AURORA_IDENTITY_SERVICE_CONSUME_SESSION_GRANT = 48,
    AURORA_IDENTITY_SERVICE_SESSION_GRANT_RESULT = 49,

    AURORA_IDENTITY_SERVICE_BEGIN_REAUTH = 64,
    AURORA_IDENTITY_SERVICE_REAUTH_PENDING = 65,
    AURORA_IDENTITY_SERVICE_QUERY_REAUTH = 66,
    AURORA_IDENTITY_SERVICE_REAUTH_RESULT = 67,
    AURORA_IDENTITY_SERVICE_CANCEL_REAUTH = 68,
    AURORA_IDENTITY_SERVICE_REAUTH_CANCELLED = 69,

    AURORA_IDENTITY_SERVICE_BEGIN_ROTATE_KEY = 80,
    AURORA_IDENTITY_SERVICE_ROTATE_KEY_PENDING = 81,
    AURORA_IDENTITY_SERVICE_QUERY_ROTATE_KEY = 82,
    AURORA_IDENTITY_SERVICE_ROTATE_KEY_RESULT = 83,
    AURORA_IDENTITY_SERVICE_CANCEL_ROTATE_KEY = 84,
    AURORA_IDENTITY_SERVICE_ROTATE_KEY_CANCELLED = 85,

    AURORA_IDENTITY_SERVICE_AUDIT_SESSION_EVENT = 96,
    AURORA_IDENTITY_SERVICE_AUDIT_SESSION_RESULT = 97,
    AURORA_IDENTITY_SERVICE_SECURITY_ACTIVITY_READ = 98,
    AURORA_IDENTITY_SERVICE_SECURITY_ACTIVITY_RESULT = 99,

    AURORA_IDENTITY_SERVICE_ERROR = 255
};

enum aurora_identity_service_auth_state {
    AURORA_IDENTITY_SERVICE_AUTH_STATE_PENDING = 1,
    AURORA_IDENTITY_SERVICE_AUTH_STATE_FAILED,
    AURORA_IDENTITY_SERVICE_AUTH_STATE_THROTTLED,
    AURORA_IDENTITY_SERVICE_AUTH_STATE_CREATION_AVAILABLE,
    AURORA_IDENTITY_SERVICE_AUTH_STATE_SUCCESS,
    AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR,
    AURORA_IDENTITY_SERVICE_AUTH_STATE_CANCELLED
};

enum aurora_identity_service_create_state {
    AURORA_IDENTITY_SERVICE_CREATE_STATE_PENDING = 1,
    AURORA_IDENTITY_SERVICE_CREATE_STATE_SUCCESS,
    AURORA_IDENTITY_SERVICE_CREATE_STATE_ALREADY_EXISTS,
    AURORA_IDENTITY_SERVICE_CREATE_STATE_SERVICE_ERROR,
    AURORA_IDENTITY_SERVICE_CREATE_STATE_CANCELLED
};

enum aurora_identity_service_session_grant_state {
    AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SUCCESS = 1,
    AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_REJECTED,
    AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SERVICE_ERROR
};

enum aurora_identity_service_reauth_purpose {
    AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_NONE = 0,
    AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY = 1,
    AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_ENROLL_AUTHENTICATOR = 2,
    AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_REVOKE_AUTHENTICATOR = 3,
    AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_CHANGE_RECOVERY_POLICY = 4,
    AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_EXPORT_RECOVERY_MATERIAL = 5,
    AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_APPROVE_USER_CREATION = 6,
    AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_CHANGE_LOCAL_ROLE = 7,
    AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_GRANT_RESOURCE_ACCESS = 8,
    AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_COUNT
};

enum aurora_identity_service_reauth_state {
    AURORA_IDENTITY_SERVICE_REAUTH_STATE_PENDING = 1,
    AURORA_IDENTITY_SERVICE_REAUTH_STATE_SUCCESS,
    AURORA_IDENTITY_SERVICE_REAUTH_STATE_FAILED,
    AURORA_IDENTITY_SERVICE_REAUTH_STATE_THROTTLED,
    AURORA_IDENTITY_SERVICE_REAUTH_STATE_SERVICE_ERROR,
    AURORA_IDENTITY_SERVICE_REAUTH_STATE_CANCELLED
};

enum aurora_identity_service_rotate_key_state {
    AURORA_IDENTITY_SERVICE_ROTATE_KEY_STATE_PENDING = 1,
    AURORA_IDENTITY_SERVICE_ROTATE_KEY_STATE_SUCCESS,
    AURORA_IDENTITY_SERVICE_ROTATE_KEY_STATE_ALREADY_EXISTS,
    AURORA_IDENTITY_SERVICE_ROTATE_KEY_STATE_SERVICE_ERROR,
    AURORA_IDENTITY_SERVICE_ROTATE_KEY_STATE_CANCELLED
};

enum aurora_identity_service_audit_session_event_type {
    AURORA_IDENTITY_SERVICE_AUDIT_SESSION_STARTED = 1,
    AURORA_IDENTITY_SERVICE_AUDIT_SESSION_LOCKED,
    AURORA_IDENTITY_SERVICE_AUDIT_SESSION_UNLOCKED,
    AURORA_IDENTITY_SERVICE_AUDIT_SESSION_LOGOUT,
    AURORA_IDENTITY_SERVICE_AUDIT_SESSION_TERMINATED
};

enum aurora_identity_service_audit_state {
    AURORA_IDENTITY_SERVICE_AUDIT_STATE_SUCCESS = 1,
    AURORA_IDENTITY_SERVICE_AUDIT_STATE_SERVICE_ERROR
};

enum aurora_identity_service_security_activity_state {
    AURORA_IDENTITY_SERVICE_SECURITY_ACTIVITY_RECORD = 1,
    AURORA_IDENTITY_SERVICE_SECURITY_ACTIVITY_END,
    AURORA_IDENTITY_SERVICE_SECURITY_ACTIVITY_SERVICE_ERROR
};

enum aurora_identity_service_public_error {
    AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_NONE = 0,
    AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INVALID_REQUEST,
    AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_UNAUTHORIZED,
    AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_UNSUPPORTED_VERSION,
    AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_BUSY,
    AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_AUTH_FAILED,
    AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_THROTTLED,
    AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_POLICY_DENIED,
    AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_SERVICE_UNAVAILABLE,
    AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_STORAGE_FAILURE,
    AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INTERNAL_FAILURE
};

struct aurora_identity_service_message {
    uint32_t version;
    uint32_t type;
    uint64_t request_id;
};

struct aurora_identity_service_begin_key_auth {
    struct aurora_identity_service_message header;
    uint32_t key_length;
    uint32_t reserved;
    char key[AURORA_IDENTITY_SERVICE_KEY_MAX_LEN];
};

struct aurora_identity_service_auth_result {
    struct aurora_identity_service_message header;
    uint32_t state;
    uint32_t public_error;
    uint64_t retry_after_ms;
    uint8_t session_grant[AURORA_IDENTITY_SERVICE_GRANT_TOKEN_SIZE];
};

struct aurora_identity_service_begin_create {
    struct aurora_identity_service_message header;
    uint32_t key_length;
    uint32_t reserved;
    char key[AURORA_IDENTITY_SERVICE_KEY_MAX_LEN];
};

struct aurora_identity_service_create_result {
    struct aurora_identity_service_message header;
    uint32_t state;
    uint32_t public_error;
    uint8_t user_id[AURORA_IDENTITY_SERVICE_USER_ID_SIZE];
    uint8_t credential_id[AURORA_IDENTITY_SERVICE_CREDENTIAL_ID_SIZE];
};

struct aurora_identity_service_consume_session_grant {
    struct aurora_identity_service_message header;
    uint8_t session_grant[AURORA_IDENTITY_SERVICE_GRANT_TOKEN_SIZE];
};

struct aurora_identity_service_session_grant_result {
    struct aurora_identity_service_message header;
    uint32_t state;
    uint32_t public_error;
    uint8_t user_id[AURORA_IDENTITY_SERVICE_USER_ID_SIZE];
};

struct aurora_identity_service_begin_reauth {
    struct aurora_identity_service_message header;
    uint32_t key_length;
    uint32_t purpose;
    uint64_t session_generation;
    uint8_t expected_user_id[AURORA_IDENTITY_SERVICE_USER_ID_SIZE];
    char key[AURORA_IDENTITY_SERVICE_KEY_MAX_LEN];
};

struct aurora_identity_service_reauth_result {
    struct aurora_identity_service_message header;
    uint32_t state;
    uint32_t public_error;
    uint64_t retry_after_ms;
    uint64_t expires_at_ms;
    uint32_t purpose;
    uint32_t reserved;
    uint8_t proof[AURORA_IDENTITY_SERVICE_REAUTH_PROOF_SIZE];
};

struct aurora_identity_service_begin_rotate_key {
    struct aurora_identity_service_message header;
    uint32_t new_key_length;
    uint32_t reserved;
    uint64_t session_generation;
    uint8_t expected_user_id[AURORA_IDENTITY_SERVICE_USER_ID_SIZE];
    uint8_t proof[AURORA_IDENTITY_SERVICE_REAUTH_PROOF_SIZE];
    char new_key[AURORA_IDENTITY_SERVICE_KEY_MAX_LEN];
};

struct aurora_identity_service_rotate_key_result {
    struct aurora_identity_service_message header;
    uint32_t state;
    uint32_t public_error;
    uint8_t new_credential_id[AURORA_IDENTITY_SERVICE_CREDENTIAL_ID_SIZE];
};

struct aurora_identity_service_audit_session_event {
    struct aurora_identity_service_message header;
    uint32_t event;
    uint32_t reserved;
    uint64_t session_generation;
    uint8_t user_id[AURORA_IDENTITY_SERVICE_USER_ID_SIZE];
};

struct aurora_identity_service_audit_session_result {
    struct aurora_identity_service_message header;
    uint32_t state;
    uint32_t public_error;
};

struct aurora_identity_service_security_activity_read {
    struct aurora_identity_service_message header;
    uint64_t before_sequence;
    uint64_t session_generation;
    uint8_t expected_user_id[AURORA_IDENTITY_SERVICE_USER_ID_SIZE];
};

struct aurora_identity_service_security_activity_result {
    struct aurora_identity_service_message header;
    uint32_t state;
    uint32_t public_error;
    uint32_t record_version;
    uint32_t event_type;
    uint32_t outcome;
    uint32_t reason_code;
    uint64_t sequence;
    uint64_t monotonic_ms;
    uint64_t session_generation;
};

#endif
