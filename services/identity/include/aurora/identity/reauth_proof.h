#ifndef AURORA_IDENTITY_REAUTH_PROOF_H
#define AURORA_IDENTITY_REAUTH_PROOF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aurora/identity/core.h"

#define AURORA_IDENTITY_REAUTH_PROOF_TOKEN_SIZE 32u
#define AURORA_IDENTITY_REAUTH_PROOF_TAG_SIZE 32u
#define AURORA_IDENTITY_REAUTH_PROOF_GENERATION_ATTEMPTS 4u
#define AURORA_IDENTITY_REAUTH_PROOF_MAX_TTL_MS UINT64_C(120000)
#define AURORA_IDENTITY_REAUTH_PROOF_RECORD_VERSION 1u

enum aurora_identity_reauth_purpose {
    AURORA_IDENTITY_REAUTH_PURPOSE_NONE = 0,
    AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY = 1,
    AURORA_IDENTITY_REAUTH_PURPOSE_ENROLL_AUTHENTICATOR = 2,
    AURORA_IDENTITY_REAUTH_PURPOSE_REVOKE_AUTHENTICATOR = 3,
    AURORA_IDENTITY_REAUTH_PURPOSE_CHANGE_RECOVERY_POLICY = 4,
    AURORA_IDENTITY_REAUTH_PURPOSE_EXPORT_RECOVERY_MATERIAL = 5,
    AURORA_IDENTITY_REAUTH_PURPOSE_APPROVE_USER_CREATION = 6,
    AURORA_IDENTITY_REAUTH_PURPOSE_CHANGE_LOCAL_ROLE = 7,
    AURORA_IDENTITY_REAUTH_PURPOSE_GRANT_RESOURCE_ACCESS = 8,
    AURORA_IDENTITY_REAUTH_PURPOSE_COUNT
};

enum aurora_identity_reauth_result {
    AURORA_IDENTITY_REAUTH_OK = 0,
    AURORA_IDENTITY_REAUTH_INVALID_ARGUMENT,
    AURORA_IDENTITY_REAUTH_INVALID_USER,
    AURORA_IDENTITY_REAUTH_INVALID_PURPOSE,
    AURORA_IDENTITY_REAUTH_POLICY_ERROR,
    AURORA_IDENTITY_REAUTH_RANDOM_ERROR,
    AURORA_IDENTITY_REAUTH_CRYPTO_ERROR,
    AURORA_IDENTITY_REAUTH_BACKEND_ERROR,
    AURORA_IDENTITY_REAUTH_NOT_FOUND,
    AURORA_IDENTITY_REAUTH_EXPIRED,
    AURORA_IDENTITY_REAUTH_USER_MISMATCH,
    AURORA_IDENTITY_REAUTH_PURPOSE_MISMATCH
};

enum aurora_identity_reauth_store_issue_result {
    AURORA_IDENTITY_REAUTH_STORE_ISSUE_OK = 0,
    AURORA_IDENTITY_REAUTH_STORE_ISSUE_CONFLICT,
    AURORA_IDENTITY_REAUTH_STORE_ISSUE_ERROR
};

enum aurora_identity_reauth_store_consume_result {
    AURORA_IDENTITY_REAUTH_STORE_CONSUME_OK = 0,
    AURORA_IDENTITY_REAUTH_STORE_CONSUME_NOT_FOUND,
    AURORA_IDENTITY_REAUTH_STORE_CONSUME_EXPIRED,
    AURORA_IDENTITY_REAUTH_STORE_CONSUME_ERROR
};

struct aurora_identity_reauth_token {
    uint8_t bytes[AURORA_IDENTITY_REAUTH_PROOF_TOKEN_SIZE];
};

struct aurora_identity_reauth_policy {
    uint64_t ttl_ms;
};

struct aurora_identity_reauth_record {
    struct aurora_identity_user_id user_id;
    uint32_t purpose;
    uint32_t record_version;
    uint8_t token_tag[AURORA_IDENTITY_REAUTH_PROOF_TAG_SIZE];
    uint64_t issued_at_ms;
    uint64_t expires_at_ms;
};

struct aurora_identity_reauth_crypto_ops {
    void *context;
    bool (*derive_token_tag)(
        void *context,
        const uint8_t token[AURORA_IDENTITY_REAUTH_PROOF_TOKEN_SIZE],
        uint8_t out_tag[AURORA_IDENTITY_REAUTH_PROOF_TAG_SIZE]);
};

struct aurora_identity_reauth_store_ops {
    void *context;
    enum aurora_identity_reauth_store_issue_result (*issue_proof)(
        void *context,
        const struct aurora_identity_reauth_record *record);
    enum aurora_identity_reauth_store_consume_result (*consume_proof)(
        void *context,
        const uint8_t token_tag[AURORA_IDENTITY_REAUTH_PROOF_TAG_SIZE],
        uint64_t now_ms,
        struct aurora_identity_reauth_record *out_record);
};

struct aurora_identity_reauth_core {
    struct aurora_identity_random_ops random;
    struct aurora_identity_clock_ops clock;
    struct aurora_identity_reauth_crypto_ops crypto;
    struct aurora_identity_reauth_store_ops store;
    struct aurora_identity_reauth_policy policy;
};

struct aurora_identity_reauth_issue_result {
    enum aurora_identity_reauth_result result;
    struct aurora_identity_reauth_token token;
    uint64_t expires_at_ms;
};

struct aurora_identity_reauth_consume_result {
    enum aurora_identity_reauth_result result;
    struct aurora_identity_user_id user_id;
    uint32_t purpose;
};

bool aurora_identity_reauth_purpose_valid(uint32_t purpose);
bool aurora_identity_reauth_token_is_zero(
    const struct aurora_identity_reauth_token *token);

struct aurora_identity_reauth_issue_result aurora_identity_reauth_issue(
    const struct aurora_identity_reauth_core *core,
    const struct aurora_identity_user_id *authenticated_user_id,
    uint32_t purpose);

struct aurora_identity_reauth_consume_result aurora_identity_reauth_consume(
    const struct aurora_identity_reauth_core *core,
    const struct aurora_identity_reauth_token *token,
    const struct aurora_identity_user_id *expected_user_id,
    uint32_t expected_purpose);

#endif
