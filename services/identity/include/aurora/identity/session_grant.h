#ifndef AURORA_IDENTITY_SESSION_GRANT_H
#define AURORA_IDENTITY_SESSION_GRANT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aurora/identity/core.h"

#define AURORA_IDENTITY_SESSION_GRANT_TOKEN_SIZE 32u
#define AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE 32u
#define AURORA_IDENTITY_SESSION_GRANT_GENERATION_ATTEMPTS 4u
#define AURORA_IDENTITY_SESSION_GRANT_MAX_TTL_MS UINT64_C(300000)
#define AURORA_IDENTITY_SESSION_GRANT_RECORD_VERSION 1u

enum aurora_identity_session_grant_result {
    AURORA_IDENTITY_SESSION_GRANT_OK = 0,
    AURORA_IDENTITY_SESSION_GRANT_INVALID_ARGUMENT,
    AURORA_IDENTITY_SESSION_GRANT_INVALID_USER,
    AURORA_IDENTITY_SESSION_GRANT_POLICY_ERROR,
    AURORA_IDENTITY_SESSION_GRANT_RANDOM_ERROR,
    AURORA_IDENTITY_SESSION_GRANT_CRYPTO_ERROR,
    AURORA_IDENTITY_SESSION_GRANT_BACKEND_ERROR,
    AURORA_IDENTITY_SESSION_GRANT_NOT_FOUND,
    AURORA_IDENTITY_SESSION_GRANT_EXPIRED
};

enum aurora_identity_session_grant_store_issue_result {
    AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_OK = 0,
    AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_CONFLICT,
    AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_ERROR
};

enum aurora_identity_session_grant_store_consume_result {
    AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_OK = 0,
    AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_NOT_FOUND,
    AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_EXPIRED,
    AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_ERROR
};

struct aurora_identity_session_grant_token {
    uint8_t bytes[AURORA_IDENTITY_SESSION_GRANT_TOKEN_SIZE];
};

struct aurora_identity_session_grant_policy {
    uint64_t ttl_ms;
};

struct aurora_identity_session_grant_record {
    struct aurora_identity_user_id user_id;
    uint8_t token_tag[AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE];
    uint64_t issued_at_ms;
    uint64_t expires_at_ms;
    uint32_t record_version;
};

struct aurora_identity_session_grant_crypto_ops {
    void *context;

    /*
     * Derive the backend lookup representation for a high-entropy opaque
     * grant token. Production must use a reviewed cryptographic primitive.
     * The raw bearer token must not be persisted by the grant store.
     */
    bool (*derive_token_tag)(
        void *context,
        const uint8_t token[AURORA_IDENTITY_SESSION_GRANT_TOKEN_SIZE],
        uint8_t out_tag[AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE]);
};

struct aurora_identity_session_grant_store_ops {
    void *context;

    /*
     * Publish a transient grant record. The backend must enforce unique tags.
     * Grant records are not durable identity state and must not survive reboot
     * or an Identity Service generation change.
     */
    enum aurora_identity_session_grant_store_issue_result (*issue_grant)(
        void *context,
        const struct aurora_identity_session_grant_record *record);

    /*
     * Atomically validate and consume a grant identified by token_tag.
     * On CONSUME_OK the record is irreversibly consumed before this call
     * returns, so concurrent/replayed consumption cannot also succeed.
     * Expired records should be invalidated while returning EXPIRED.
     */
    enum aurora_identity_session_grant_store_consume_result (*consume_grant)(
        void *context,
        const uint8_t token_tag[AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE],
        uint64_t now_ms,
        struct aurora_identity_session_grant_record *out_record);
};

struct aurora_identity_session_grant_core {
    struct aurora_identity_random_ops random;
    struct aurora_identity_clock_ops clock;
    struct aurora_identity_session_grant_crypto_ops crypto;
    struct aurora_identity_session_grant_store_ops store;
    struct aurora_identity_session_grant_policy policy;
};

struct aurora_identity_session_grant_issue_result {
    enum aurora_identity_session_grant_result result;
    struct aurora_identity_session_grant_token token;
    uint64_t expires_at_ms;
};

struct aurora_identity_session_grant_consume_result {
    enum aurora_identity_session_grant_result result;
    struct aurora_identity_user_id user_id;
};

struct aurora_identity_session_grant_issue_result
    aurora_identity_session_grant_issue(
        const struct aurora_identity_session_grant_core *core,
        const struct aurora_identity_user_id *authenticated_user_id);

struct aurora_identity_session_grant_consume_result
    aurora_identity_session_grant_consume(
        const struct aurora_identity_session_grant_core *core,
        const struct aurora_identity_session_grant_token *token);

bool aurora_identity_session_grant_token_is_zero(
    const struct aurora_identity_session_grant_token *token);

#endif
