#include "aurora/identity/core.h"
#include "aurora/identity/reauth_proof.h"
#include "aurora/identity/reauth_proof_memory.h"
#include "aurora/identity/rotation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c,m) do { if (!(c)) { fprintf(stderr,"FAIL: %s\n",m); exit(1); } } while (0)

struct fixture {
    struct aurora_identity_key_record record;
    bool has_record;
    uint8_t random_seed;
    uint64_t now_ms;
};

static void fill_id(uint8_t *out, size_t size, uint8_t seed) {
    for (size_t i = 0; i < size; ++i) out[i] = (uint8_t)(seed + i + 1u);
}

static bool fill_random(void *opaque, uint8_t *out, size_t size) {
    struct fixture *f = opaque;
    for (size_t i = 0; i < size; ++i) {
        out[i] = ++f->random_seed;
        if (out[i] == 0u) out[i] = ++f->random_seed;
    }
    return true;
}

static bool clock_now(void *opaque, uint64_t *out) {
    *out = ((struct fixture *)opaque)->now_ms;
    return true;
}

static void tag_for(const uint8_t *data, size_t size, uint8_t out[32]) {
    uint64_t h = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < size; ++i) { h ^= data[i]; h *= UINT64_C(1099511628211); }
    for (size_t i = 0; i < 32u; ++i) out[i] = (uint8_t)(h >> ((i % 8u) * 8u));
}

static bool derive_proof_tag(void *opaque, const uint8_t token[32], uint8_t out[32]) {
    (void)opaque; tag_for(token, 32u, out); return true;
}

static bool derive_lookup(void *opaque, const char *key, size_t len, uint8_t out[32]) {
    (void)opaque; tag_for((const uint8_t *)key, len, out); return true;
}

static bool derive_verifier(
    void *opaque, const char *key, size_t len,
    const struct aurora_identity_kdf_params *kdf,
    const uint8_t *salt, size_t salt_size,
    uint8_t *out, size_t out_size) {
    (void)opaque; (void)kdf;
    uint8_t material[96];
    CHECK(len + salt_size <= sizeof(material), "verifier material");
    memcpy(material, key, len); memcpy(material + len, salt, salt_size);
    uint8_t tag[32]; tag_for(material, len + salt_size, tag);
    for (size_t i = 0; i < out_size; ++i) out[i] = tag[i % sizeof(tag)];
    memset(material, 0, sizeof(material)); memset(tag, 0, sizeof(tag));
    return true;
}

static enum aurora_identity_store_result find_record(
    void *opaque, const uint8_t lookup[32], struct aurora_identity_key_record *out) {
    struct fixture *f = opaque;
    if (!f->has_record || memcmp(f->record.lookup_tag, lookup, 32u) != 0)
        return AURORA_IDENTITY_STORE_NOT_FOUND;
    *out = f->record; return AURORA_IDENTITY_STORE_OK;
}

static bool noop_failure(void *o, const struct aurora_identity_user_id *u, uint32_t n, uint64_t t) {
    (void)o; (void)u; (void)n; (void)t; return true;
}
static bool noop_clear(void *o, const struct aurora_identity_user_id *u) {
    (void)o; (void)u; return true;
}

static enum aurora_identity_rotation_store_result replace_record(
    void *opaque,
    const struct aurora_identity_user_id *user,
    const struct aurora_identity_credential_id *credential,
    const struct aurora_identity_key_record *replacement) {
    struct fixture *f = opaque;
    if (!f->has_record ||
        memcmp(f->record.user_id.bytes, user->bytes, AURORA_IDENTITY_USER_ID_SIZE) != 0 ||
        memcmp(f->record.credential_id.bytes, credential->bytes,
               AURORA_IDENTITY_CREDENTIAL_ID_SIZE) != 0)
        return AURORA_IDENTITY_ROTATION_STORE_CURRENT_NOT_FOUND;
    f->record = *replacement;
    return AURORA_IDENTITY_ROTATION_STORE_OK;
}

int main(void) {
    struct fixture f;
    memset(&f, 0, sizeof(f));
    f.random_seed = 17u;
    f.now_ms = 1000u;
    f.has_record = true;
    fill_id(f.record.user_id.bytes, sizeof(f.record.user_id.bytes), 0x10u);
    fill_id(f.record.credential_id.bytes, sizeof(f.record.credential_id.bytes), 0x40u);
    f.record.status = AURORA_IDENTITY_RECORD_ACTIVE;
    f.record.kdf.algorithm = AURORA_IDENTITY_KDF_ARGON2ID;
    f.record.kdf.parameters_version = 1u;
    f.record.kdf.memory_kib = 8u;
    f.record.kdf.time_cost = 1u;
    f.record.kdf.parallelism = 1u;
    f.record.salt_size = 16u;
    f.record.verifier_size = 32u;
    memset(f.record.salt, 0x5a, f.record.salt_size);
    static const char old_key[] = "OLDKEY123456789";
    static const char new_key[] = "NEWKEY123456789";
    derive_lookup(&f, old_key, sizeof(old_key)-1u, f.record.lookup_tag);
    derive_verifier(&f, old_key, sizeof(old_key)-1u, &f.record.kdf,
        f.record.salt, f.record.salt_size, f.record.verifier, f.record.verifier_size);

    struct aurora_identity_reauth_memory_store proofs;
    aurora_identity_reauth_memory_init(&proofs);
    struct aurora_identity_reauth_core reauth;
    memset(&reauth, 0, sizeof(reauth));
    reauth.random.context = &f; reauth.random.fill_random = fill_random;
    reauth.clock.context = &f; reauth.clock.monotonic_ms = clock_now;
    reauth.crypto.context = &f; reauth.crypto.derive_token_tag = derive_proof_tag;
    reauth.store = aurora_identity_reauth_memory_ops(&proofs);
    reauth.policy.ttl_ms = 60000u;

    struct aurora_identity_reauth_issue_result issued = aurora_identity_reauth_issue(
        &reauth, &f.record.user_id, &f.record.credential_id, UINT64_C(7),
        AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY);
    CHECK(issued.result == AURORA_IDENTITY_REAUTH_OK, "issue rotation proof");

    struct aurora_identity_reauth_consume_result consumed = aurora_identity_reauth_consume(
        &reauth, &issued.token, &f.record.user_id, UINT64_C(7),
        AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY);
    CHECK(consumed.result == AURORA_IDENTITY_REAUTH_OK, "consume rotation proof");

    struct aurora_identity_core core;
    memset(&core, 0, sizeof(core));
    core.crypto.context = &f;
    core.crypto.derive_lookup_tag = derive_lookup;
    core.crypto.derive_key_verifier = derive_verifier;
    core.random.context = &f; core.random.fill_random = fill_random;
    core.store.context = &f; core.store.find_key_record_by_lookup_tag = find_record;
    core.store.store_failure_state = noop_failure; core.store.clear_failure_state = noop_clear;
    core.creation_policy.kdf = f.record.kdf;
    core.creation_policy.salt_size = 16u;
    core.creation_policy.verifier_size = 32u;
    core.creation_policy.identity_record_version = 1u;
    core.creation_policy.policy_version = 1u;

    struct aurora_identity_rotation_context rotation;
    memset(&rotation, 0, sizeof(rotation));
    rotation.core = &core;
    rotation.store.context = &f;
    rotation.store.replace_key_credential = replace_record;

    struct aurora_identity_rotation_result rotated = aurora_identity_rotate_key(
        &rotation, &consumed.user_id, &consumed.credential_id,
        new_key, sizeof(new_key)-1u);
    CHECK(rotated.result == AURORA_IDENTITY_OK, "proof-authorized rotation");
    CHECK(memcmp(rotated.new_credential_id.bytes, consumed.credential_id.bytes,
        AURORA_IDENTITY_CREDENTIAL_ID_SIZE) != 0, "credential id replaced");

    consumed = aurora_identity_reauth_consume(
        &reauth, &issued.token, &f.record.user_id, UINT64_C(7),
        AURORA_IDENTITY_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY);
    CHECK(consumed.result == AURORA_IDENTITY_REAUTH_NOT_FOUND, "proof replay rejected");

    uint8_t old_lookup[32], new_lookup[32];
    derive_lookup(&f, old_key, sizeof(old_key)-1u, old_lookup);
    derive_lookup(&f, new_key, sizeof(new_key)-1u, new_lookup);
    CHECK(memcmp(f.record.lookup_tag, old_lookup, 32u) != 0, "old key retired");
    CHECK(memcmp(f.record.lookup_tag, new_lookup, 32u) == 0, "new key published");

    puts("Aurora Identity reauth-to-rotation integration tests: PASS");
    return 0;
}
