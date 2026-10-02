#include "aurora/identity/core.h"
#include "aurora/identity/rotation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fake_crypto_context {
    bool fail_verifier_derivation;
};

struct fake_random_context {
    uint8_t next_byte;
    bool fail;
};

struct fake_store_context {
    struct aurora_identity_key_record record;
    bool has_record;
    bool fail_reads;
    bool fail_replace;
    bool force_conflict;
    unsigned int replace_calls;
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

static bool fake_derive_lookup_tag(
    void *opaque,
    const char *normalized_key,
    size_t normalized_key_length,
    uint8_t out_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE]) {
    (void)opaque;
    fake_tag_for_key(normalized_key, normalized_key_length, out_tag);
    return true;
}

static void fake_verifier_bytes(
    const char *normalized_key,
    size_t normalized_key_length,
    const uint8_t *salt,
    size_t salt_size,
    uint8_t *out_verifier,
    size_t verifier_size) {
    uint64_t state = UINT64_C(0xCBF29CE484222325);
    size_t index;

    for (index = 0u; index < normalized_key_length; ++index) {
        state ^= (uint8_t)normalized_key[index];
        state *= UINT64_C(0x100000001B3);
    }
    for (index = 0u; index < salt_size; ++index) {
        state ^= salt[index];
        state *= UINT64_C(0x100000001B3);
    }
    for (index = 0u; index < verifier_size; ++index) {
        state ^= (uint64_t)(index + 1u);
        state *= UINT64_C(0x100000001B3);
        out_verifier[index] = (uint8_t)(state >> ((index % 8u) * 8u));
    }
}

/* Test-only stand-in. Production must use the reviewed Argon2id provider. */
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
    (void)kdf;

    if (context->fail_verifier_derivation) {
        return false;
    }

    fake_verifier_bytes(
        normalized_key,
        normalized_key_length,
        salt,
        salt_size,
        out_verifier,
        verifier_size);
    return true;
}

static bool fake_verify_key(
    void *opaque,
    const char *normalized_key,
    size_t normalized_key_length,
    const struct aurora_identity_key_record *record,
    bool *out_matches) {
    uint8_t verifier[AURORA_IDENTITY_VERIFIER_MAX_SIZE];
    (void)opaque;

    memset(verifier, 0, sizeof(verifier));
    fake_verifier_bytes(
        normalized_key,
        normalized_key_length,
        record->salt,
        record->salt_size,
        verifier,
        record->verifier_size);

    *out_matches = memcmp(verifier, record->verifier, record->verifier_size) == 0;
    aurora_identity_secure_zero(verifier, sizeof(verifier));
    return true;
}

static bool fake_fill_random(void *opaque, uint8_t *buffer, size_t size) {
    struct fake_random_context *context = (struct fake_random_context *)opaque;
    size_t index;

    if (context->fail) {
        return false;
    }

    for (index = 0u; index < size; ++index) {
        buffer[index] = context->next_byte++;
        if (buffer[index] == 0u) {
            buffer[index] = context->next_byte++;
        }
    }
    return true;
}

static enum aurora_identity_store_result fake_find_record(
    void *opaque,
    const uint8_t lookup_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE],
    struct aurora_identity_key_record *out_record) {
    struct fake_store_context *context = (struct fake_store_context *)opaque;

    if (context->fail_reads) {
        return AURORA_IDENTITY_STORE_ERROR;
    }
    if (!context->has_record ||
        memcmp(context->record.lookup_tag,
               lookup_tag,
               AURORA_IDENTITY_LOOKUP_TAG_SIZE) != 0) {
        return AURORA_IDENTITY_STORE_NOT_FOUND;
    }

    *out_record = context->record;
    return AURORA_IDENTITY_STORE_OK;
}

static bool fake_store_failure(
    void *opaque,
    const struct aurora_identity_user_id *user_id,
    uint32_t failed_attempts,
    uint64_t throttle_until_ms) {
    struct fake_store_context *context = (struct fake_store_context *)opaque;

    if (!context->has_record ||
        memcmp(context->record.user_id.bytes,
               user_id->bytes,
               AURORA_IDENTITY_USER_ID_SIZE) != 0) {
        return false;
    }

    context->record.failed_attempts = failed_attempts;
    context->record.throttle_until_ms = throttle_until_ms;
    return true;
}

static bool fake_clear_failure(
    void *opaque,
    const struct aurora_identity_user_id *user_id) {
    struct fake_store_context *context = (struct fake_store_context *)opaque;

    if (!context->has_record ||
        memcmp(context->record.user_id.bytes,
               user_id->bytes,
               AURORA_IDENTITY_USER_ID_SIZE) != 0) {
        return false;
    }

    context->record.failed_attempts = 0u;
    context->record.throttle_until_ms = 0u;
    return true;
}

static enum aurora_identity_rotation_store_result fake_replace_key(
    void *opaque,
    const struct aurora_identity_user_id *user_id,
    const struct aurora_identity_credential_id *current_credential_id,
    const struct aurora_identity_key_record *replacement) {
    struct fake_store_context *context = (struct fake_store_context *)opaque;

    ++context->replace_calls;

    if (context->fail_replace) {
        return AURORA_IDENTITY_ROTATION_STORE_ERROR;
    }
    if (context->force_conflict) {
        return AURORA_IDENTITY_ROTATION_STORE_CONFLICT;
    }
    if (!context->has_record ||
        memcmp(context->record.user_id.bytes,
               user_id->bytes,
               AURORA_IDENTITY_USER_ID_SIZE) != 0 ||
        memcmp(context->record.credential_id.bytes,
               current_credential_id->bytes,
               AURORA_IDENTITY_CREDENTIAL_ID_SIZE) != 0) {
        return AURORA_IDENTITY_ROTATION_STORE_CURRENT_NOT_FOUND;
    }

    context->record = *replacement;
    context->has_record = true;
    return AURORA_IDENTITY_ROTATION_STORE_OK;
}

static bool fake_monotonic_ms(void *opaque, uint64_t *out_now_ms) {
    struct fake_clock_context *context = (struct fake_clock_context *)opaque;
    *out_now_ms = context->now_ms;
    return true;
}

static void fill_user_id(struct aurora_identity_user_id *user_id, uint8_t seed) {
    size_t index;
    for (index = 0u; index < AURORA_IDENTITY_USER_ID_SIZE; ++index) {
        user_id->bytes[index] = (uint8_t)(seed + index);
    }
}

static void fill_credential_id(
    struct aurora_identity_credential_id *credential_id,
    uint8_t seed) {
    size_t index;
    for (index = 0u; index < AURORA_IDENTITY_CREDENTIAL_ID_SIZE; ++index) {
        credential_id->bytes[index] = (uint8_t)(seed + index);
    }
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

static struct aurora_identity_rotation_context make_rotation_context(
    const struct aurora_identity_core *core,
    struct fake_store_context *store) {
    struct aurora_identity_rotation_context context;
    memset(&context, 0, sizeof(context));
    context.core = core;
    context.store.context = store;
    context.store.replace_key_credential = fake_replace_key;
    return context;
}

static void prepare_record(
    struct fake_crypto_context *crypto,
    struct fake_store_context *store,
    const char *normalized_key) {
    struct aurora_identity_kdf_params kdf;

    memset(store, 0, sizeof(*store));
    memset(&kdf, 0, sizeof(kdf));

    store->has_record = true;
    fill_user_id(&store->record.user_id, 1u);
    fill_credential_id(&store->record.credential_id, 32u);
    fake_tag_for_key(
        normalized_key,
        strlen(normalized_key),
        store->record.lookup_tag);

    kdf.algorithm = AURORA_IDENTITY_KDF_ARGON2ID;
    kdf.parameters_version = 1u;
    kdf.memory_kib = 65536u;
    kdf.time_cost = 3u;
    kdf.parallelism = 1u;
    store->record.kdf = kdf;
    store->record.salt_size = 16u;
    store->record.verifier_size = 32u;
    store->record.status = AURORA_IDENTITY_RECORD_ACTIVE;
    memset(store->record.salt, 0x5Au, store->record.salt_size);

    CHECK(fake_derive_key_verifier(
        crypto,
        normalized_key,
        strlen(normalized_key),
        &store->record.kdf,
        store->record.salt,
        store->record.salt_size,
        store->record.verifier,
        store->record.verifier_size),
        "record verifier preparation failed");
}

static void test_success_preserves_user_and_replaces_key(void) {
    const char *old_key = "AUR7K4PN9Q2XM6D";
    const char *new_key = "NEW8K4PN9Q2XM6D";
    struct fake_crypto_context crypto = {false};
    struct fake_random_context random = {70u, false};
    struct fake_store_context store;
    struct fake_clock_context clock = {1000u};
    struct aurora_identity_core core;
    struct aurora_identity_rotation_context rotation;
    struct aurora_identity_user_id original_user;
    struct aurora_identity_credential_id old_credential;
    struct aurora_identity_rotation_result rotate_result;
    struct aurora_identity_auth_result auth_result;

    prepare_record(&crypto, &store, old_key);
    original_user = store.record.user_id;
    old_credential = store.record.credential_id;
    core = make_core(&crypto, &random, &store, &clock);
    rotation = make_rotation_context(&core, &store);

    rotate_result = aurora_identity_rotate_key(
        &rotation,
        &original_user,
        &old_credential,
        new_key,
        strlen(new_key));

    CHECK(rotate_result.result == AURORA_IDENTITY_OK, "rotation should succeed");
    CHECK(store.replace_calls == 1u, "rotation store must be called once");
    CHECK(memcmp(store.record.user_id.bytes,
                 original_user.bytes,
                 AURORA_IDENTITY_USER_ID_SIZE) == 0,
          "rotation must preserve stable user id");
    CHECK(memcmp(store.record.credential_id.bytes,
                 old_credential.bytes,
                 AURORA_IDENTITY_CREDENTIAL_ID_SIZE) != 0,
          "rotation must issue a new credential id");
    CHECK(memcmp(rotate_result.new_credential_id.bytes,
                 store.record.credential_id.bytes,
                 AURORA_IDENTITY_CREDENTIAL_ID_SIZE) == 0,
          "rotation must return the published credential id");

    auth_result = aurora_identity_authenticate_key(
        &core, old_key, strlen(old_key));
    CHECK(auth_result.result == AURORA_IDENTITY_NOT_FOUND,
          "old key must stop resolving after atomic rotation");

    auth_result = aurora_identity_authenticate_key(
        &core, new_key, strlen(new_key));
    CHECK(auth_result.result == AURORA_IDENTITY_OK,
          "new key must authenticate after rotation");
    CHECK(memcmp(auth_result.user_id.bytes,
                 original_user.bytes,
                 AURORA_IDENTITY_USER_ID_SIZE) == 0,
          "new key must authenticate the same stable user");
}

static void test_same_key_is_rejected_before_write(void) {
    const char *old_key = "AUR7K4PN9Q2XM6D";
    struct fake_crypto_context crypto = {false};
    struct fake_random_context random = {80u, false};
    struct fake_store_context store;
    struct fake_clock_context clock = {1000u};
    struct aurora_identity_core core;
    struct aurora_identity_rotation_context rotation;
    struct aurora_identity_rotation_result result;
    struct aurora_identity_key_record before;

    prepare_record(&crypto, &store, old_key);
    before = store.record;
    core = make_core(&crypto, &random, &store, &clock);
    rotation = make_rotation_context(&core, &store);

    result = aurora_identity_rotate_key(
        &rotation,
        &store.record.user_id,
        &store.record.credential_id,
        old_key,
        strlen(old_key));

    CHECK(result.result == AURORA_IDENTITY_ALREADY_EXISTS,
          "rotating to the same key must be rejected");
    CHECK(store.replace_calls == 0u, "same-key rejection must not write");
    CHECK(memcmp(&before, &store.record, sizeof(before)) == 0,
          "same-key rejection must preserve old credential");
}

static void test_wrong_current_credential_fails_atomically(void) {
    const char *old_key = "AUR7K4PN9Q2XM6D";
    const char *new_key = "NEW8K4PN9Q2XM6D";
    struct fake_crypto_context crypto = {false};
    struct fake_random_context random = {90u, false};
    struct fake_store_context store;
    struct fake_clock_context clock = {1000u};
    struct aurora_identity_core core;
    struct aurora_identity_rotation_context rotation;
    struct aurora_identity_credential_id wrong_credential;
    struct aurora_identity_key_record before;
    struct aurora_identity_rotation_result result;

    prepare_record(&crypto, &store, old_key);
    before = store.record;
    fill_credential_id(&wrong_credential, 200u);
    core = make_core(&crypto, &random, &store, &clock);
    rotation = make_rotation_context(&core, &store);

    result = aurora_identity_rotate_key(
        &rotation,
        &store.record.user_id,
        &wrong_credential,
        new_key,
        strlen(new_key));

    CHECK(result.result == AURORA_IDENTITY_NOT_FOUND,
          "unknown current credential must fail closed");
    CHECK(memcmp(&before, &store.record, sizeof(before)) == 0,
          "failed rotation must preserve old credential");
}

static void test_conflict_preserves_old_credential(void) {
    const char *old_key = "AUR7K4PN9Q2XM6D";
    const char *new_key = "NEW8K4PN9Q2XM6D";
    struct fake_crypto_context crypto = {false};
    struct fake_random_context random = {100u, false};
    struct fake_store_context store;
    struct fake_clock_context clock = {1000u};
    struct aurora_identity_core core;
    struct aurora_identity_rotation_context rotation;
    struct aurora_identity_key_record before;
    struct aurora_identity_rotation_result result;

    prepare_record(&crypto, &store, old_key);
    before = store.record;
    store.force_conflict = true;
    core = make_core(&crypto, &random, &store, &clock);
    rotation = make_rotation_context(&core, &store);

    result = aurora_identity_rotate_key(
        &rotation,
        &store.record.user_id,
        &store.record.credential_id,
        new_key,
        strlen(new_key));

    CHECK(result.result == AURORA_IDENTITY_ALREADY_EXISTS,
          "commit-time uniqueness conflict must be surfaced");
    CHECK(memcmp(&before, &store.record, sizeof(before)) == 0,
          "conflict must preserve the old credential atomically");
}

static void test_crypto_and_random_failures_do_not_write(void) {
    const char *old_key = "AUR7K4PN9Q2XM6D";
    const char *new_key = "NEW8K4PN9Q2XM6D";
    struct fake_crypto_context crypto = {false};
    struct fake_random_context random = {110u, false};
    struct fake_store_context store;
    struct fake_clock_context clock = {1000u};
    struct aurora_identity_core core;
    struct aurora_identity_rotation_context rotation;
    struct aurora_identity_key_record before;
    struct aurora_identity_rotation_result result;

    prepare_record(&crypto, &store, old_key);
    before = store.record;
    core = make_core(&crypto, &random, &store, &clock);
    rotation = make_rotation_context(&core, &store);

    crypto.fail_verifier_derivation = true;
    result = aurora_identity_rotate_key(
        &rotation,
        &store.record.user_id,
        &store.record.credential_id,
        new_key,
        strlen(new_key));
    CHECK(result.result == AURORA_IDENTITY_CRYPTO_ERROR,
          "verifier derivation failure must fail closed");
    CHECK(store.replace_calls == 0u, "crypto failure must not publish");
    CHECK(memcmp(&before, &store.record, sizeof(before)) == 0,
          "crypto failure must preserve old credential");

    crypto.fail_verifier_derivation = false;
    random.fail = true;
    result = aurora_identity_rotate_key(
        &rotation,
        &store.record.user_id,
        &store.record.credential_id,
        new_key,
        strlen(new_key));
    CHECK(result.result == AURORA_IDENTITY_RANDOM_ERROR,
          "RNG failure must fail closed");
    CHECK(store.replace_calls == 0u, "RNG failure must not publish");
    CHECK(memcmp(&before, &store.record, sizeof(before)) == 0,
          "RNG failure must preserve old credential");
}

static void test_invalid_new_key_is_rejected(void) {
    const char *old_key = "AUR7K4PN9Q2XM6D";
    struct fake_crypto_context crypto = {false};
    struct fake_random_context random = {120u, false};
    struct fake_store_context store;
    struct fake_clock_context clock = {1000u};
    struct aurora_identity_core core;
    struct aurora_identity_rotation_context rotation;
    struct aurora_identity_rotation_result result;

    prepare_record(&crypto, &store, old_key);
    core = make_core(&crypto, &random, &store, &clock);
    rotation = make_rotation_context(&core, &store);

    result = aurora_identity_rotate_key(
        &rotation,
        &store.record.user_id,
        &store.record.credential_id,
        "SHORT-1",
        strlen("SHORT-1"));

    CHECK(result.result == AURORA_IDENTITY_INVALID_KEY_FORMAT,
          "invalid replacement key must be rejected");
    CHECK(store.replace_calls == 0u, "invalid key must not write");
}

int main(void) {
    test_success_preserves_user_and_replaces_key();
    test_same_key_is_rejected_before_write();
    test_wrong_current_credential_fails_atomically();
    test_conflict_preserves_old_credential();
    test_crypto_and_random_failures_do_not_write();
    test_invalid_new_key_is_rejected();

    puts("Aurora Identity rotation tests: PASS");
    return 0;
}
