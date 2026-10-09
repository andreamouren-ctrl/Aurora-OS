#ifndef AURORA_IDENTITY_CRYPTO_PROVIDER_H
#define AURORA_IDENTITY_CRYPTO_PROVIDER_H

#include "aurora/identity/core.h"
#include "aurora/identity/crypto_foundation.h"
#include "aurora/identity/session_grant.h"
#include "aurora/identity/reauth_proof.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURORA_IDENTITY_PROVIDER_KEY_SIZE 32u

struct aurora_identity_hmac_provider {
    uint8_t lookup_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE];
    uint8_t session_grant_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE];
    uint8_t reauth_proof_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE];
    struct aurora_identity_hmac_drbg *drbg;
    bool initialized;
};

/*
 * The provider keys must come from protected system state. In particular the
 * lookup key must remain stable across reboot or existing Aurora Key lookup
 * tags would no longer be reproducible. This API never fabricates those keys.
 *
 * drbg may be NULL when the platform currently has no qualified fresh entropy.
 * Keyed lookup/session-tag operations remain available in that degraded mode;
 * every operation that requires fresh randomness must fail closed until an
 * instantiated DRBG is attached by a later provider initialization.
 */
bool aurora_identity_hmac_provider_init(
    struct aurora_identity_hmac_provider *provider,
    const uint8_t lookup_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE],
    const uint8_t session_grant_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE],
    const uint8_t reauth_proof_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE],
    struct aurora_identity_hmac_drbg *drbg);

void aurora_identity_hmac_provider_clear(
    struct aurora_identity_hmac_provider *provider);

bool aurora_identity_hmac_provider_derive_lookup_tag(
    void *context,
    const char *normalized_key,
    size_t normalized_key_length,
    uint8_t out_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE]);

bool aurora_identity_hmac_provider_derive_session_grant_tag(
    void *context,
    const uint8_t token[AURORA_IDENTITY_SESSION_GRANT_TOKEN_SIZE],
    uint8_t out_tag[AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE]);

bool aurora_identity_hmac_provider_derive_reauth_proof_tag(
    void *context,
    const uint8_t token[AURORA_IDENTITY_REAUTH_PROOF_TOKEN_SIZE],
    uint8_t out_tag[AURORA_IDENTITY_REAUTH_PROOF_TAG_SIZE]);

bool aurora_identity_hmac_provider_fill_random(
    void *context,
    uint8_t *buffer,
    size_t size);

struct aurora_identity_random_ops aurora_identity_hmac_provider_random_ops(
    struct aurora_identity_hmac_provider *provider);

struct aurora_identity_session_grant_crypto_ops
    aurora_identity_hmac_provider_session_grant_crypto_ops(
        struct aurora_identity_hmac_provider *provider);

struct aurora_identity_reauth_crypto_ops
    aurora_identity_hmac_provider_reauth_crypto_ops(
        struct aurora_identity_hmac_provider *provider);

#endif
