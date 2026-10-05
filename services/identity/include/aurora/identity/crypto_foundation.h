#ifndef AURORA_IDENTITY_CRYPTO_FOUNDATION_H
#define AURORA_IDENTITY_CRYPTO_FOUNDATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURORA_IDENTITY_SHA256_SIZE 32u
#define AURORA_IDENTITY_HMAC_SHA256_SIZE 32u
#define AURORA_IDENTITY_HMAC_DRBG_MIN_ENTROPY_SIZE 32u
#define AURORA_IDENTITY_HMAC_DRBG_MIN_NONCE_SIZE 16u
#define AURORA_IDENTITY_HMAC_DRBG_MAX_ENTROPY_SIZE 64u
#define AURORA_IDENTITY_HMAC_DRBG_MAX_NONCE_SIZE 32u
#define AURORA_IDENTITY_HMAC_DRBG_MAX_PERSONALIZATION_SIZE 64u
#define AURORA_IDENTITY_HMAC_DRBG_MAX_ADDITIONAL_SIZE 64u
#define AURORA_IDENTITY_HMAC_DRBG_MAX_REQUEST_SIZE 1024u
#define AURORA_IDENTITY_HMAC_DRBG_RESEED_INTERVAL UINT64_C(281474976710656)

struct aurora_identity_hmac_drbg {
    uint8_t key[AURORA_IDENTITY_HMAC_SHA256_SIZE];
    uint8_t value[AURORA_IDENTITY_HMAC_SHA256_SIZE];
    uint64_t reseed_counter;
    bool instantiated;
};

bool aurora_identity_sha256(
    const uint8_t *data,
    size_t data_size,
    uint8_t out_digest[AURORA_IDENTITY_SHA256_SIZE]);

bool aurora_identity_hmac_sha256(
    const uint8_t *key,
    size_t key_size,
    const uint8_t *data,
    size_t data_size,
    uint8_t out_mac[AURORA_IDENTITY_HMAC_SHA256_SIZE]);

bool aurora_identity_constant_time_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size);

/*
 * HMAC-DRBG based on HMAC-SHA256. This object is not an entropy source.
 * Production callers must instantiate/reseed it with entropy supplied by a
 * separately reviewed Aurora entropy subsystem. Deterministic/test seeds must
 * never be used by the live Identity Service.
 */
bool aurora_identity_hmac_drbg_instantiate(
    struct aurora_identity_hmac_drbg *drbg,
    const uint8_t *entropy,
    size_t entropy_size,
    const uint8_t *nonce,
    size_t nonce_size,
    const uint8_t *personalization,
    size_t personalization_size);

bool aurora_identity_hmac_drbg_reseed(
    struct aurora_identity_hmac_drbg *drbg,
    const uint8_t *entropy,
    size_t entropy_size,
    const uint8_t *additional,
    size_t additional_size);

bool aurora_identity_hmac_drbg_generate(
    struct aurora_identity_hmac_drbg *drbg,
    uint8_t *out,
    size_t out_size,
    const uint8_t *additional,
    size_t additional_size);

void aurora_identity_hmac_drbg_clear(struct aurora_identity_hmac_drbg *drbg);

#endif
