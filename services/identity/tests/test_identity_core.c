#include "aurora/identity/core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fake_crypto_context {
    const char *expected_key;
    size_t expected_key_length;
    bool force_lookup_collision;
    unsigned int verify_calls;
};

struct fake_store_context {
    struct aurora_identity_key_record record;
    bool has_record;
    bool fail_reads;
    bool fail_writes;
    unsigned int failure_writes;
    unsigned int clear_writes;
    uint32_t last_failed_attempts;
    uint64_t last_throttle_until_ms;
};

struct fake_clock_context {
    uint64_t now_ms;
    bool fail;
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

    if (context->force_lookup_collision) {
        memset(out_tag, 0xA5, AURORA_IDENTITY_LOOKUP_TAG_SIZE);
        return true;
    }

    fake_tag_for_key(normalized_key, normalized_key_length, out_tag);
    return true;
}

/* Test-only verifier. Production must provide the reviewed Argon2id path. */
static bool fake_verify_key(
    void *opaque,
    const char *normalized_key,
    size_t normalized_key_length,
    const struct aurora_identity_key_record *record,
    bool *out_matches) {
    struct fake_crypto_context *context = (struct fake_crypto_context *)opaque;
    (void)record;

    ++context->verify_calls;
    *out_matches = normalized_key_length == context->expected_key_length &&
        memcmp(normalized_key, context->expected_key, normalized_key_length) == 0;
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
        memcmp(lookup_tag, context->record.lookup_tag, AURORA_IDENTITY_LOOKUP_TAG_SIZE) != 0) {
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
    (void)user_id;

    if (context->fail_writes) {
        return false;
    }

    ++context->failure_writes;
    context->last_failed_attempts = failed_attempts;
    context->last_throttle_until_ms = throttle_until_ms;
    return true;
}

static bool fake_clear_failure(
    void *opaque,
    const struct aurora_identity_user_id *user_id) {
    struct fake_store_context *context = (struct fake_store_context *)opaque;
    (void)user_id;

    if (context->fail_writes) {
        return false;
    }

    ++context->clear_writes;
    return true;
}

static bool fake_monotonic_ms(void *opaque, uint64_t *out_now_ms) {
    struct fake_clock_context *context = (struct fake_clock_context *)opaque;

    if (context->fail) {
        return false;
    }

    *out_now_ms = context->now_ms;
    return true;
}

static void fill_user_id(struct aurora_identity_user_id *user_id) {
    size_t index;
    for (index = 0u; index < AURORA_IDENTITY_USER_ID_SIZE; ++index) {
        user_id->bytes[index] = (uint8_t)(index + 1u);
    }
}

static struct aurora_identity_core make_core(
    struct fake_crypto_context *crypto,
    struct fake_store_context *store,
    struct fake_clock_context *clock) {
    struct aurora_identity_core core;
    memset(&core, 0, sizeof(core));

    core.crypto.context = crypto;
    core.crypto.derive_lookup_tag = fake_derive_lookup_tag;
    core.crypto.verify_key = fake_verify_key;

    core.store.context = store;
    core.store.find_key_record_by_lookup_tag = fake_find_record;
    core.store.store_failure_state = fake_store_failure;
    core.store.clear_failure_state = fake_clear_failure;

    core.clock.context = clock;
    core.clock.monotonic_ms = fake_monotonic_ms;

    core.throttle_policy.free_failures = 2u;
    core.throttle_policy.initial_delay_ms = 1000u;
    core.throttle_policy.maximum_delay_ms = 8000u;
    return core;
}

static void prepare_record(
    struct fake_crypto_context *crypto,
    struct fake_store_context *store) {
    memset(store, 0, sizeof(*store));
    store->has_record = true;
    store->record.status = AURORA_IDENTITY_RECORD_ACTIVE;
    store->record.kdf.algorithm = AURORA_IDENTITY_KDF_ARGON2ID;
    fill_user_id(&store->record.user_id);
    for (size_t i = 0u; i < AURORA_IDENTITY_CREDENTIAL_ID_SIZE; ++i) {
        store->record.credential_id.bytes[i] = (uint8_t)(0xC0u + (uint8_t)i);
    }

    if (crypto->force_lookup_collision) {
        memset(store->record.lookup_tag, 0xA5, AURORA_IDENTITY_LOOKUP_TAG_SIZE);
    } else {
        fake_tag_for_key(
            crypto->expected_key,
            crypto->expected_key_length,
            store->record.lookup_tag);
    }
}

static void test_normalization(void) {
    struct aurora_identity_normalized_key key;
    enum aurora_identity_result result;
    const char *input = "aur-7k4p-n9q2-xm6d";

    result = aurora_identity_normalize_key(input, strlen(input), &key);
    CHECK(result == AURORA_IDENTITY_OK, "formatted Aurora Key should normalize");
    CHECK(strcmp(key.bytes, "AUR7K4PN9Q2XM6D") == 0, "normalization output mismatch");
    CHECK(key.length == strlen("AUR7K4PN9Q2XM6D"), "normalization length mismatch");

    result = aurora_identity_normalize_key("SHORT-1", strlen("SHORT-1"), &key);
    CHECK(result == AURORA_IDENTITY_INVALID_KEY_FORMAT, "short key must be rejected");

    result = aurora_identity_normalize_key(
        "AUR7K4P_N9Q2XM6D", strlen("AUR7K4P_N9Q2XM6D"), &key);
    CHECK(result == AURORA_IDENTITY_INVALID_KEY_FORMAT, "unsupported character must be rejected");
}

static void test_throttle_policy(void) {
    const struct aurora_identity_throttle_policy policy = {2u, 1000u, 8000u};

    CHECK(aurora_identity_compute_throttle_delay_ms(&policy, 1u) == 0u, "first failure delay");
    CHECK(aurora_identity_compute_throttle_delay_ms(&policy, 2u) == 0u, "second failure delay");
    CHECK(aurora_identity_compute_throttle_delay_ms(&policy, 3u) == 1000u, "third failure delay");
    CHECK(aurora_identity_compute_throttle_delay_ms(&policy, 4u) == 2000u, "fourth failure delay");
    CHECK(aurora_identity_compute_throttle_delay_ms(&policy, 20u) == 8000u, "delay must cap");
}

static void test_success(void) {
    struct fake_crypto_context crypto = {"AUR7K4PN9Q2XM6D", 15u, false, 0u};
    struct fake_store_context store;
    struct fake_clock_context clock = {10000u, false};
    struct aurora_identity_core core;
    struct aurora_identity_auth_result result;

    prepare_record(&crypto, &store);
    store.record.failed_attempts = 1u;
    core = make_core(&crypto, &store, &clock);

    result = aurora_identity_authenticate_key(
        &core, "aur-7k4p-n9q2-xm6d", strlen("aur-7k4p-n9q2-xm6d"));

    CHECK(result.result == AURORA_IDENTITY_OK, "valid key must authenticate");
    CHECK(!aurora_identity_user_id_is_zero(&result.user_id), "success must return user id");
    CHECK(!aurora_identity_credential_id_is_zero(&result.credential_id),
        "success must return credential id");
    CHECK(crypto.verify_calls == 1u, "verifier must run once");
    CHECK(store.clear_writes == 1u, "success must clear prior failure state");
}

static void test_unknown_key(void) {
    struct fake_crypto_context crypto = {"AUR7K4PN9Q2XM6D", 15u, false, 0u};
    struct fake_store_context store;
    struct fake_clock_context clock = {10000u, false};
    struct aurora_identity_core core;
    struct aurora_identity_auth_result result;

    prepare_record(&crypto, &store);
    core = make_core(&crypto, &store, &clock);

    result = aurora_identity_authenticate_key(
        &core, "ZZZZ-1111-YYYY-2222", strlen("ZZZZ-1111-YYYY-2222"));

    CHECK(result.result == AURORA_IDENTITY_NOT_FOUND, "unknown lookup tag must not resolve");
    CHECK(crypto.verify_calls == 0u, "unknown key must not run record verifier");
}

static void test_failed_verification_updates_throttle(void) {
    struct fake_crypto_context crypto = {"AUR7K4PN9Q2XM6D", 15u, true, 0u};
    struct fake_store_context store;
    struct fake_clock_context clock = {5000u, false};
    struct aurora_identity_core core;
    struct aurora_identity_auth_result result;

    prepare_record(&crypto, &store);
    store.record.failed_attempts = 2u;
    core = make_core(&crypto, &store, &clock);

    result = aurora_identity_authenticate_key(
        &core, "ZZZZ-1111-YYYY-2222", strlen("ZZZZ-1111-YYYY-2222"));

    CHECK(result.result == AURORA_IDENTITY_AUTH_FAILED, "verifier mismatch must fail");
    CHECK(store.failure_writes == 1u, "failure state must persist");
    CHECK(store.last_failed_attempts == 3u, "failure count must increment");
    CHECK(result.retry_after_ms == 1000u, "third failure must report delay");
    CHECK(store.last_throttle_until_ms == 6000u, "throttle deadline mismatch");
}

static void test_existing_throttle_short_circuits_verifier(void) {
    struct fake_crypto_context crypto = {"AUR7K4PN9Q2XM6D", 15u, false, 0u};
    struct fake_store_context store;
    struct fake_clock_context clock = {10000u, false};
    struct aurora_identity_core core;
    struct aurora_identity_auth_result result;

    prepare_record(&crypto, &store);
    store.record.throttle_until_ms = 12500u;
    core = make_core(&crypto, &store, &clock);

    result = aurora_identity_authenticate_key(
        &core, crypto.expected_key, crypto.expected_key_length);

    CHECK(result.result == AURORA_IDENTITY_THROTTLED, "active throttle must be enforced");
    CHECK(result.retry_after_ms == 2500u, "retry-after mismatch");
    CHECK(crypto.verify_calls == 0u, "throttled credential must not run expensive verifier");
}

static void test_disabled_identity_fails_closed(void) {
    struct fake_crypto_context crypto = {"AUR7K4PN9Q2XM6D", 15u, false, 0u};
    struct fake_store_context store;
    struct fake_clock_context clock = {10000u, false};
    struct aurora_identity_core core;
    struct aurora_identity_auth_result result;

    prepare_record(&crypto, &store);
    store.record.status = AURORA_IDENTITY_RECORD_DISABLED;
    core = make_core(&crypto, &store, &clock);

    result = aurora_identity_authenticate_key(
        &core, crypto.expected_key, crypto.expected_key_length);

    CHECK(result.result == AURORA_IDENTITY_DISABLED, "disabled identity must not authenticate");
    CHECK(crypto.verify_calls == 0u, "disabled identity should fail before KDF");
}

int main(void) {
    test_normalization();
    test_throttle_policy();
    test_success();
    test_unknown_key();
    test_failed_verification_updates_throttle();
    test_existing_throttle_short_circuits_verifier();
    test_disabled_identity_fails_closed();

    puts("Aurora Identity core tests: PASS");
    return 0;
}
