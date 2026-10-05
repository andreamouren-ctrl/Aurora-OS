#include "aurora/identity/argon2id_provider.h"

#include "aurora/identity/crypto_foundation.h"

#include "argon2.h"

#include <limits.h>
#include <string.h>

#define ARGON2_BLOCKS_PER_LANE_MIN 8u

static void secure_zero(void *buffer, size_t size) {
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

static bool limits_are_valid(
    const struct aurora_identity_argon2id_limits *limits) {
    if (limits == NULL || limits->minimum_memory_kib == 0u ||
        limits->maximum_memory_kib < limits->minimum_memory_kib ||
        limits->minimum_time_cost == 0u ||
        limits->maximum_time_cost < limits->minimum_time_cost ||
        limits->minimum_parallelism == 0u ||
        limits->maximum_parallelism < limits->minimum_parallelism ||
        limits->maximum_parallelism > UINT32_MAX / ARGON2_BLOCKS_PER_LANE_MIN ||
        limits->minimum_salt_size < 8u ||
        limits->maximum_salt_size < limits->minimum_salt_size ||
        limits->maximum_salt_size > AURORA_IDENTITY_SALT_MAX_SIZE ||
        limits->minimum_verifier_size < 4u ||
        limits->maximum_verifier_size < limits->minimum_verifier_size ||
        limits->maximum_verifier_size > AURORA_IDENTITY_VERIFIER_MAX_SIZE) {
        return false;
    }

    return true;
}

static bool kdf_is_allowed(
    const struct aurora_identity_argon2id_provider *provider,
    const struct aurora_identity_kdf_params *kdf,
    size_t salt_size,
    size_t verifier_size) {
    uint32_t minimum_memory_for_lanes;

    if (provider == NULL || !provider->initialized || kdf == NULL ||
        kdf->algorithm != AURORA_IDENTITY_KDF_ARGON2ID ||
        kdf->parameters_version != AURORA_IDENTITY_ARGON2ID_PARAMETERS_VERSION_1 ||
        kdf->memory_kib < provider->limits.minimum_memory_kib ||
        kdf->memory_kib > provider->limits.maximum_memory_kib ||
        kdf->time_cost < provider->limits.minimum_time_cost ||
        kdf->time_cost > provider->limits.maximum_time_cost ||
        kdf->parallelism < provider->limits.minimum_parallelism ||
        kdf->parallelism > provider->limits.maximum_parallelism ||
        salt_size < provider->limits.minimum_salt_size ||
        salt_size > provider->limits.maximum_salt_size ||
        verifier_size < provider->limits.minimum_verifier_size ||
        verifier_size > provider->limits.maximum_verifier_size ||
        kdf->parallelism > UINT32_MAX / ARGON2_BLOCKS_PER_LANE_MIN) {
        return false;
    }

    minimum_memory_for_lanes =
        kdf->parallelism * ARGON2_BLOCKS_PER_LANE_MIN;
    if (kdf->memory_kib < minimum_memory_for_lanes) {
        return false;
    }

    return true;
}

bool aurora_identity_argon2id_provider_init(
    struct aurora_identity_argon2id_provider *provider,
    struct aurora_identity_hmac_provider *hmac_provider,
    const struct aurora_identity_argon2id_limits *limits) {
    if (provider == NULL || hmac_provider == NULL || !hmac_provider->initialized ||
        !limits_are_valid(limits)) {
        return false;
    }

    memset(provider, 0, sizeof(*provider));
    provider->hmac_provider = hmac_provider;
    provider->limits = *limits;
    provider->initialized = true;
    return true;
}

void aurora_identity_argon2id_provider_clear(
    struct aurora_identity_argon2id_provider *provider) {
    if (provider != NULL) {
        secure_zero(provider, sizeof(*provider));
    }
}

bool aurora_identity_argon2id_provider_derive_lookup_tag(
    void *context,
    const char *normalized_key,
    size_t normalized_key_length,
    uint8_t out_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE]) {
    struct aurora_identity_argon2id_provider *provider =
        (struct aurora_identity_argon2id_provider *)context;

    if (provider == NULL || !provider->initialized ||
        provider->hmac_provider == NULL) {
        return false;
    }

    return aurora_identity_hmac_provider_derive_lookup_tag(
        provider->hmac_provider,
        normalized_key,
        normalized_key_length,
        out_tag);
}

bool aurora_identity_argon2id_provider_derive_key_verifier(
    void *context,
    const char *normalized_key,
    size_t normalized_key_length,
    const struct aurora_identity_kdf_params *kdf,
    const uint8_t *salt,
    size_t salt_size,
    uint8_t *out_verifier,
    size_t verifier_size) {
    struct aurora_identity_argon2id_provider *provider =
        (struct aurora_identity_argon2id_provider *)context;
    int result;

    if (provider == NULL || !provider->initialized || normalized_key == NULL ||
        normalized_key_length == 0u ||
        normalized_key_length > AURORA_IDENTITY_KEY_MAX_LEN || salt == NULL ||
        out_verifier == NULL ||
        !kdf_is_allowed(provider, kdf, salt_size, verifier_size)) {
        return false;
    }

    result = argon2id_hash_raw(
        kdf->time_cost,
        kdf->memory_kib,
        kdf->parallelism,
        normalized_key,
        normalized_key_length,
        salt,
        salt_size,
        out_verifier,
        verifier_size);

    if (result != ARGON2_OK) {
        secure_zero(out_verifier, verifier_size);
        return false;
    }

    return true;
}

bool aurora_identity_argon2id_provider_verify_key(
    void *context,
    const char *normalized_key,
    size_t normalized_key_length,
    const struct aurora_identity_key_record *record,
    bool *out_matches) {
    struct aurora_identity_argon2id_provider *provider =
        (struct aurora_identity_argon2id_provider *)context;
    uint8_t derived[AURORA_IDENTITY_VERIFIER_MAX_SIZE];
    bool derived_ok;

    if (out_matches != NULL) {
        *out_matches = false;
    }

    memset(derived, 0, sizeof(derived));

    if (provider == NULL || !provider->initialized || normalized_key == NULL ||
        record == NULL || out_matches == NULL ||
        record->salt_size > AURORA_IDENTITY_SALT_MAX_SIZE ||
        record->verifier_size > AURORA_IDENTITY_VERIFIER_MAX_SIZE ||
        !kdf_is_allowed(
            provider,
            &record->kdf,
            record->salt_size,
            record->verifier_size)) {
        secure_zero(derived, sizeof(derived));
        return false;
    }

    derived_ok = aurora_identity_argon2id_provider_derive_key_verifier(
        provider,
        normalized_key,
        normalized_key_length,
        &record->kdf,
        record->salt,
        record->salt_size,
        derived,
        record->verifier_size);
    if (!derived_ok) {
        secure_zero(derived, sizeof(derived));
        return false;
    }

    *out_matches = aurora_identity_constant_time_equal(
        derived,
        record->verifier,
        record->verifier_size);
    secure_zero(derived, sizeof(derived));
    return true;
}

struct aurora_identity_crypto_ops aurora_identity_argon2id_provider_crypto_ops(
    struct aurora_identity_argon2id_provider *provider) {
    struct aurora_identity_crypto_ops ops;

    memset(&ops, 0, sizeof(ops));
    ops.context = provider;
    ops.derive_lookup_tag = aurora_identity_argon2id_provider_derive_lookup_tag;
    ops.derive_key_verifier =
        aurora_identity_argon2id_provider_derive_key_verifier;
    ops.verify_key = aurora_identity_argon2id_provider_verify_key;
    return ops;
}
