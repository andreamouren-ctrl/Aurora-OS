#include "aurora/identity/reauth_proof.h"

#include <limits.h>
#include <string.h>

static bool bytes_are_zero(const uint8_t *bytes, size_t size) {
    uint8_t aggregate = 0u;
    if (bytes == NULL) return true;
    for (size_t i = 0u; i < size; ++i) aggregate |= bytes[i];
    return aggregate == 0u;
}

static bool user_ids_equal(
    const struct aurora_identity_user_id *left,
    const struct aurora_identity_user_id *right
) {
    uint8_t difference = 0u;
    if (left == NULL || right == NULL) return false;
    for (size_t i = 0u; i < AURORA_IDENTITY_USER_ID_SIZE; ++i) {
        difference |= (uint8_t)(left->bytes[i] ^ right->bytes[i]);
    }
    return difference == 0u;
}

static uint64_t saturating_add_u64(uint64_t left, uint64_t right) {
    if (UINT64_MAX - left < right) return UINT64_MAX;
    return left + right;
}

static struct aurora_identity_reauth_issue_result issue_result(
    enum aurora_identity_reauth_result result
) {
    struct aurora_identity_reauth_issue_result value;
    memset(&value, 0, sizeof(value));
    value.result = result;
    return value;
}

static struct aurora_identity_reauth_consume_result consume_result(
    enum aurora_identity_reauth_result result
) {
    struct aurora_identity_reauth_consume_result value;
    memset(&value, 0, sizeof(value));
    value.result = result;
    return value;
}

bool aurora_identity_reauth_purpose_valid(uint32_t purpose) {
    return purpose > AURORA_IDENTITY_REAUTH_PURPOSE_NONE &&
        purpose < AURORA_IDENTITY_REAUTH_PURPOSE_COUNT;
}

bool aurora_identity_reauth_token_is_zero(
    const struct aurora_identity_reauth_token *token
) {
    return token == NULL ||
        bytes_are_zero(token->bytes, AURORA_IDENTITY_REAUTH_PROOF_TOKEN_SIZE);
}

struct aurora_identity_reauth_issue_result aurora_identity_reauth_issue(
    const struct aurora_identity_reauth_core *core,
    const struct aurora_identity_user_id *authenticated_user_id,
    const struct aurora_identity_credential_id *authenticated_credential_id,
    uint64_t session_generation,
    uint32_t purpose
) {
    struct aurora_identity_reauth_issue_result result =
        issue_result(AURORA_IDENTITY_REAUTH_INVALID_ARGUMENT);
    struct aurora_identity_reauth_record record;
    struct aurora_identity_reauth_token candidate;
    uint64_t now_ms = 0u;

    memset(&record, 0, sizeof(record));
    memset(&candidate, 0, sizeof(candidate));

    if (core == NULL || authenticated_user_id == NULL ||
        authenticated_credential_id == NULL ||
        core->random.fill_random == NULL ||
        core->clock.monotonic_ms == NULL ||
        core->crypto.derive_token_tag == NULL ||
        core->store.issue_proof == NULL) {
        goto cleanup;
    }

    if (aurora_identity_user_id_is_zero(authenticated_user_id)) {
        result.result = AURORA_IDENTITY_REAUTH_INVALID_USER;
        goto cleanup;
    }
    if (aurora_identity_credential_id_is_zero(authenticated_credential_id)) {
        result.result = AURORA_IDENTITY_REAUTH_INVALID_ARGUMENT;
        goto cleanup;
    }
    if (session_generation == 0u) {
        result.result = AURORA_IDENTITY_REAUTH_INVALID_ARGUMENT;
        goto cleanup;
    }
    if (!aurora_identity_reauth_purpose_valid(purpose)) {
        result.result = AURORA_IDENTITY_REAUTH_INVALID_PURPOSE;
        goto cleanup;
    }
    if (core->policy.ttl_ms == 0u ||
        core->policy.ttl_ms > AURORA_IDENTITY_REAUTH_PROOF_MAX_TTL_MS) {
        result.result = AURORA_IDENTITY_REAUTH_POLICY_ERROR;
        goto cleanup;
    }
    if (!core->clock.monotonic_ms(core->clock.context, &now_ms)) {
        result.result = AURORA_IDENTITY_REAUTH_BACKEND_ERROR;
        goto cleanup;
    }

    record.user_id = *authenticated_user_id;
    record.credential_id = *authenticated_credential_id;
    record.session_generation = session_generation;
    record.purpose = purpose;
    record.record_version = AURORA_IDENTITY_REAUTH_PROOF_RECORD_VERSION;
    record.issued_at_ms = now_ms;
    record.expires_at_ms = saturating_add_u64(now_ms, core->policy.ttl_ms);

    for (uint32_t attempt = 0u;
         attempt < AURORA_IDENTITY_REAUTH_PROOF_GENERATION_ATTEMPTS;
         ++attempt) {
        aurora_identity_secure_zero(&candidate, sizeof(candidate));
        aurora_identity_secure_zero(record.token_tag, sizeof(record.token_tag));

        if (!core->random.fill_random(
                core->random.context,
                candidate.bytes,
                sizeof(candidate.bytes))) {
            result.result = AURORA_IDENTITY_REAUTH_RANDOM_ERROR;
            goto cleanup;
        }
        if (aurora_identity_reauth_token_is_zero(&candidate)) continue;

        if (!core->crypto.derive_token_tag(
                core->crypto.context,
                candidate.bytes,
                record.token_tag) ||
            bytes_are_zero(record.token_tag, sizeof(record.token_tag))) {
            result.result = AURORA_IDENTITY_REAUTH_CRYPTO_ERROR;
            goto cleanup;
        }

        enum aurora_identity_reauth_store_issue_result store_result =
            core->store.issue_proof(core->store.context, &record);

        if (store_result == AURORA_IDENTITY_REAUTH_STORE_ISSUE_OK) {
            result.result = AURORA_IDENTITY_REAUTH_OK;
            result.token = candidate;
            result.expires_at_ms = record.expires_at_ms;
            goto cleanup;
        }
        if (store_result == AURORA_IDENTITY_REAUTH_STORE_ISSUE_ERROR) {
            result.result = AURORA_IDENTITY_REAUTH_BACKEND_ERROR;
            goto cleanup;
        }
        if (store_result != AURORA_IDENTITY_REAUTH_STORE_ISSUE_CONFLICT) {
            result.result = AURORA_IDENTITY_REAUTH_BACKEND_ERROR;
            goto cleanup;
        }
    }

    result.result = AURORA_IDENTITY_REAUTH_RANDOM_ERROR;

cleanup:
    if (result.result != AURORA_IDENTITY_REAUTH_OK) {
        aurora_identity_secure_zero(&result.token, sizeof(result.token));
        result.expires_at_ms = 0u;
    }
    aurora_identity_secure_zero(&candidate, sizeof(candidate));
    aurora_identity_secure_zero(&record, sizeof(record));
    return result;
}

struct aurora_identity_reauth_consume_result aurora_identity_reauth_consume(
    const struct aurora_identity_reauth_core *core,
    const struct aurora_identity_reauth_token *token,
    const struct aurora_identity_user_id *expected_user_id,
    uint64_t expected_session_generation,
    uint32_t expected_purpose
) {
    struct aurora_identity_reauth_consume_result result =
        consume_result(AURORA_IDENTITY_REAUTH_INVALID_ARGUMENT);
    struct aurora_identity_reauth_record record;
    uint8_t token_tag[AURORA_IDENTITY_REAUTH_PROOF_TAG_SIZE];
    uint64_t now_ms = 0u;

    memset(&record, 0, sizeof(record));
    memset(token_tag, 0, sizeof(token_tag));

    if (core == NULL || token == NULL || expected_user_id == NULL ||
        core->clock.monotonic_ms == NULL ||
        core->crypto.derive_token_tag == NULL ||
        core->store.consume_proof == NULL) {
        goto cleanup;
    }
    if (aurora_identity_user_id_is_zero(expected_user_id)) {
        result.result = AURORA_IDENTITY_REAUTH_INVALID_USER;
        goto cleanup;
    }
    if (expected_session_generation == 0u) {
        result.result = AURORA_IDENTITY_REAUTH_INVALID_ARGUMENT;
        goto cleanup;
    }
    if (!aurora_identity_reauth_purpose_valid(expected_purpose)) {
        result.result = AURORA_IDENTITY_REAUTH_INVALID_PURPOSE;
        goto cleanup;
    }
    if (aurora_identity_reauth_token_is_zero(token)) {
        result.result = AURORA_IDENTITY_REAUTH_NOT_FOUND;
        goto cleanup;
    }
    if (!core->crypto.derive_token_tag(
            core->crypto.context,
            token->bytes,
            token_tag)) {
        result.result = AURORA_IDENTITY_REAUTH_CRYPTO_ERROR;
        goto cleanup;
    }
    if (!core->clock.monotonic_ms(core->clock.context, &now_ms)) {
        result.result = AURORA_IDENTITY_REAUTH_BACKEND_ERROR;
        goto cleanup;
    }

    enum aurora_identity_reauth_store_consume_result store_result =
        core->store.consume_proof(
            core->store.context,
            token_tag,
            now_ms,
            &record);

    if (store_result == AURORA_IDENTITY_REAUTH_STORE_CONSUME_NOT_FOUND) {
        result.result = AURORA_IDENTITY_REAUTH_NOT_FOUND;
        goto cleanup;
    }
    if (store_result == AURORA_IDENTITY_REAUTH_STORE_CONSUME_EXPIRED) {
        result.result = AURORA_IDENTITY_REAUTH_EXPIRED;
        goto cleanup;
    }
    if (store_result != AURORA_IDENTITY_REAUTH_STORE_CONSUME_OK) {
        result.result = AURORA_IDENTITY_REAUTH_BACKEND_ERROR;
        goto cleanup;
    }

    if (record.record_version != AURORA_IDENTITY_REAUTH_PROOF_RECORD_VERSION ||
        aurora_identity_user_id_is_zero(&record.user_id) ||
        aurora_identity_credential_id_is_zero(&record.credential_id) ||
        record.session_generation == 0u ||
        !aurora_identity_reauth_purpose_valid(record.purpose) ||
        record.expires_at_ms <= record.issued_at_ms ||
        record.expires_at_ms <= now_ms) {
        result.result = AURORA_IDENTITY_REAUTH_BACKEND_ERROR;
        goto cleanup;
    }

    if (!user_ids_equal(&record.user_id, expected_user_id)) {
        result.result = AURORA_IDENTITY_REAUTH_USER_MISMATCH;
        goto cleanup;
    }
    if (record.session_generation != expected_session_generation) {
        result.result = AURORA_IDENTITY_REAUTH_SESSION_MISMATCH;
        goto cleanup;
    }
    if (record.purpose != expected_purpose) {
        result.result = AURORA_IDENTITY_REAUTH_PURPOSE_MISMATCH;
        goto cleanup;
    }

    result.result = AURORA_IDENTITY_REAUTH_OK;
    result.user_id = record.user_id;
    result.credential_id = record.credential_id;
    result.session_generation = record.session_generation;
    result.purpose = record.purpose;

cleanup:
    aurora_identity_secure_zero(token_tag, sizeof(token_tag));
    aurora_identity_secure_zero(&record, sizeof(record));
    return result;
}
