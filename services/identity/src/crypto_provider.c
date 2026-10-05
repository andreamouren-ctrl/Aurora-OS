#include "aurora/identity/crypto_provider.h"

#include <string.h>

static const uint8_t lookup_domain[] = "AURORA.IDENTITY.LOOKUP.V1";
static const uint8_t session_grant_domain[] = "AURORA.IDENTITY.SESSION-GRANT.V1";

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

static bool derive_domain_tag(
    const uint8_t key[AURORA_IDENTITY_PROVIDER_KEY_SIZE],
    const uint8_t *domain,
    size_t domain_size,
    const uint8_t *payload,
    size_t payload_size,
    uint8_t out_tag[AURORA_IDENTITY_HMAC_SHA256_SIZE]) {
    uint8_t message[96u];
    size_t message_size;

    if (key == NULL || domain == NULL || payload == NULL || out_tag == NULL ||
        domain_size == 0u || domain_size + 1u + payload_size > sizeof(message)) {
        return false;
    }

    memcpy(message, domain, domain_size);
    message[domain_size] = 0u;
    memcpy(message + domain_size + 1u, payload, payload_size);
    message_size = domain_size + 1u + payload_size;

    if (!aurora_identity_hmac_sha256(
            key,
            AURORA_IDENTITY_PROVIDER_KEY_SIZE,
            message,
            message_size,
            out_tag)) {
        secure_zero(message, sizeof(message));
        return false;
    }

    secure_zero(message, sizeof(message));
    return true;
}

bool aurora_identity_hmac_provider_init(
    struct aurora_identity_hmac_provider *provider,
    const uint8_t lookup_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE],
    const uint8_t session_grant_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE],
    struct aurora_identity_hmac_drbg *drbg) {
    if (provider == NULL || lookup_key == NULL || session_grant_key == NULL ||
        drbg == NULL || !drbg->instantiated) {
        return false;
    }

    memset(provider, 0, sizeof(*provider));
    memcpy(provider->lookup_key, lookup_key, sizeof(provider->lookup_key));
    memcpy(
        provider->session_grant_key,
        session_grant_key,
        sizeof(provider->session_grant_key));
    provider->drbg = drbg;
    provider->initialized = true;
    return true;
}

void aurora_identity_hmac_provider_clear(
    struct aurora_identity_hmac_provider *provider) {
    if (provider != NULL) {
        secure_zero(provider, sizeof(*provider));
    }
}

bool aurora_identity_hmac_provider_derive_lookup_tag(
    void *context,
    const char *normalized_key,
    size_t normalized_key_length,
    uint8_t out_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE]) {
    struct aurora_identity_hmac_provider *provider =
        (struct aurora_identity_hmac_provider *)context;

    if (provider == NULL || !provider->initialized || normalized_key == NULL ||
        normalized_key_length == 0u || normalized_key_length > AURORA_IDENTITY_KEY_MAX_LEN ||
        out_tag == NULL) {
        return false;
    }

    return derive_domain_tag(
        provider->lookup_key,
        lookup_domain,
        sizeof(lookup_domain) - 1u,
        (const uint8_t *)normalized_key,
        normalized_key_length,
        out_tag);
}

bool aurora_identity_hmac_provider_derive_session_grant_tag(
    void *context,
    const uint8_t token[AURORA_IDENTITY_SESSION_GRANT_TOKEN_SIZE],
    uint8_t out_tag[AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE]) {
    struct aurora_identity_hmac_provider *provider =
        (struct aurora_identity_hmac_provider *)context;

    if (provider == NULL || !provider->initialized || token == NULL || out_tag == NULL) {
        return false;
    }

    return derive_domain_tag(
        provider->session_grant_key,
        session_grant_domain,
        sizeof(session_grant_domain) - 1u,
        token,
        AURORA_IDENTITY_SESSION_GRANT_TOKEN_SIZE,
        out_tag);
}

bool aurora_identity_hmac_provider_fill_random(
    void *context,
    uint8_t *buffer,
    size_t size) {
    struct aurora_identity_hmac_provider *provider =
        (struct aurora_identity_hmac_provider *)context;

    if (provider == NULL || !provider->initialized || provider->drbg == NULL ||
        buffer == NULL || size == 0u) {
        return false;
    }

    return aurora_identity_hmac_drbg_generate(
        provider->drbg,
        buffer,
        size,
        NULL,
        0u);
}

struct aurora_identity_random_ops aurora_identity_hmac_provider_random_ops(
    struct aurora_identity_hmac_provider *provider) {
    struct aurora_identity_random_ops ops;

    memset(&ops, 0, sizeof(ops));
    ops.context = provider;
    ops.fill_random = aurora_identity_hmac_provider_fill_random;
    return ops;
}

struct aurora_identity_session_grant_crypto_ops
    aurora_identity_hmac_provider_session_grant_crypto_ops(
        struct aurora_identity_hmac_provider *provider) {
    struct aurora_identity_session_grant_crypto_ops ops;

    memset(&ops, 0, sizeof(ops));
    ops.context = provider;
    ops.derive_token_tag = aurora_identity_hmac_provider_derive_session_grant_tag;
    return ops;
}
