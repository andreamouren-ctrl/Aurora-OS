#include "aurora/identity/core.h"

#include <limits.h>
#include <string.h>

static uint64_t saturating_add_u64(uint64_t left, uint64_t right) {
    if (UINT64_MAX - left < right) {
        return UINT64_MAX;
    }
    return left + right;
}

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

static bool generate_nonzero_random_id(
    const struct aurora_identity_random_ops *random,
    uint8_t *out_bytes,
    size_t size) {
    uint32_t attempt;

    if (random == NULL || random->fill_random == NULL || out_bytes == NULL || size == 0u) {
        return false;
    }

    for (attempt = 0u; attempt < AURORA_IDENTITY_ID_GENERATION_ATTEMPTS; ++attempt) {
        if (!random->fill_random(random->context, out_bytes, size)) {
            return false;
        }
        if (!bytes_are_zero(out_bytes, size)) {
            return true;
        }
    }

    aurora_identity_secure_zero(out_bytes, size);
    return false;
}

static bool creation_policy_is_valid(const struct aurora_identity_creation_policy *policy) {
    if (policy == NULL) {
        return false;
    }

    if (policy->kdf.algorithm != AURORA_IDENTITY_KDF_ARGON2ID ||
        policy->kdf.parameters_version == 0u || policy->kdf.memory_kib == 0u ||
        policy->kdf.time_cost == 0u || policy->kdf.parallelism == 0u) {
        return false;
    }

    if (policy->salt_size == 0u || policy->salt_size > AURORA_IDENTITY_SALT_MAX_SIZE ||
        policy->verifier_size == 0u ||
        policy->verifier_size > AURORA_IDENTITY_VERIFIER_MAX_SIZE) {
        return false;
    }

    if (policy->identity_record_version == 0u || policy->policy_version == 0u) {
        return false;
    }

    return true;
}

static struct aurora_identity_auth_result auth_result(enum aurora_identity_result result) {
    struct aurora_identity_auth_result value;
    memset(&value, 0, sizeof(value));
    value.result = result;
    return value;
}

static struct aurora_identity_create_result create_result(enum aurora_identity_result result) {
    struct aurora_identity_create_result value;
    memset(&value, 0, sizeof(value));
    value.result = result;
    return value;
}

void aurora_identity_secure_zero(void *buffer, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;

    if (buffer == NULL) {
        return;
    }

    while (size > 0u) {
        *bytes = 0u;
        ++bytes;
        --size;
    }
}

bool aurora_identity_user_id_is_zero(const struct aurora_identity_user_id *user_id) {
    if (user_id == NULL) {
        return true;
    }

    return bytes_are_zero(user_id->bytes, AURORA_IDENTITY_USER_ID_SIZE);
}

bool aurora_identity_credential_id_is_zero(
    const struct aurora_identity_credential_id *credential_id) {
    if (credential_id == NULL) {
        return true;
    }

    return bytes_are_zero(credential_id->bytes, AURORA_IDENTITY_CREDENTIAL_ID_SIZE);
}

enum aurora_identity_result aurora_identity_normalize_key(
    const char *input,
    size_t input_length,
    struct aurora_identity_normalized_key *out_key) {
    size_t input_index;
    size_t output_index = 0u;

    if (input == NULL || out_key == NULL) {
        return AURORA_IDENTITY_INVALID_ARGUMENT;
    }

    aurora_identity_secure_zero(out_key, sizeof(*out_key));

    for (input_index = 0u; input_index < input_length; ++input_index) {
        unsigned char character = (unsigned char)input[input_index];

        if (character == (unsigned char)' ' || character == (unsigned char)'-') {
            continue;
        }

        if (character >= (unsigned char)'a' && character <= (unsigned char)'z') {
            character = (unsigned char)(character - (unsigned char)'a' + (unsigned char)'A');
        } else if (!((character >= (unsigned char)'A' && character <= (unsigned char)'Z') ||
                     (character >= (unsigned char)'0' && character <= (unsigned char)'9'))) {
            aurora_identity_secure_zero(out_key, sizeof(*out_key));
            return AURORA_IDENTITY_INVALID_KEY_FORMAT;
        }

        if (output_index >= AURORA_IDENTITY_KEY_MAX_LEN) {
            aurora_identity_secure_zero(out_key, sizeof(*out_key));
            return AURORA_IDENTITY_INVALID_KEY_FORMAT;
        }

        out_key->bytes[output_index++] = (char)character;
    }

    if (output_index < AURORA_IDENTITY_KEY_MIN_LEN ||
        output_index > AURORA_IDENTITY_KEY_MAX_LEN) {
        aurora_identity_secure_zero(out_key, sizeof(*out_key));
        return AURORA_IDENTITY_INVALID_KEY_FORMAT;
    }

    out_key->bytes[output_index] = '\0';
    out_key->length = output_index;
    return AURORA_IDENTITY_OK;
}

uint64_t aurora_identity_compute_throttle_delay_ms(
    const struct aurora_identity_throttle_policy *policy,
    uint32_t failed_attempts) {
    uint32_t exponent;
    uint64_t delay;

    if (policy == NULL || policy->initial_delay_ms == 0u ||
        policy->maximum_delay_ms == 0u || failed_attempts <= policy->free_failures) {
        return 0u;
    }

    delay = policy->initial_delay_ms;
    exponent = failed_attempts - policy->free_failures - 1u;

    while (exponent > 0u && delay < policy->maximum_delay_ms) {
        if (delay > policy->maximum_delay_ms / 2u) {
            delay = policy->maximum_delay_ms;
            break;
        }
        delay *= 2u;
        --exponent;
    }

    if (delay > policy->maximum_delay_ms) {
        delay = policy->maximum_delay_ms;
    }

    return delay;
}

struct aurora_identity_auth_result aurora_identity_authenticate_key(
    const struct aurora_identity_core *core,
    const char *candidate_key,
    size_t candidate_key_length) {
    struct aurora_identity_auth_result result = auth_result(AURORA_IDENTITY_INVALID_ARGUMENT);
    struct aurora_identity_normalized_key normalized_key;
    struct aurora_identity_key_record record;
    uint8_t lookup_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE];
    enum aurora_identity_store_result store_result;
    enum aurora_identity_result normalize_result;
    uint64_t now_ms = 0u;
    bool matches = false;
    bool have_record = false;

    memset(&normalized_key, 0, sizeof(normalized_key));
    memset(&record, 0, sizeof(record));
    memset(lookup_tag, 0, sizeof(lookup_tag));

    if (core == NULL || candidate_key == NULL ||
        core->crypto.derive_lookup_tag == NULL || core->crypto.verify_key == NULL ||
        core->store.find_key_record_by_lookup_tag == NULL ||
        core->store.store_failure_state == NULL || core->store.clear_failure_state == NULL ||
        core->clock.monotonic_ms == NULL) {
        goto cleanup;
    }

    normalize_result = aurora_identity_normalize_key(
        candidate_key, candidate_key_length, &normalized_key);
    if (normalize_result != AURORA_IDENTITY_OK) {
        result.result = normalize_result;
        goto cleanup;
    }

    if (!core->crypto.derive_lookup_tag(
            core->crypto.context,
            normalized_key.bytes,
            normalized_key.length,
            lookup_tag)) {
        result.result = AURORA_IDENTITY_CRYPTO_ERROR;
        goto cleanup;
    }

    store_result = core->store.find_key_record_by_lookup_tag(
        core->store.context, lookup_tag, &record);
    if (store_result == AURORA_IDENTITY_STORE_NOT_FOUND) {
        result.result = AURORA_IDENTITY_NOT_FOUND;
        goto cleanup;
    }
    if (store_result != AURORA_IDENTITY_STORE_OK) {
        result.result = AURORA_IDENTITY_BACKEND_ERROR;
        goto cleanup;
    }
    have_record = true;

    if (record.salt_size > AURORA_IDENTITY_SALT_MAX_SIZE ||
        record.verifier_size > AURORA_IDENTITY_VERIFIER_MAX_SIZE ||
        aurora_identity_user_id_is_zero(&record.user_id)) {
        result.result = AURORA_IDENTITY_BACKEND_ERROR;
        goto cleanup;
    }

    if (record.status == AURORA_IDENTITY_RECORD_DISABLED) {
        result.result = AURORA_IDENTITY_DISABLED;
        goto cleanup;
    }
    if (record.status == AURORA_IDENTITY_RECORD_RECOVERY_REQUIRED) {
        result.result = AURORA_IDENTITY_RECOVERY_REQUIRED;
        goto cleanup;
    }
    if (record.status != AURORA_IDENTITY_RECORD_ACTIVE) {
        result.result = AURORA_IDENTITY_BACKEND_ERROR;
        goto cleanup;
    }

    if (!core->clock.monotonic_ms(core->clock.context, &now_ms)) {
        result.result = AURORA_IDENTITY_BACKEND_ERROR;
        goto cleanup;
    }

    if (record.throttle_until_ms > now_ms) {
        result.result = AURORA_IDENTITY_THROTTLED;
        result.retry_after_ms = record.throttle_until_ms - now_ms;
        goto cleanup;
    }

    /*
     * A persistent backend restores failed_attempts but deliberately clears
     * the monotonic deadline when a new service/boot clock epoch begins.
     * Re-arm the penalty exactly once in the new epoch before allowing another
     * verifier attempt. A nonzero expired deadline means this epoch already
     * served the persisted penalty and must not be re-armed repeatedly.
     */
    if (record.throttle_until_ms == 0u &&
        record.failed_attempts > core->throttle_policy.free_failures) {
        uint64_t persisted_delay_ms = aurora_identity_compute_throttle_delay_ms(
            &core->throttle_policy,
            record.failed_attempts);

        if (persisted_delay_ms != 0u) {
            uint64_t rearmed_until_ms = saturating_add_u64(now_ms, persisted_delay_ms);

            if (!core->store.store_failure_state(
                    core->store.context,
                    &record.user_id,
                    record.failed_attempts,
                    rearmed_until_ms)) {
                result.result = AURORA_IDENTITY_BACKEND_ERROR;
                goto cleanup;
            }

            result.result = AURORA_IDENTITY_THROTTLED;
            result.retry_after_ms = persisted_delay_ms;
            goto cleanup;
        }
    }

    if (!core->crypto.verify_key(
            core->crypto.context,
            normalized_key.bytes,
            normalized_key.length,
            &record,
            &matches)) {
        result.result = AURORA_IDENTITY_CRYPTO_ERROR;
        goto cleanup;
    }

    if (!matches) {
        uint32_t failures = record.failed_attempts;
        uint64_t delay_ms;
        uint64_t throttle_until_ms;

        if (failures != UINT32_MAX) {
            ++failures;
        }

        delay_ms = aurora_identity_compute_throttle_delay_ms(
            &core->throttle_policy, failures);
        throttle_until_ms = saturating_add_u64(now_ms, delay_ms);

        if (!core->store.store_failure_state(
                core->store.context,
                &record.user_id,
                failures,
                throttle_until_ms)) {
            result.result = AURORA_IDENTITY_BACKEND_ERROR;
            goto cleanup;
        }

        result.result = AURORA_IDENTITY_AUTH_FAILED;
        result.retry_after_ms = delay_ms;
        goto cleanup;
    }

    if ((record.failed_attempts != 0u || record.throttle_until_ms != 0u) &&
        !core->store.clear_failure_state(core->store.context, &record.user_id)) {
        result.result = AURORA_IDENTITY_BACKEND_ERROR;
        goto cleanup;
    }

    result.result = AURORA_IDENTITY_OK;
    result.user_id = record.user_id;
    result.retry_after_ms = 0u;

cleanup:
    aurora_identity_secure_zero(&normalized_key, sizeof(normalized_key));
    aurora_identity_secure_zero(lookup_tag, sizeof(lookup_tag));
    if (have_record) {
        aurora_identity_secure_zero(&record, sizeof(record));
    }
    return result;
}

struct aurora_identity_create_result aurora_identity_create_with_key(
    const struct aurora_identity_core *core,
    const char *candidate_key,
    size_t candidate_key_length) {
    struct aurora_identity_create_result result =
        create_result(AURORA_IDENTITY_INVALID_ARGUMENT);
    struct aurora_identity_normalized_key normalized_key;
    struct aurora_identity_record identity;
    struct aurora_identity_key_record key_record;
    struct aurora_identity_key_record existing_record;
    uint8_t lookup_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE];
    enum aurora_identity_result normalize_result;
    enum aurora_identity_store_result lookup_result;
    enum aurora_identity_store_create_result create_store_result;
    bool have_existing_record = false;

    memset(&normalized_key, 0, sizeof(normalized_key));
    memset(&identity, 0, sizeof(identity));
    memset(&key_record, 0, sizeof(key_record));
    memset(&existing_record, 0, sizeof(existing_record));
    memset(lookup_tag, 0, sizeof(lookup_tag));

    if (core == NULL || candidate_key == NULL ||
        core->crypto.derive_lookup_tag == NULL ||
        core->crypto.derive_key_verifier == NULL ||
        core->random.fill_random == NULL ||
        core->store.find_key_record_by_lookup_tag == NULL ||
        core->store.create_identity_with_key == NULL) {
        goto cleanup;
    }

    if (!creation_policy_is_valid(&core->creation_policy)) {
        result.result = AURORA_IDENTITY_POLICY_ERROR;
        goto cleanup;
    }

    normalize_result = aurora_identity_normalize_key(
        candidate_key, candidate_key_length, &normalized_key);
    if (normalize_result != AURORA_IDENTITY_OK) {
        result.result = normalize_result;
        goto cleanup;
    }

    if (!core->crypto.derive_lookup_tag(
            core->crypto.context,
            normalized_key.bytes,
            normalized_key.length,
            lookup_tag)) {
        result.result = AURORA_IDENTITY_CRYPTO_ERROR;
        goto cleanup;
    }

    lookup_result = core->store.find_key_record_by_lookup_tag(
        core->store.context, lookup_tag, &existing_record);
    if (lookup_result == AURORA_IDENTITY_STORE_OK) {
        have_existing_record = true;
        result.result = AURORA_IDENTITY_ALREADY_EXISTS;
        goto cleanup;
    }
    if (lookup_result != AURORA_IDENTITY_STORE_NOT_FOUND) {
        result.result = AURORA_IDENTITY_BACKEND_ERROR;
        goto cleanup;
    }

    if (!generate_nonzero_random_id(
            &core->random, identity.user_id.bytes, AURORA_IDENTITY_USER_ID_SIZE)) {
        result.result = AURORA_IDENTITY_RANDOM_ERROR;
        goto cleanup;
    }

    if (!generate_nonzero_random_id(
            &core->random,
            key_record.credential_id.bytes,
            AURORA_IDENTITY_CREDENTIAL_ID_SIZE)) {
        result.result = AURORA_IDENTITY_RANDOM_ERROR;
        goto cleanup;
    }

    identity.status = AURORA_IDENTITY_RECORD_ACTIVE;
    identity.role = AURORA_IDENTITY_ROLE_UNASSIGNED;
    identity.policy_version = core->creation_policy.policy_version;
    identity.record_version = core->creation_policy.identity_record_version;

    key_record.user_id = identity.user_id;
    memcpy(key_record.lookup_tag, lookup_tag, sizeof(key_record.lookup_tag));
    key_record.kdf = core->creation_policy.kdf;
    key_record.salt_size = core->creation_policy.salt_size;
    key_record.verifier_size = core->creation_policy.verifier_size;
    key_record.status = AURORA_IDENTITY_RECORD_ACTIVE;

    if (!core->random.fill_random(
            core->random.context, key_record.salt, key_record.salt_size)) {
        result.result = AURORA_IDENTITY_RANDOM_ERROR;
        goto cleanup;
    }

    if (!core->crypto.derive_key_verifier(
            core->crypto.context,
            normalized_key.bytes,
            normalized_key.length,
            &key_record.kdf,
            key_record.salt,
            key_record.salt_size,
            key_record.verifier,
            key_record.verifier_size)) {
        result.result = AURORA_IDENTITY_CRYPTO_ERROR;
        goto cleanup;
    }

    create_store_result = core->store.create_identity_with_key(
        core->store.context, &identity, &key_record);
    if (create_store_result == AURORA_IDENTITY_STORE_CREATE_CONFLICT) {
        result.result = AURORA_IDENTITY_ALREADY_EXISTS;
        goto cleanup;
    }
    if (create_store_result != AURORA_IDENTITY_STORE_CREATE_OK) {
        result.result = AURORA_IDENTITY_BACKEND_ERROR;
        goto cleanup;
    }

    result.result = AURORA_IDENTITY_OK;
    result.user_id = identity.user_id;
    result.credential_id = key_record.credential_id;

cleanup:
    aurora_identity_secure_zero(&normalized_key, sizeof(normalized_key));
    aurora_identity_secure_zero(lookup_tag, sizeof(lookup_tag));
    aurora_identity_secure_zero(&identity, sizeof(identity));
    aurora_identity_secure_zero(&key_record, sizeof(key_record));
    if (have_existing_record) {
        aurora_identity_secure_zero(&existing_record, sizeof(existing_record));
    }
    return result;
}
