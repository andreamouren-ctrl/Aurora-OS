#include "aurora/identity/session_grant.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORE_CAPACITY 8u

struct fake_random_context {
    uint8_t seed;
    bool fail;
    bool zero_once;
    unsigned int calls;
};

struct fake_clock_context {
    uint64_t now_ms;
    bool fail;
};

struct fake_crypto_context {
    bool fail;
};

struct fake_store_slot {
    bool active;
    struct aurora_identity_session_grant_record record;
};

struct fake_store_context {
    struct fake_store_slot slots[STORE_CAPACITY];
    bool fail_issue;
    bool fail_consume;
    unsigned int forced_conflicts;
    unsigned int issue_calls;
    unsigned int consume_calls;
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

static bool fake_random_fill(void *opaque, uint8_t *buffer, size_t size) {
    struct fake_random_context *context = (struct fake_random_context *)opaque;
    size_t index;

    ++context->calls;
    if (context->fail) {
        return false;
    }
    if (context->zero_once) {
        context->zero_once = false;
        memset(buffer, 0, size);
        return true;
    }

    for (index = 0u; index < size; ++index) {
        buffer[index] = (uint8_t)(context->seed + (uint8_t)index + 1u);
    }
    ++context->seed;
    return true;
}

static bool fake_clock_now(void *opaque, uint64_t *out_now_ms) {
    struct fake_clock_context *context = (struct fake_clock_context *)opaque;
    if (context->fail) {
        return false;
    }
    *out_now_ms = context->now_ms;
    return true;
}

static bool fake_derive_token_tag(
    void *opaque,
    const uint8_t token[AURORA_IDENTITY_SESSION_GRANT_TOKEN_SIZE],
    uint8_t out_tag[AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE]) {
    struct fake_crypto_context *context = (struct fake_crypto_context *)opaque;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    if (context->fail) {
        return false;
    }

    for (index = 0u; index < AURORA_IDENTITY_SESSION_GRANT_TOKEN_SIZE; ++index) {
        hash ^= token[index];
        hash *= UINT64_C(1099511628211);
    }

    for (index = 0u; index < AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE; ++index) {
        out_tag[index] = (uint8_t)(hash >> ((index % 8u) * 8u));
    }
    return true;
}

static enum aurora_identity_session_grant_store_issue_result fake_issue_grant(
    void *opaque,
    const struct aurora_identity_session_grant_record *record) {
    struct fake_store_context *context = (struct fake_store_context *)opaque;
    size_t index;

    ++context->issue_calls;
    if (context->fail_issue) {
        return AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_ERROR;
    }
    if (context->forced_conflicts > 0u) {
        --context->forced_conflicts;
        return AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_CONFLICT;
    }

    for (index = 0u; index < STORE_CAPACITY; ++index) {
        if (context->slots[index].active &&
            memcmp(context->slots[index].record.token_tag,
                   record->token_tag,
                   AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE) == 0) {
            return AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_CONFLICT;
        }
    }

    for (index = 0u; index < STORE_CAPACITY; ++index) {
        if (!context->slots[index].active) {
            context->slots[index].active = true;
            context->slots[index].record = *record;
            return AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_OK;
        }
    }

    return AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_ERROR;
}

static enum aurora_identity_session_grant_store_consume_result fake_consume_grant(
    void *opaque,
    const uint8_t token_tag[AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE],
    uint64_t now_ms,
    struct aurora_identity_session_grant_record *out_record) {
    struct fake_store_context *context = (struct fake_store_context *)opaque;
    size_t index;

    ++context->consume_calls;
    if (context->fail_consume) {
        return AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_ERROR;
    }

    for (index = 0u; index < STORE_CAPACITY; ++index) {
        if (!context->slots[index].active ||
            memcmp(context->slots[index].record.token_tag,
                   token_tag,
                   AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE) != 0) {
            continue;
        }

        *out_record = context->slots[index].record;
        context->slots[index].active = false;
        aurora_identity_secure_zero(
            &context->slots[index].record,
            sizeof(context->slots[index].record));

        if (out_record->expires_at_ms <= now_ms) {
            return AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_EXPIRED;
        }
        return AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_OK;
    }

    return AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_NOT_FOUND;
}

static void fill_user_id(struct aurora_identity_user_id *user_id, uint8_t base) {
    size_t index;
    for (index = 0u; index < AURORA_IDENTITY_USER_ID_SIZE; ++index) {
        user_id->bytes[index] = (uint8_t)(base + index + 1u);
    }
}

static struct aurora_identity_session_grant_core make_core(
    struct fake_random_context *random,
    struct fake_clock_context *clock,
    struct fake_crypto_context *crypto,
    struct fake_store_context *store) {
    struct aurora_identity_session_grant_core core;
    memset(&core, 0, sizeof(core));

    core.random.context = random;
    core.random.fill_random = fake_random_fill;
    core.clock.context = clock;
    core.clock.monotonic_ms = fake_clock_now;
    core.crypto.context = crypto;
    core.crypto.derive_token_tag = fake_derive_token_tag;
    core.store.context = store;
    core.store.issue_grant = fake_issue_grant;
    core.store.consume_grant = fake_consume_grant;
    core.policy.ttl_ms = 30000u;
    return core;
}

static void test_issue_consume_and_replay(void) {
    struct fake_random_context random = {1u, false, false, 0u};
    struct fake_clock_context clock = {1000u, false};
    struct fake_crypto_context crypto = {false};
    struct fake_store_context store;
    struct aurora_identity_session_grant_core core;
    struct aurora_identity_user_id user_id;
    struct aurora_identity_session_grant_issue_result issued;
    struct aurora_identity_session_grant_consume_result consumed;
    struct aurora_identity_session_grant_consume_result replayed;

    memset(&store, 0, sizeof(store));
    fill_user_id(&user_id, 0x10u);
    core = make_core(&random, &clock, &crypto, &store);

    issued = aurora_identity_session_grant_issue(&core, &user_id);
    CHECK(issued.result == AURORA_IDENTITY_SESSION_GRANT_OK, "grant issue should succeed");
    CHECK(!aurora_identity_session_grant_token_is_zero(&issued.token), "issued token must be nonzero");
    CHECK(issued.expires_at_ms == 31000u, "grant expiry mismatch");

    consumed = aurora_identity_session_grant_consume(&core, &issued.token);
    CHECK(consumed.result == AURORA_IDENTITY_SESSION_GRANT_OK, "first consume should succeed");
    CHECK(memcmp(consumed.user_id.bytes, user_id.bytes, AURORA_IDENTITY_USER_ID_SIZE) == 0,
          "consumed grant must return bound user id");

    replayed = aurora_identity_session_grant_consume(&core, &issued.token);
    CHECK(replayed.result == AURORA_IDENTITY_SESSION_GRANT_NOT_FOUND,
          "replayed grant must fail");
    CHECK(store.consume_calls == 2u, "consume count mismatch");
}

static void test_expired_grant_is_invalidated(void) {
    struct fake_random_context random = {2u, false, false, 0u};
    struct fake_clock_context clock = {5000u, false};
    struct fake_crypto_context crypto = {false};
    struct fake_store_context store;
    struct aurora_identity_session_grant_core core;
    struct aurora_identity_user_id user_id;
    struct aurora_identity_session_grant_issue_result issued;
    struct aurora_identity_session_grant_consume_result consumed;
    struct aurora_identity_session_grant_consume_result replayed;

    memset(&store, 0, sizeof(store));
    fill_user_id(&user_id, 0x20u);
    core = make_core(&random, &clock, &crypto, &store);
    core.policy.ttl_ms = 1000u;

    issued = aurora_identity_session_grant_issue(&core, &user_id);
    CHECK(issued.result == AURORA_IDENTITY_SESSION_GRANT_OK, "expiry test issue");

    clock.now_ms = 6000u;
    consumed = aurora_identity_session_grant_consume(&core, &issued.token);
    CHECK(consumed.result == AURORA_IDENTITY_SESSION_GRANT_EXPIRED,
          "expired grant must be rejected");

    replayed = aurora_identity_session_grant_consume(&core, &issued.token);
    CHECK(replayed.result == AURORA_IDENTITY_SESSION_GRANT_NOT_FOUND,
          "expired grant must be invalidated after check");
}

static void test_zero_random_retry_and_tag_conflict_retry(void) {
    struct fake_random_context random = {3u, false, true, 0u};
    struct fake_clock_context clock = {7000u, false};
    struct fake_crypto_context crypto = {false};
    struct fake_store_context store;
    struct aurora_identity_session_grant_core core;
    struct aurora_identity_user_id user_id;
    struct aurora_identity_session_grant_issue_result issued;

    memset(&store, 0, sizeof(store));
    store.forced_conflicts = 1u;
    fill_user_id(&user_id, 0x30u);
    core = make_core(&random, &clock, &crypto, &store);

    issued = aurora_identity_session_grant_issue(&core, &user_id);
    CHECK(issued.result == AURORA_IDENTITY_SESSION_GRANT_OK,
          "grant should retry zero token and uniqueness conflict");
    CHECK(random.calls == 3u, "expected zero-token retry plus conflict retry");
    CHECK(store.issue_calls == 2u, "expected one conflict then successful issue");
}

static void test_invalid_user_and_policy_fail_closed(void) {
    struct fake_random_context random = {4u, false, false, 0u};
    struct fake_clock_context clock = {100u, false};
    struct fake_crypto_context crypto = {false};
    struct fake_store_context store;
    struct aurora_identity_session_grant_core core;
    struct aurora_identity_user_id zero_user;
    struct aurora_identity_user_id user_id;
    struct aurora_identity_session_grant_issue_result result;

    memset(&store, 0, sizeof(store));
    memset(&zero_user, 0, sizeof(zero_user));
    fill_user_id(&user_id, 0x40u);
    core = make_core(&random, &clock, &crypto, &store);

    result = aurora_identity_session_grant_issue(&core, &zero_user);
    CHECK(result.result == AURORA_IDENTITY_SESSION_GRANT_INVALID_USER,
          "zero user id must not receive grant");
    CHECK(store.issue_calls == 0u, "invalid user must not reach store");

    core.policy.ttl_ms = AURORA_IDENTITY_SESSION_GRANT_MAX_TTL_MS + 1u;
    result = aurora_identity_session_grant_issue(&core, &user_id);
    CHECK(result.result == AURORA_IDENTITY_SESSION_GRANT_POLICY_ERROR,
          "oversized ttl must be rejected");
}

static void test_issue_failure_paths(void) {
    struct fake_random_context random = {5u, false, false, 0u};
    struct fake_clock_context clock = {200u, false};
    struct fake_crypto_context crypto = {false};
    struct fake_store_context store;
    struct aurora_identity_session_grant_core core;
    struct aurora_identity_user_id user_id;
    struct aurora_identity_session_grant_issue_result result;

    memset(&store, 0, sizeof(store));
    fill_user_id(&user_id, 0x50u);
    core = make_core(&random, &clock, &crypto, &store);

    random.fail = true;
    result = aurora_identity_session_grant_issue(&core, &user_id);
    CHECK(result.result == AURORA_IDENTITY_SESSION_GRANT_RANDOM_ERROR, "rng failure must fail closed");
    CHECK(aurora_identity_session_grant_token_is_zero(&result.token), "failed issue must expose no token");

    random.fail = false;
    crypto.fail = true;
    result = aurora_identity_session_grant_issue(&core, &user_id);
    CHECK(result.result == AURORA_IDENTITY_SESSION_GRANT_CRYPTO_ERROR, "tag crypto failure must fail closed");

    crypto.fail = false;
    store.fail_issue = true;
    result = aurora_identity_session_grant_issue(&core, &user_id);
    CHECK(result.result == AURORA_IDENTITY_SESSION_GRANT_BACKEND_ERROR, "store issue failure must fail closed");
}

static void test_consume_failure_paths(void) {
    struct fake_random_context random = {6u, false, false, 0u};
    struct fake_clock_context clock = {1000u, false};
    struct fake_crypto_context crypto = {false};
    struct fake_store_context store;
    struct aurora_identity_session_grant_core core;
    struct aurora_identity_user_id user_id;
    struct aurora_identity_session_grant_token zero_token;
    struct aurora_identity_session_grant_issue_result issued;
    struct aurora_identity_session_grant_consume_result consumed;

    memset(&store, 0, sizeof(store));
    memset(&zero_token, 0, sizeof(zero_token));
    fill_user_id(&user_id, 0x60u);
    core = make_core(&random, &clock, &crypto, &store);

    consumed = aurora_identity_session_grant_consume(&core, &zero_token);
    CHECK(consumed.result == AURORA_IDENTITY_SESSION_GRANT_NOT_FOUND,
          "zero bearer token must not resolve");

    issued = aurora_identity_session_grant_issue(&core, &user_id);
    CHECK(issued.result == AURORA_IDENTITY_SESSION_GRANT_OK, "consume failure setup issue");

    crypto.fail = true;
    consumed = aurora_identity_session_grant_consume(&core, &issued.token);
    CHECK(consumed.result == AURORA_IDENTITY_SESSION_GRANT_CRYPTO_ERROR,
          "consume crypto failure must fail closed");

    crypto.fail = false;
    store.fail_consume = true;
    consumed = aurora_identity_session_grant_consume(&core, &issued.token);
    CHECK(consumed.result == AURORA_IDENTITY_SESSION_GRANT_BACKEND_ERROR,
          "consume store failure must fail closed");
}

int main(void) {
    test_issue_consume_and_replay();
    test_expired_grant_is_invalidated();
    test_zero_random_retry_and_tag_conflict_retry();
    test_invalid_user_and_policy_fail_closed();
    test_issue_failure_paths();
    test_consume_failure_paths();

    puts("Aurora Identity session grant tests: PASS");
    return 0;
}
