#ifndef AURORA_IDENTITY_CORE_H
#define AURORA_IDENTITY_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURORA_IDENTITY_KEY_MIN_LEN 12u
#define AURORA_IDENTITY_KEY_MAX_LEN 32u
#define AURORA_IDENTITY_LOOKUP_TAG_SIZE 32u
#define AURORA_IDENTITY_USER_ID_SIZE 16u
#define AURORA_IDENTITY_SALT_MAX_SIZE 32u
#define AURORA_IDENTITY_VERIFIER_MAX_SIZE 64u

#define AURORA_IDENTITY_KDF_ARGON2ID 1u

enum aurora_identity_result {
    AURORA_IDENTITY_OK = 0,
    AURORA_IDENTITY_INVALID_ARGUMENT,
    AURORA_IDENTITY_INVALID_KEY_FORMAT,
    AURORA_IDENTITY_NOT_FOUND,
    AURORA_IDENTITY_AUTH_FAILED,
    AURORA_IDENTITY_THROTTLED,
    AURORA_IDENTITY_DISABLED,
    AURORA_IDENTITY_RECOVERY_REQUIRED,
    AURORA_IDENTITY_BACKEND_ERROR,
    AURORA_IDENTITY_CRYPTO_ERROR
};

enum aurora_identity_record_status {
    AURORA_IDENTITY_RECORD_ACTIVE = 0,
    AURORA_IDENTITY_RECORD_DISABLED,
    AURORA_IDENTITY_RECORD_RECOVERY_REQUIRED
};

enum aurora_identity_store_result {
    AURORA_IDENTITY_STORE_OK = 0,
    AURORA_IDENTITY_STORE_NOT_FOUND,
    AURORA_IDENTITY_STORE_ERROR
};

struct aurora_identity_user_id {
    uint8_t bytes[AURORA_IDENTITY_USER_ID_SIZE];
};

struct aurora_identity_normalized_key {
    char bytes[AURORA_IDENTITY_KEY_MAX_LEN + 1u];
    size_t length;
};

struct aurora_identity_kdf_params {
    uint32_t algorithm;
    uint32_t parameters_version;
    uint32_t memory_kib;
    uint32_t time_cost;
    uint32_t parallelism;
};

struct aurora_identity_key_record {
    struct aurora_identity_user_id user_id;
    uint8_t lookup_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE];
    struct aurora_identity_kdf_params kdf;
    uint8_t salt[AURORA_IDENTITY_SALT_MAX_SIZE];
    size_t salt_size;
    uint8_t verifier[AURORA_IDENTITY_VERIFIER_MAX_SIZE];
    size_t verifier_size;
    enum aurora_identity_record_status status;
    uint32_t failed_attempts;
    uint64_t throttle_until_ms;
};

struct aurora_identity_throttle_policy {
    uint32_t free_failures;
    uint64_t initial_delay_ms;
    uint64_t maximum_delay_ms;
};

struct aurora_identity_auth_result {
    enum aurora_identity_result result;
    struct aurora_identity_user_id user_id;
    uint64_t retry_after_ms;
};

/*
 * Production implementations must derive a keyed, opaque lookup tag from the
 * normalized Aurora Key. A plain hash of the Key is not an acceptable
 * production implementation because it would create an offline guessing
 * oracle if the identity database were copied.
 */
struct aurora_identity_crypto_ops {
    void *context;

    bool (*derive_lookup_tag)(
        void *context,
        const char *normalized_key,
        size_t normalized_key_length,
        uint8_t out_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE]);

    /*
     * Verify the normalized Key against the record's configured KDF/verifier.
     * The production provider is expected to implement the reviewed Argon2id
     * path and constant-time verifier comparison where applicable.
     */
    bool (*verify_key)(
        void *context,
        const char *normalized_key,
        size_t normalized_key_length,
        const struct aurora_identity_key_record *record,
        bool *out_matches);
};

struct aurora_identity_store_ops {
    void *context;

    enum aurora_identity_store_result (*find_key_record_by_lookup_tag)(
        void *context,
        const uint8_t lookup_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE],
        struct aurora_identity_key_record *out_record);

    bool (*store_failure_state)(
        void *context,
        const struct aurora_identity_user_id *user_id,
        uint32_t failed_attempts,
        uint64_t throttle_until_ms);

    bool (*clear_failure_state)(
        void *context,
        const struct aurora_identity_user_id *user_id);
};

struct aurora_identity_clock_ops {
    void *context;
    bool (*monotonic_ms)(void *context, uint64_t *out_now_ms);
};

struct aurora_identity_core {
    struct aurora_identity_crypto_ops crypto;
    struct aurora_identity_store_ops store;
    struct aurora_identity_clock_ops clock;
    struct aurora_identity_throttle_policy throttle_policy;
};

enum aurora_identity_result aurora_identity_normalize_key(
    const char *input,
    size_t input_length,
    struct aurora_identity_normalized_key *out_key);

uint64_t aurora_identity_compute_throttle_delay_ms(
    const struct aurora_identity_throttle_policy *policy,
    uint32_t failed_attempts);

struct aurora_identity_auth_result aurora_identity_authenticate_key(
    const struct aurora_identity_core *core,
    const char *candidate_key,
    size_t candidate_key_length);

bool aurora_identity_user_id_is_zero(const struct aurora_identity_user_id *user_id);

void aurora_identity_secure_zero(void *buffer, size_t size);

#endif
