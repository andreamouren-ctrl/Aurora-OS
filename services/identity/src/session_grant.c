#include "aurora/identity/session_grant.h"

#include <limits.h>
#include <string.h>

static bool bytes_are_zero(const uint8_t *bytes, size_t size) {
    uint8_t aggregate = 0u;
    size_t index;

    if (bytes == NULL) {
        return true;
    }

    for (index = 0u; index < size; ++index) {
        aggregate |= bytes[index];
    }

    return aggregate == 0u;
}

static uint64_t saturating_add_u64(uint64_t left, uint64_t right) {
    if (UINT64_MAX - left < right) {
        return UINT64_MAX;
    }
    return left + right;
}

static struct aurora_identity_session_grant_issue_result issue_result(
    enum aurora_identity_session_grant_result result) {
    struct aurora_identity_session_grant_issue_result value;
    memset(&value, 0, sizeof(value));
    value.result = result;
    return value;
}

static struct aurora_identity_session_grant_consume_result consume_result(
    enum aurora_identity_session_grant_result result) {
    struct aurora_identity_session_grant_consume_result value;
    memset(&value, 0, sizeof(value));
    value.result = result;
    return value;
}

bool aurora_identity_session_grant_token_is_zero(
    const struct aurora_identity_session_grant_token *token) {
    if (token == NULL) {
        return true;
    }

    return bytes_are_zero(token->bytes, AURORA_IDENTITY_SESSION_GRANT_TOKEN_SIZE);
}

struct aurora_identity_session_grant_issue_result
    aurora_identity_session_grant_issue(
        const struct aurora_identity_session_grant_core *core,
        const struct aurora_identity_user_id *authenticated_user_id) {
    struct aurora_identity_session_grant_issue_result result =
        issue_result(AURORA_IDENTITY_SESSION_GRANT_INVALID_ARGUMENT);
    struct aurora_identity_session_grant_record record;
    struct aurora_identity_session_grant_token candidate_token;
    enum aurora_identity_session_grant_store_issue_result store_result;
    uint64_t now_ms = 0u;
    uint32_t attempt;

    memset(&record, 0, sizeof(record));
    memset(&candidate_token, 0, sizeof(candidate_token));

    if (core == NULL || authenticated_user_id == NULL ||
        core->random.fill_random == NULL || core->clock.monotonic_ms == NULL ||
        core->crypto.derive_token_tag == NULL || core->store.issue_grant == NULL) {
        goto cleanup;
    }

    if (aurora_identity_user_id_is_zero(authenticated_user_id)) {
        result.result = AURORA_IDENTITY_SESSION_GRANT_INVALID_USER;
        goto cleanup;
    }

    if (core->policy.ttl_ms == 0u ||
        core->policy.ttl_ms > AURORA_IDENTITY_SESSION_GRANT_MAX_TTL_MS) {
        result.result = AURORA_IDENTITY_SESSION_GRANT_POLICY_ERROR;
        goto cleanup;
    }

    if (!core->clock.monotonic_ms(core->clock.context, &now_ms)) {
        result.result = AURORA_IDENTITY_SESSION_GRANT_BACKEND_ERROR;
        goto cleanup;
    }

    record.user_id = *authenticated_user_id;
    record.issued_at_ms = now_ms;
    record.expires_at_ms = saturating_add_u64(now_ms, core->policy.ttl_ms);
    record.record_version = AURORA_IDENTITY_SESSION_GRANT_RECORD_VERSION;

    for (attempt = 0u;
         attempt < AURORA_IDENTITY_SESSION_GRANT_GENERATION_ATTEMPTS;
         ++attempt) {
        aurora_identity_secure_zero(&candidate_token, sizeof(candidate_token));
        aurora_identity_secure_zero(record.token_tag, sizeof(record.token_tag));

        if (!core->random.fill_random(
                core->random.context,
                candidate_token.bytes,
                sizeof(candidate_token.bytes))) {
            result.result = AURORA_IDENTITY_SESSION_GRANT_RANDOM_ERROR;
            goto cleanup;
        }

        if (aurora_identity_session_grant_token_is_zero(&candidate_token)) {
            continue;
        }

        if (!core->crypto.derive_token_tag(
                core->crypto.context,
                candidate_token.bytes,
                record.token_tag)) {
            result.result = AURORA_IDENTITY_SESSION_GRANT_CRYPTO_ERROR;
            goto cleanup;
        }

        if (bytes_are_zero(record.token_tag, sizeof(record.token_tag))) {
            result.result = AURORA_IDENTITY_SESSION_GRANT_CRYPTO_ERROR;
            goto cleanup;
        }

        store_result = core->store.issue_grant(core->store.context, &record);
        if (store_result == AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_OK) {
            result.result = AURORA_IDENTITY_SESSION_GRANT_OK;
            result.token = candidate_token;
            result.expires_at_ms = record.expires_at_ms;
            goto cleanup;
        }

        if (store_result == AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_ERROR) {
            result.result = AURORA_IDENTITY_SESSION_GRANT_BACKEND_ERROR;
            goto cleanup;
        }

        if (store_result != AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_CONFLICT) {
            result.result = AURORA_IDENTITY_SESSION_GRANT_BACKEND_ERROR;
            goto cleanup;
        }
    }

    result.result = AURORA_IDENTITY_SESSION_GRANT_RANDOM_ERROR;

cleanup:
    if (result.result != AURORA_IDENTITY_SESSION_GRANT_OK) {
        aurora_identity_secure_zero(&result.token, sizeof(result.token));
        result.expires_at_ms = 0u;
    }
    aurora_identity_secure_zero(&candidate_token, sizeof(candidate_token));
    aurora_identity_secure_zero(&record, sizeof(record));
    return result;
}

struct aurora_identity_session_grant_consume_result
    aurora_identity_session_grant_consume(
        const struct aurora_identity_session_grant_core *core,
        const struct aurora_identity_session_grant_token *token) {
    struct aurora_identity_session_grant_consume_result result =
        consume_result(AURORA_IDENTITY_SESSION_GRANT_INVALID_ARGUMENT);
    struct aurora_identity_session_grant_record record;
    uint8_t token_tag[AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE];
    enum aurora_identity_session_grant_store_consume_result store_result;
    uint64_t now_ms = 0u;

    memset(&record, 0, sizeof(record));
    memset(token_tag, 0, sizeof(token_tag));

    if (core == NULL || token == NULL || core->clock.monotonic_ms == NULL ||
        core->crypto.derive_token_tag == NULL || core->store.consume_grant == NULL) {
        goto cleanup;
    }

    if (aurora_identity_session_grant_token_is_zero(token)) {
        result.result = AURORA_IDENTITY_SESSION_GRANT_NOT_FOUND;
        goto cleanup;
    }

    if (!core->crypto.derive_token_tag(
            core->crypto.context, token->bytes, token_tag)) {
        result.result = AURORA_IDENTITY_SESSION_GRANT_CRYPTO_ERROR;
        goto cleanup;
    }

    if (!core->clock.monotonic_ms(core->clock.context, &now_ms)) {
        result.result = AURORA_IDENTITY_SESSION_GRANT_BACKEND_ERROR;
        goto cleanup;
    }

    store_result = core->store.consume_grant(
        core->store.context, token_tag, now_ms, &record);

    if (store_result == AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_NOT_FOUND) {
        result.result = AURORA_IDENTITY_SESSION_GRANT_NOT_FOUND;
        goto cleanup;
    }
    if (store_result == AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_EXPIRED) {
        result.result = AURORA_IDENTITY_SESSION_GRANT_EXPIRED;
        goto cleanup;
    }
    if (store_result != AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_OK) {
        result.result = AURORA_IDENTITY_SESSION_GRANT_BACKEND_ERROR;
        goto cleanup;
    }

    if (record.record_version != AURORA_IDENTITY_SESSION_GRANT_RECORD_VERSION ||
        aurora_identity_user_id_is_zero(&record.user_id) ||
        record.expires_at_ms <= record.issued_at_ms ||
        record.expires_at_ms <= now_ms) {
        result.result = AURORA_IDENTITY_SESSION_GRANT_BACKEND_ERROR;
        goto cleanup;
    }

    result.result = AURORA_IDENTITY_SESSION_GRANT_OK;
    result.user_id = record.user_id;

cleanup:
    aurora_identity_secure_zero(token_tag, sizeof(token_tag));
    aurora_identity_secure_zero(&record, sizeof(record));
    return result;
}
