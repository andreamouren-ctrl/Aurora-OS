#include "aurora/identity/rotation.h"

#include <string.h>

static bool rotation_creation_policy_is_valid(
    const struct aurora_identity_creation_policy *policy) {
    if (policy == NULL) {
        return false;
    }

    if (policy->kdf.algorithm != AURORA_IDENTITY_KDF_ARGON2ID ||
        policy->kdf.parameters_version == 0u ||
        policy->kdf.memory_kib == 0u ||
        policy->kdf.time_cost == 0u ||
        policy->kdf.parallelism == 0u) {
        return false;
    }

    if (policy->salt_size == 0u ||
        policy->salt_size > AURORA_IDENTITY_SALT_MAX_SIZE ||
        policy->verifier_size == 0u ||
        policy->verifier_size > AURORA_IDENTITY_VERIFIER_MAX_SIZE) {
        return false;
    }

    if (policy->identity_record_version == 0u || policy->policy_version == 0u) {
        return false;
    }

    return true;
}

static bool generate_rotation_credential_id(
    const struct aurora_identity_random_ops *random,
    struct aurora_identity_credential_id *out_id) {
    uint32_t attempt;

    if (random == NULL || random->fill_random == NULL || out_id == NULL) {
        return false;
    }

    for (attempt = 0u; attempt < AURORA_IDENTITY_ID_GENERATION_ATTEMPTS; ++attempt) {
        if (!random->fill_random(
                random->context,
                out_id->bytes,
                AURORA_IDENTITY_CREDENTIAL_ID_SIZE)) {
            aurora_identity_secure_zero(out_id, sizeof(*out_id));
            return false;
        }

        if (!aurora_identity_credential_id_is_zero(out_id)) {
            return true;
        }
    }

    aurora_identity_secure_zero(out_id, sizeof(*out_id));
    return false;
}

static struct aurora_identity_rotation_result rotation_result(
    enum aurora_identity_result result) {
    struct aurora_identity_rotation_result value;
    memset(&value, 0, sizeof(value));
    value.result = result;
    return value;
}

struct aurora_identity_rotation_result aurora_identity_rotate_key(
    const struct aurora_identity_rotation_context *context,
    const struct aurora_identity_user_id *user_id,
    const struct aurora_identity_credential_id *current_credential_id,
    const char *new_key,
    size_t new_key_length) {
    struct aurora_identity_rotation_result result =
        rotation_result(AURORA_IDENTITY_INVALID_ARGUMENT);
    struct aurora_identity_normalized_key normalized_key;
    struct aurora_identity_key_record replacement;
    struct aurora_identity_key_record existing_record;
    uint8_t lookup_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE];
    enum aurora_identity_result normalize_result;
    enum aurora_identity_store_result lookup_result;
    enum aurora_identity_rotation_store_result rotate_result;
    bool have_existing_record = false;

    memset(&normalized_key, 0, sizeof(normalized_key));
    memset(&replacement, 0, sizeof(replacement));
    memset(&existing_record, 0, sizeof(existing_record));
    memset(lookup_tag, 0, sizeof(lookup_tag));

    if (context == NULL || context->core == NULL || user_id == NULL ||
        current_credential_id == NULL || new_key == NULL ||
        aurora_identity_user_id_is_zero(user_id) ||
        aurora_identity_credential_id_is_zero(current_credential_id) ||
        context->core->crypto.derive_lookup_tag == NULL ||
        context->core->crypto.derive_key_verifier == NULL ||
        context->core->random.fill_random == NULL ||
        context->core->store.find_key_record_by_lookup_tag == NULL ||
        context->store.replace_key_credential == NULL) {
        goto cleanup;
    }

    if (!rotation_creation_policy_is_valid(&context->core->creation_policy)) {
        result.result = AURORA_IDENTITY_POLICY_ERROR;
        goto cleanup;
    }

    normalize_result = aurora_identity_normalize_key(
        new_key, new_key_length, &normalized_key);
    if (normalize_result != AURORA_IDENTITY_OK) {
        result.result = normalize_result;
        goto cleanup;
    }

    if (!context->core->crypto.derive_lookup_tag(
            context->core->crypto.context,
            normalized_key.bytes,
            normalized_key.length,
            lookup_tag)) {
        result.result = AURORA_IDENTITY_CRYPTO_ERROR;
        goto cleanup;
    }

    lookup_result = context->core->store.find_key_record_by_lookup_tag(
        context->core->store.context, lookup_tag, &existing_record);
    if (lookup_result == AURORA_IDENTITY_STORE_OK) {
        have_existing_record = true;
        result.result = AURORA_IDENTITY_ALREADY_EXISTS;
        goto cleanup;
    }
    if (lookup_result != AURORA_IDENTITY_STORE_NOT_FOUND) {
        result.result = AURORA_IDENTITY_BACKEND_ERROR;
        goto cleanup;
    }

    replacement.user_id = *user_id;
    memcpy(replacement.lookup_tag, lookup_tag, sizeof(replacement.lookup_tag));
    replacement.kdf = context->core->creation_policy.kdf;
    replacement.salt_size = context->core->creation_policy.salt_size;
    replacement.verifier_size = context->core->creation_policy.verifier_size;
    replacement.status = AURORA_IDENTITY_RECORD_ACTIVE;
    replacement.failed_attempts = 0u;
    replacement.throttle_until_ms = 0u;

    if (!generate_rotation_credential_id(
            &context->core->random, &replacement.credential_id)) {
        result.result = AURORA_IDENTITY_RANDOM_ERROR;
        goto cleanup;
    }

    if (!context->core->random.fill_random(
            context->core->random.context,
            replacement.salt,
            replacement.salt_size)) {
        result.result = AURORA_IDENTITY_RANDOM_ERROR;
        goto cleanup;
    }

    if (!context->core->crypto.derive_key_verifier(
            context->core->crypto.context,
            normalized_key.bytes,
            normalized_key.length,
            &replacement.kdf,
            replacement.salt,
            replacement.salt_size,
            replacement.verifier,
            replacement.verifier_size)) {
        result.result = AURORA_IDENTITY_CRYPTO_ERROR;
        goto cleanup;
    }

    rotate_result = context->store.replace_key_credential(
        context->store.context,
        user_id,
        current_credential_id,
        &replacement);

    if (rotate_result == AURORA_IDENTITY_ROTATION_STORE_CURRENT_NOT_FOUND) {
        result.result = AURORA_IDENTITY_NOT_FOUND;
        goto cleanup;
    }
    if (rotate_result == AURORA_IDENTITY_ROTATION_STORE_CONFLICT) {
        result.result = AURORA_IDENTITY_ALREADY_EXISTS;
        goto cleanup;
    }
    if (rotate_result != AURORA_IDENTITY_ROTATION_STORE_OK) {
        result.result = AURORA_IDENTITY_BACKEND_ERROR;
        goto cleanup;
    }

    result.result = AURORA_IDENTITY_OK;
    result.new_credential_id = replacement.credential_id;

cleanup:
    aurora_identity_secure_zero(&normalized_key, sizeof(normalized_key));
    aurora_identity_secure_zero(lookup_tag, sizeof(lookup_tag));
    aurora_identity_secure_zero(&replacement, sizeof(replacement));
    if (have_existing_record) {
        aurora_identity_secure_zero(&existing_record, sizeof(existing_record));
    }
    return result;
}
