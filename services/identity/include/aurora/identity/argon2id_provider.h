#ifndef AURORA_IDENTITY_ARGON2ID_PROVIDER_H
#define AURORA_IDENTITY_ARGON2ID_PROVIDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aurora/identity/core.h"
#include "aurora/identity/crypto_provider.h"

#define AURORA_IDENTITY_ARGON2ID_PARAMETERS_VERSION_1 1u
#define AURORA_IDENTITY_ARGON2_VERSION_13 0x13u

struct aurora_identity_argon2id_limits {
    uint32_t minimum_memory_kib;
    uint32_t maximum_memory_kib;
    uint32_t minimum_time_cost;
    uint32_t maximum_time_cost;
    uint32_t minimum_parallelism;
    uint32_t maximum_parallelism;
    size_t minimum_salt_size;
    size_t maximum_salt_size;
    size_t minimum_verifier_size;
    size_t maximum_verifier_size;
};

struct aurora_identity_argon2id_provider {
    struct aurora_identity_hmac_provider *hmac_provider;
    struct aurora_identity_argon2id_limits limits;
    bool initialized;
};

/*
 * Initializes an Argon2id-backed Identity crypto provider. The caller owns the
 * HMAC provider and must keep it alive for the lifetime of this object.
 *
 * The limits are a denial-of-service boundary as well as a policy boundary:
 * stored KDF metadata is never trusted to request arbitrary RAM/CPU. The
 * active creation policy must fit inside these bounds.
 */
bool aurora_identity_argon2id_provider_init(
    struct aurora_identity_argon2id_provider *provider,
    struct aurora_identity_hmac_provider *hmac_provider,
    const struct aurora_identity_argon2id_limits *limits);

void aurora_identity_argon2id_provider_clear(
    struct aurora_identity_argon2id_provider *provider);

bool aurora_identity_argon2id_provider_derive_lookup_tag(
    void *context,
    const char *normalized_key,
    size_t normalized_key_length,
    uint8_t out_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE]);

bool aurora_identity_argon2id_provider_derive_key_verifier(
    void *context,
    const char *normalized_key,
    size_t normalized_key_length,
    const struct aurora_identity_kdf_params *kdf,
    const uint8_t *salt,
    size_t salt_size,
    uint8_t *out_verifier,
    size_t verifier_size);

bool aurora_identity_argon2id_provider_verify_key(
    void *context,
    const char *normalized_key,
    size_t normalized_key_length,
    const struct aurora_identity_key_record *record,
    bool *out_matches);

struct aurora_identity_crypto_ops aurora_identity_argon2id_provider_crypto_ops(
    struct aurora_identity_argon2id_provider *provider);

#endif
