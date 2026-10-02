#include "aurora/identity/core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fake_crypto_context {
    bool fail_lookup;
    bool fail_verifier_derivation;
    unsigned int lookup_calls;
    unsigned int derive_calls;
    unsigned int verify_calls;
};

struct fake_random_context {
    bool fail;
    unsigned int zero_calls_remaining;
    unsigned int calls;
    uint8_t seed;
};

struct fake_store_context {
    bool fail_lookup;
    bool fail_create;
    bool force_conflict;
    bool has_key;
    bool has_identity;
    unsigned int create_calls;
    struct aurora_identity_record identity;
    struct aurora_identity_key_record key_record;
};

struct fake_clock_context {
    uint64_t now_ms;
};

static void fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fail(message); \
        } \
    } while (0)

static void fake_tag_for_key(
    const char *key,
    size_t key_length,
    uint8_t out_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE]) {
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0u; index < key_length; ++index) {
        hash ^= (uint8_t)key[index];
        hash *= UINT64_C(1099511628211);
    }

    for (index = 0u; index < AURORA_IDENTITY_LOOKUP_TAG_SIZE; ++index) {
        out_tag[index] = (uint8_t)(hash >> ((index % 8u) * 8u));
    }
}

/* Test-only deterministic stand-in. Never a production credential primitive. */
static bool fake_derive_lookup_tag(
    void *opaque,
    const char *normalized_key,
    size_t normalized_key_length,
    uint8_t out_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE]) {
    struct fake_crypto_context *context = (struct fake_crypto_context *)opaque;

    ++context->lookup_calls;
    if (context->fail_lookup) {
        return false;
    }

    fake_tag_for_key(normalized_key, normalized_key_length, out_tag);
    return true;
}

/* Test-only deterministic stand-in. Production must use reviewed Argon2id. */
static bool fake_derive_key_verifier(
    void *opaque,
    const char *normalized_key,
    size_t normalized_key_length,
    const struct aurora_identity_kdf_params *kdf,
    const uint8_t *salt,
    size_t salt_size,
    uint8_t *out_verifier,
    size_t verifier_size) {
    struct fake_crypto_context *context = (struct fake_crypto_context *)opaque;
    uint8_t accumulator;
    size_t index;

    ++context->derive_calls;
    if (context->fail_verifier_derivation) {
        return false;
    }

    accumulator = (uint8_t)(kdf->parameters_version ^ kdf->time_cost ^ kdf->parallelism);
    for (index = 0u; index < normalized_key_length; ++index) {
        accumulator = (uint8_t)(accumulator + (uint8_t)normalized_key[index]);
        accumulator = (uint8_t)((accumulator << 1u) | (accumulator >> 7u));
    }
    for (index = 0u; index < salt_size; ++index) {
        accumulator ^= salt[index];
        accumulator = (uint8_t)(accumulator + 0x3Du);
    }
    for (index = 0u; index < verifier_size; ++index) {
        out_verifier[index] = (uint8_t)(accumulator + (uint8_t)(index * 17u));
    }

    return true;
}

static bool fake_verify_key(
    void *opaque,
    const char *normalized_key,
    size_t normalized_key_length,
    const struct aurora_identity_key_record *record,
    bool *out_matches) {
    struct fake_crypto_context *context = (struct fake_crypto_context *)opaque;
    uint8_t verifier[AURORA_IDENTITY_VERIFIER_MAX_SIZE];
    bool derived;

    memset(verifier, 0, sizeof(verifier));
    ++context->verify_calls;

    derived = fake_derive_key_verifier(
        opaque,
        normalized_key,
        normalized_key_length,
        &record->kdf,
        record->salt,
        record->salt_size,
        verifier,
        record->verifier_size);
    if (!derived) {
        aurora_identity_secure_zero(verifier, sizeof(verifier));
        return false;
    }

    *out_matches = memcmp(verifier, record->verifier, record->verifier_size) == 0;
    aurora_identity_secure_zero(verifier, sizeof(verifier));
    return true;
}

static bool fake_fill_random(void *opaque, uint8_t *buffer, size_t size) {
    struct fake_random_context *context = (struct fake_random_context *)opaque;
    size_t index;

    ++context->calls;
    if (context->fail) {
        return false;
    }

    if (context->zero_calls_remaining > 0u) {
        --context->zero_calls_remaining;
        memset(buffer, 0, size);
        return true;
    }

    for (index = 0u; index < size; ++index) {
        context->seed = (uint8_t)(context->seed + 29u);
        if (context->seed == 0u) {
            context->seed = 1u;
        }
        buffer[index] = context->seed;
    }

    return true;
}

static enum aurora_identity_store_result fake_find_record(
    void *opaque,
    const uint8_t lookup_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE],
    struct aurora_identity_key_record *out_record) {
    struct fake_store_context *context = (struct fake_store_context *)opaque;

    if (context->fail_lookup) {
        return AURORA_IDENTITY_STORE_ERROR;
    }
    if (!context->has_key ||
        memcmp(lookup_tag, context->key_record.lookup_tag,
               AURORA_IDENTITY_LOOKUP_TAG_SIZE) != 0) {
        return AURORA_IDENTITY_STORE_NOT_FOUND;
    }

    *out_record = context->key_record;
    return AURORA_IDENTITY_STORE_OK;
}

static bool fake_store_failure(
    void *opaque,
    const struct aurora_identity_user_id *user_id,
    uint32_t failed_attempts,
    uint64_t throttle_until_ms) {
    struct fake_store_context *context = (struct fake_store_context *)opaque;
    (void)user_id;

    if (!context->has_key) {
        return false;
    }

    context->key_record.failed_attempts = failed_attempts;
    context->key_record.throttle_until_ms = throttle_until_ms;
    return true;
}

static bool fake_clear_failure(
    void *opaque,
    const struct aurora_identity_user_id *user_id) {
    struct fake_store_context *context = (struct fake_store_context *)opaque;
    (void)user_id;

    if (!context->has_key) {
        return false;
    }

    context->key_record.failed_attempts = 0u;
    context->key_record.throttle_until_ms = 0u;
    return true;
}

static enum aurora_identity_store_create_result fake_create_identity_with_key(
    void *opaque,
    const struct aurora_identity_record *identity,
    const struct aurora_identity_key_record *key_record) {
    struct fake_store_context *context = (struct fake_store_context *)opaque;

    ++context->create_calls;

    if (context->fail_create) {
        return AURORA_IDENTITY_STORE_CREATE_ERROR;
    }
    if (context->force_conflict || context->has_key || context->has_identity) {
        return AURORA_IDENTITY_STORE_CREATE_CONFLICT;
    }

    /* Atomic publication in the fake store: copy both, then expose both. */
    context->identity = *identity;
    context->key_record = *key_record;
    context->has_identity = true;
    context->has_key = true;
    return AURORA_IDENTITY_STORE_CREATE_OK;
}

static bool fake_monotonic_ms(void *opaque, uint64_t *out_now_ms) {
    struct fake_clock_context *context = (struct fake_clock_context *)opaque;
    *out_now_ms = context->now_ms;
    return true;
}

static struct aurora_identity_core make_core(
    struct fake_crypto_context *crypto,
    struct fake_random_context *random,
    struct fake_store_context *store,
    struct fake_clock_context *clock) {
    struct aurora_identity_core core;
    memset(&core, 0, sizeof(core));

    core.crypto.context = crypto;
    core.crypto.derive_lookup_tag = fake_derive_lookup_tag;
    core.crypto.derive_key_verifier = fake_derive_key_verifier;
    core.crypto.verify_key = fake_verify_key;

    core.random.context = random;
    core.random.fill_random = fake_fill_random;

    core.store.context = store;
    core.store.find_key_record_by_lookup_tag = fake_find_record;
    core.store.store_failure_state = fake_store_failure;
    core.store.clear_failure_state = fake_clear_failure;
    core.store.create_identity_with_key = fake_create_identity_with_key;

    core.clock.context = clock;
    core.clock.monotonic_ms = fake_monotonic_ms;

    core.throttle_policy.free_failures = 2u;
    core.throttle_policy.initial_delay_ms = 1000u;
    core.throttle_policy.maximum_delay_ms = 8000u;

    core.creation_policy.kdf.algorithm = AURORA_IDENTITY_KDF_ARGON2ID;
    core.creation_policy.kdf.parameters_version = 1u;
    core.creation_policy.kdf.memory_kib = 65536u;
    core.creation_policy.kdf.time_cost = 3u;
    core.creation_policy.kdf.parallelism = 1u;
    core.creation_policy.salt_size = 16u;
    core.creation_policy.verifier_size = 32u;
    core.creation_policy.identity_record_version = 1u;
    core.creation_policy.policy_version = 1u;

    return core;
}

static void seed_existing_key(
    struct fake_store_context *store,
    const char *normalized_key,
    size_t normalized_key_length) {
    memset(store, 0, sizeof(*store));
    store->has_key = true;
    store->has_identity = true;
    store->key_record.status = AURORA_IDENTITY_RECORD_ACTIVE;
    fake_tag_for_key(
        normalized_key,
        normalized_key_length,
        store->key_record.lookup_tag);
}

static void test_creation_success_and_authentication(void) {
    struct fake_crypto_context crypto = {false, false, 0u, 0u, 0u};
    struct fake_random_context random = {false, 0u, 0u, 7u};
    struct fake_store_context store;
    struct fake_clock_context clock = {5000u};
    struct aurora_identity_core core;
    struct aurora_identity_create_result created;
    struct aurora_identity_auth_result authenticated;
    const char *formatted_key = "aur-7k4p-n9q2-xm6d";

    memset(&store, 0, sizeof(store));
    core = make_core(&crypto, &random, &store, &clock);

    created = aurora_identity_create_with_key(
        &core, formatted_key, strlen(formatted_key));

    CHECK(created.result == AURORA_IDENTITY_OK, "identity creation should succeed");
    CHECK(!aurora_identity_user_id_is_zero(&created.user_id), "created user id must be nonzero");
    CHECK(!aurora_identity_credential_id_is_zero(&created.credential_id),
          "created credential id must be nonzero");
    CHECK(store.has_identity && store.has_key, "identity and key must publish together");
    CHECK(store.create_calls == 1u, "atomic create must run once");
    CHECK(store.identity.record_version == 1u, "identity record version mismatch");
    CHECK(store.identity.policy_version == 1u, "identity policy version mismatch");
    CHECK(store.key_record.kdf.algorithm == AURORA_IDENTITY_KDF_ARGON2ID,
          "created key must retain KDF metadata");
    CHECK(store.key_record.salt_size == 16u, "created salt size mismatch");
    CHECK(store.key_record.verifier_size == 32u, "created verifier size mismatch");
    CHECK(crypto.derive_calls == 1u, "creation must derive one verifier");
    CHECK(random.calls == 3u, "creation should request user id, credential id and salt");

    authenticated = aurora_identity_authenticate_key(
        &core, formatted_key, strlen(formatted_key));
    CHECK(authenticated.result == AURORA_IDENTITY_OK,
          "freshly created identity should authenticate through the same store");
    CHECK(memcmp(authenticated.user_id.bytes, created.user_id.bytes,
                 AURORA_IDENTITY_USER_ID_SIZE) == 0,
          "authentication must resolve the created identity");
}

static void test_preflight_duplicate_is_rejected_without_expensive_work(void) {
    struct fake_crypto_context crypto = {false, false, 0u, 0u, 0u};
    struct fake_random_context random = {false, 0u, 0u, 9u};
    struct fake_store_context store;
    struct fake_clock_context clock = {0u};
    struct aurora_identity_core core;
    struct aurora_identity_create_result result;
    const char *normalized_key = "AUR7K4PN9Q2XM6D";

    seed_existing_key(&store, normalized_key, strlen(normalized_key));
    core = make_core(&crypto, &random, &store, &clock);

    result = aurora_identity_create_with_key(
        &core, normalized_key, strlen(normalized_key));

    CHECK(result.result == AURORA_IDENTITY_ALREADY_EXISTS,
          "existing lookup tag must reject duplicate creation");
    CHECK(random.calls == 0u, "duplicate preflight must not consume secure randomness");
    CHECK(crypto.derive_calls == 0u, "duplicate preflight must not run Argon2id provider");
    CHECK(store.create_calls == 0u, "duplicate preflight must not begin publication");
}

static void test_commit_conflict_is_atomic(void) {
    struct fake_crypto_context crypto = {false, false, 0u, 0u, 0u};
    struct fake_random_context random = {false, 0u, 0u, 11u};
    struct fake_store_context store;
    struct fake_clock_context clock = {0u};
    struct aurora_identity_core core;
    struct aurora_identity_create_result result;

    memset(&store, 0, sizeof(store));
    store.force_conflict = true;
    core = make_core(&crypto, &random, &store, &clock);

    result = aurora_identity_create_with_key(
        &core, "AUR7K4PN9Q2XM6D", strlen("AUR7K4PN9Q2XM6D"));

    CHECK(result.result == AURORA_IDENTITY_ALREADY_EXISTS,
          "commit-time uniqueness race must become a conflict");
    CHECK(store.create_calls == 1u, "atomic publication should be attempted once");
    CHECK(!store.has_identity && !store.has_key,
          "conflict must not expose a partial identity or credential");
}

static void test_backend_create_failure_is_atomic(void) {
    struct fake_crypto_context crypto = {false, false, 0u, 0u, 0u};
    struct fake_random_context random = {false, 0u, 0u, 13u};
    struct fake_store_context store;
    struct fake_clock_context clock = {0u};
    struct aurora_identity_core core;
    struct aurora_identity_create_result result;

    memset(&store, 0, sizeof(store));
    store.fail_create = true;
    core = make_core(&crypto, &random, &store, &clock);

    result = aurora_identity_create_with_key(
        &core, "AUR7K4PN9Q2XM6D", strlen("AUR7K4PN9Q2XM6D"));

    CHECK(result.result == AURORA_IDENTITY_BACKEND_ERROR,
          "store failure must fail closed");
    CHECK(!store.has_identity && !store.has_key,
          "store failure must not publish partial records");
}

static void test_random_failure_prevents_publication(void) {
    struct fake_crypto_context crypto = {false, false, 0u, 0u, 0u};
    struct fake_random_context random = {true, 0u, 0u, 0u};
    struct fake_store_context store;
    struct fake_clock_context clock = {0u};
    struct aurora_identity_core core;
    struct aurora_identity_create_result result;

    memset(&store, 0, sizeof(store));
    core = make_core(&crypto, &random, &store, &clock);

    result = aurora_identity_create_with_key(
        &core, "AUR7K4PN9Q2XM6D", strlen("AUR7K4PN9Q2XM6D"));

    CHECK(result.result == AURORA_IDENTITY_RANDOM_ERROR,
          "secure RNG failure must fail creation");
    CHECK(store.create_calls == 0u, "RNG failure must occur before publication");
    CHECK(crypto.derive_calls == 0u, "RNG failure before salt must avoid verifier derivation");
}

static void test_zero_identifier_is_retried(void) {
    struct fake_crypto_context crypto = {false, false, 0u, 0u, 0u};
    struct fake_random_context random = {false, 1u, 0u, 17u};
    struct fake_store_context store;
    struct fake_clock_context clock = {0u};
    struct aurora_identity_core core;
    struct aurora_identity_create_result result;

    memset(&store, 0, sizeof(store));
    core = make_core(&crypto, &random, &store, &clock);

    result = aurora_identity_create_with_key(
        &core, "AUR7K4PN9Q2XM6D", strlen("AUR7K4PN9Q2XM6D"));

    CHECK(result.result == AURORA_IDENTITY_OK, "zero random identifier should be retried");
    CHECK(random.calls == 4u, "one zero user-id attempt should add exactly one RNG call");
}

static void test_repeated_zero_identifier_fails_closed(void) {
    struct fake_crypto_context crypto = {false, false, 0u, 0u, 0u};
    struct fake_random_context random = {false, AURORA_IDENTITY_ID_GENERATION_ATTEMPTS, 0u, 0u};
    struct fake_store_context store;
    struct fake_clock_context clock = {0u};
    struct aurora_identity_core core;
    struct aurora_identity_create_result result;

    memset(&store, 0, sizeof(store));
    core = make_core(&crypto, &random, &store, &clock);

    result = aurora_identity_create_with_key(
        &core, "AUR7K4PN9Q2XM6D", strlen("AUR7K4PN9Q2XM6D"));

    CHECK(result.result == AURORA_IDENTITY_RANDOM_ERROR,
          "repeated zero identifiers must fail closed");
    CHECK(random.calls == AURORA_IDENTITY_ID_GENERATION_ATTEMPTS,
          "identifier retries must be bounded");
    CHECK(store.create_calls == 0u, "invalid generated identity must never publish");
}

static void test_verifier_derivation_failure_prevents_publication(void) {
    struct fake_crypto_context crypto = {false, true, 0u, 0u, 0u};
    struct fake_random_context random = {false, 0u, 0u, 19u};
    struct fake_store_context store;
    struct fake_clock_context clock = {0u};
    struct aurora_identity_core core;
    struct aurora_identity_create_result result;

    memset(&store, 0, sizeof(store));
    core = make_core(&crypto, &random, &store, &clock);

    result = aurora_identity_create_with_key(
        &core, "AUR7K4PN9Q2XM6D", strlen("AUR7K4PN9Q2XM6D"));

    CHECK(result.result == AURORA_IDENTITY_CRYPTO_ERROR,
          "verifier derivation failure must fail creation");
    CHECK(store.create_calls == 0u, "crypto failure must happen before publication");
}

static void test_invalid_creation_policy_fails_before_secret_work(void) {
    struct fake_crypto_context crypto = {false, false, 0u, 0u, 0u};
    struct fake_random_context random = {false, 0u, 0u, 23u};
    struct fake_store_context store;
    struct fake_clock_context clock = {0u};
    struct aurora_identity_core core;
    struct aurora_identity_create_result result;

    memset(&store, 0, sizeof(store));
    core = make_core(&crypto, &random, &store, &clock);
    core.creation_policy.kdf.memory_kib = 0u;

    result = aurora_identity_create_with_key(
        &core, "AUR7K4PN9Q2XM6D", strlen("AUR7K4PN9Q2XM6D"));

    CHECK(result.result == AURORA_IDENTITY_POLICY_ERROR,
          "invalid creation policy must be rejected");
    CHECK(crypto.lookup_calls == 0u, "invalid policy should fail before lookup derivation");
    CHECK(random.calls == 0u, "invalid policy should fail before RNG use");
    CHECK(store.create_calls == 0u, "invalid policy must never publish");
}

int main(void) {
    test_creation_success_and_authentication();
    test_preflight_duplicate_is_rejected_without_expensive_work();
    test_commit_conflict_is_atomic();
    test_backend_create_failure_is_atomic();
    test_random_failure_prevents_publication();
    test_zero_identifier_is_retried();
    test_repeated_zero_identifier_fails_closed();
    test_verifier_derivation_failure_prevents_publication();
    test_invalid_creation_policy_fails_before_secret_work();

    puts("Aurora Identity creation tests: PASS");
    return 0;
}
