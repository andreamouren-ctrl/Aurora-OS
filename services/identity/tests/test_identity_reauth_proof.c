#include "aurora/identity/reauth_proof.h"
#include "aurora/identity/reauth_proof_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", message); \
            exit(1); \
        } \
    } while (0)

struct fake_random {
    uint8_t seed;
    bool fail;
};

struct fake_clock {
    uint64_t now_ms;
    bool fail;
};

struct fake_crypto {
    bool fail;
};

static bool random_fill(void *opaque, uint8_t *buffer, size_t size) {
    struct fake_random *random = (struct fake_random *)opaque;
    if (random->fail) return false;
    for (size_t i = 0u; i < size; ++i) {
        buffer[i] = (uint8_t)(random->seed + (uint8_t)i + 1u);
    }
    ++random->seed;
    return true;
}

static bool clock_now(void *opaque, uint64_t *out_now_ms) {
    struct fake_clock *clock = (struct fake_clock *)opaque;
    if (clock->fail) return false;
    *out_now_ms = clock->now_ms;
    return true;
}

static bool derive_tag(
    void *opaque,
    const uint8_t token[AURORA_IDENTITY_REAUTH_PROOF_TOKEN_SIZE],
    uint8_t out_tag[AURORA_IDENTITY_REAUTH_PROOF_TAG_SIZE]
) {
    struct fake_crypto *crypto = (struct fake_crypto *)opaque;
    uint64_t hash = UINT64_C(1469598103934665603);
    if (crypto->fail) return false;

    for (size_t i = 0u; i < AURORA_IDENTITY_REAUTH_PROOF_TOKEN_SIZE; ++i) {
        hash ^= token[i];
        hash *= UINT64_C(1099511628211);
    }
    for (size_t i = 0u; i < AURORA_IDENTITY_REAUTH_PROOF_TAG_SIZE; ++i) {
        out_tag[i] = (uint8_t)(hash >> ((i % 8u) * 8u));
    }
    return true;
}

static void fill_user(
    struct aurora_identity_user_id *user_id,
    uint8_t base
) {
    memset(user_id, 0, sizeof(*user_id));
    for (size_t i = 0u; i < sizeof(user_id->bytes); ++i) {
        user_id->bytes[i] = (uint8_t)(base + (uint8_t)i + 1u);
    }
}

static struct aurora_identity_reauth_core make_core(
    struct fake_random *random,
    struct fake_clock *clock,
    struct fake_crypto *crypto,
    struct aurora_identity_reauth_memory_store *store
) {
    struct aurora_identity_reauth_core core;
    memset(&core, 0, sizeof(core));
    core.random.context = random;
    core.random.fill_random = random_fill;
    core.clock.context = clock;
    core.clock.monotonic_ms = clock_now;
    core.crypto.context = crypto;
    core.crypto.derive_token_tag = derive_tag;
    core.store = aurora_identity_reauth_memory_ops(store);
    core.policy.ttl_ms = UINT64_C(30000);
    return core;
}

static void test_issue_consume_replay(void) {
    struct fake_random random = {1u, false};
    struct fake_clock clock = {1000u, false};
    struct fake_crypto crypto = {false};
    struct aurora_identity_reauth_memory_store store;
    struct aurora_identity_user_id user;
    aurora_identity_reauth_memory_init(&store);
    fill_user(&user, 0x10u);

    struct aurora_identity_reauth_core core =
        make_core(&random, &clock, &crypto, &store);
    struct aurora_identity_reauth_issue_result issued =
        aurora_identity_reauth_issue(
            &core,
            &user,
            UINT64_C(41),
            AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY);

    CHECK(issued.result == AURORA_IDENTITY_REAUTH_OK, "proof issue");
    CHECK(!aurora_identity_reauth_token_is_zero(&issued.token), "nonzero proof token");
    CHECK(issued.expires_at_ms == UINT64_C(31000), "proof expiry");
    CHECK(aurora_identity_reauth_memory_count(&store) == 1u, "proof stored");

    struct aurora_identity_reauth_consume_result consumed =
        aurora_identity_reauth_consume(
            &core,
            &issued.token,
            &user,
            UINT64_C(41),
            AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY);
    CHECK(consumed.result == AURORA_IDENTITY_REAUTH_OK, "proof consume");
    CHECK(consumed.session_generation == UINT64_C(41), "session generation returned");
    CHECK(consumed.purpose ==
        AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY, "purpose returned");
    CHECK(memcmp(consumed.user_id.bytes, user.bytes, sizeof(user.bytes)) == 0,
        "user binding returned");
    CHECK(aurora_identity_reauth_memory_count(&store) == 0u, "proof consumed");

    consumed = aurora_identity_reauth_consume(
        &core,
        &issued.token,
        &user,
        UINT64_C(41),
        AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY);
    CHECK(consumed.result == AURORA_IDENTITY_REAUTH_NOT_FOUND, "replay rejected");
}

static void test_wrong_purpose_burns_proof(void) {
    struct fake_random random = {7u, false};
    struct fake_clock clock = {2000u, false};
    struct fake_crypto crypto = {false};
    struct aurora_identity_reauth_memory_store store;
    struct aurora_identity_user_id user;
    aurora_identity_reauth_memory_init(&store);
    fill_user(&user, 0x20u);

    struct aurora_identity_reauth_core core =
        make_core(&random, &clock, &crypto, &store);
    struct aurora_identity_reauth_issue_result issued =
        aurora_identity_reauth_issue(
            &core,
            &user,
            UINT64_C(41),
            AURORA_IDENTITY_REAUTH_PURPOSE_ENROLL_AUTHENTICATOR);
    CHECK(issued.result == AURORA_IDENTITY_REAUTH_OK, "wrong-purpose setup");

    struct aurora_identity_reauth_consume_result consumed =
        aurora_identity_reauth_consume(
            &core,
            &issued.token,
            &user,
            UINT64_C(41),
            AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY);
    CHECK(consumed.result == AURORA_IDENTITY_REAUTH_PURPOSE_MISMATCH,
        "wrong purpose rejected");

    consumed = aurora_identity_reauth_consume(
        &core,
        &issued.token,
        &user,
        UINT64_C(41),
        AURORA_IDENTITY_REAUTH_PURPOSE_ENROLL_AUTHENTICATOR);
    CHECK(consumed.result == AURORA_IDENTITY_REAUTH_NOT_FOUND,
        "wrong-purpose attempt burns proof");
}

static void test_wrong_user_burns_proof(void) {
    struct fake_random random = {11u, false};
    struct fake_clock clock = {3000u, false};
    struct fake_crypto crypto = {false};
    struct aurora_identity_reauth_memory_store store;
    struct aurora_identity_user_id user_a;
    struct aurora_identity_user_id user_b;
    aurora_identity_reauth_memory_init(&store);
    fill_user(&user_a, 0x30u);
    fill_user(&user_b, 0x50u);

    struct aurora_identity_reauth_core core =
        make_core(&random, &clock, &crypto, &store);
    struct aurora_identity_reauth_issue_result issued =
        aurora_identity_reauth_issue(
            &core,
            &user_a,
            UINT64_C(51),
            AURORA_IDENTITY_REAUTH_PURPOSE_CHANGE_RECOVERY_POLICY);
    CHECK(issued.result == AURORA_IDENTITY_REAUTH_OK, "wrong-user setup");

    struct aurora_identity_reauth_consume_result consumed =
        aurora_identity_reauth_consume(
            &core,
            &issued.token,
            &user_b,
            UINT64_C(51),
            AURORA_IDENTITY_REAUTH_PURPOSE_CHANGE_RECOVERY_POLICY);
    CHECK(consumed.result == AURORA_IDENTITY_REAUTH_USER_MISMATCH,
        "wrong user rejected");

    consumed = aurora_identity_reauth_consume(
        &core,
        &issued.token,
        &user_a,
        UINT64_C(51),
        AURORA_IDENTITY_REAUTH_PURPOSE_CHANGE_RECOVERY_POLICY);
    CHECK(consumed.result == AURORA_IDENTITY_REAUTH_NOT_FOUND,
        "wrong-user attempt burns proof");
}

static void test_expiry_and_policy(void) {
    struct fake_random random = {17u, false};
    struct fake_clock clock = {4000u, false};
    struct fake_crypto crypto = {false};
    struct aurora_identity_reauth_memory_store store;
    struct aurora_identity_user_id user;
    aurora_identity_reauth_memory_init(&store);
    fill_user(&user, 0x60u);

    struct aurora_identity_reauth_core core =
        make_core(&random, &clock, &crypto, &store);
    core.policy.ttl_ms = 1000u;

    struct aurora_identity_reauth_issue_result issued =
        aurora_identity_reauth_issue(
            &core,
            &user,
            UINT64_C(41),
            AURORA_IDENTITY_REAUTH_PURPOSE_EXPORT_RECOVERY_MATERIAL);
    CHECK(issued.result == AURORA_IDENTITY_REAUTH_OK, "expiry setup");

    clock.now_ms = 5000u;
    struct aurora_identity_reauth_consume_result consumed =
        aurora_identity_reauth_consume(
            &core,
            &issued.token,
            &user,
            UINT64_C(41),
            AURORA_IDENTITY_REAUTH_PURPOSE_EXPORT_RECOVERY_MATERIAL);
    CHECK(consumed.result == AURORA_IDENTITY_REAUTH_EXPIRED, "expired proof rejected");

    issued = aurora_identity_reauth_issue(
        &core, &user, 0u, AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY);
    CHECK(issued.result == AURORA_IDENTITY_REAUTH_INVALID_ARGUMENT,
        "zero session generation rejected");

    issued = aurora_identity_reauth_issue(
        &core, &user, UINT64_C(41), AURORA_IDENTITY_REAUTH_PURPOSE_NONE);
    CHECK(issued.result == AURORA_IDENTITY_REAUTH_INVALID_PURPOSE,
        "invalid purpose rejected");

    core.policy.ttl_ms = AURORA_IDENTITY_REAUTH_PROOF_MAX_TTL_MS + 1u;
    issued = aurora_identity_reauth_issue(
        &core, &user, UINT64_C(41), AURORA_IDENTITY_REAUTH_PURPOSE_CHANGE_LOCAL_ROLE);
    CHECK(issued.result == AURORA_IDENTITY_REAUTH_POLICY_ERROR,
        "oversized ttl rejected");
}


static void test_wrong_session_burns_proof(void) {
    struct fake_random random = {29u, false};
    struct fake_clock clock = {7000u, false};
    struct fake_crypto crypto = {false};
    struct aurora_identity_reauth_memory_store store;
    struct aurora_identity_user_id user;
    aurora_identity_reauth_memory_init(&store);
    fill_user(&user, 0x80u);

    struct aurora_identity_reauth_core core =
        make_core(&random, &clock, &crypto, &store);
    struct aurora_identity_reauth_issue_result issued =
        aurora_identity_reauth_issue(
            &core,
            &user,
            UINT64_C(71),
            AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY);
    CHECK(issued.result == AURORA_IDENTITY_REAUTH_OK, "wrong-session setup");

    struct aurora_identity_reauth_consume_result consumed =
        aurora_identity_reauth_consume(
            &core,
            &issued.token,
            &user,
            UINT64_C(72),
            AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY);
    CHECK(consumed.result == AURORA_IDENTITY_REAUTH_SESSION_MISMATCH,
        "wrong session rejected");

    consumed = aurora_identity_reauth_consume(
        &core,
        &issued.token,
        &user,
        UINT64_C(71),
        AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY);
    CHECK(consumed.result == AURORA_IDENTITY_REAUTH_NOT_FOUND,
        "wrong-session attempt burns proof");
}

static void test_store_clear(void) {
    struct fake_random random = {23u, false};
    struct fake_clock clock = {6000u, false};
    struct fake_crypto crypto = {false};
    struct aurora_identity_reauth_memory_store store;
    struct aurora_identity_user_id user;
    aurora_identity_reauth_memory_init(&store);
    fill_user(&user, 0x70u);

    struct aurora_identity_reauth_core core =
        make_core(&random, &clock, &crypto, &store);
    struct aurora_identity_reauth_issue_result issued =
        aurora_identity_reauth_issue(
            &core,
            &user,
            UINT64_C(41),
            AURORA_IDENTITY_REAUTH_PURPOSE_GRANT_RESOURCE_ACCESS);
    CHECK(issued.result == AURORA_IDENTITY_REAUTH_OK, "clear setup");
    CHECK(aurora_identity_reauth_memory_count(&store) == 1u, "clear count setup");

    aurora_identity_reauth_memory_clear(&store);
    CHECK(aurora_identity_reauth_memory_count(&store) == 0u, "store clear");

    struct aurora_identity_reauth_consume_result consumed =
        aurora_identity_reauth_consume(
            &core,
            &issued.token,
            &user,
            UINT64_C(41),
            AURORA_IDENTITY_REAUTH_PURPOSE_GRANT_RESOURCE_ACCESS);
    CHECK(consumed.result == AURORA_IDENTITY_REAUTH_NOT_FOUND,
        "cleared proof unavailable");
}

int main(void) {
    test_issue_consume_replay();
    test_wrong_purpose_burns_proof();
    test_wrong_user_burns_proof();
    test_wrong_session_burns_proof();
    test_expiry_and_policy();
    test_store_clear();

    puts("Aurora Identity purpose-bound re-auth proof tests: PASS");
    return 0;
}
