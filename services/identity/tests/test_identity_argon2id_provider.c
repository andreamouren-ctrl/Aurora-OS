#include "aurora/identity/argon2id_provider.h"
#include "aurora/identity/crypto_foundation.h"
#include "aurora/identity/crypto_provider.h"

#include "argon2.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                      \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                                 \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static void fill_sequence(uint8_t *buffer, size_t size, uint8_t value) {
    size_t index;

    for (index = 0u; index < size; ++index) {
        buffer[index] = value;
    }
}

static void test_rfc9106_argon2id_vector(void) {
    uint8_t password[32];
    uint8_t salt[16];
    uint8_t secret[8];
    uint8_t associated_data[12];
    uint8_t output[32];
    const uint8_t expected[32] = {
        0x0d, 0x64, 0x0d, 0xf5, 0x8d, 0x78, 0x76, 0x6c,
        0x08, 0xc0, 0x37, 0xa3, 0x4a, 0x8b, 0x53, 0xc9,
        0xd0, 0x1e, 0xf0, 0x45, 0x2d, 0x75, 0xb6, 0x5e,
        0xb5, 0x25, 0x20, 0xe9, 0x6b, 0x01, 0xe6, 0x59
    };
    argon2_context context;
    int result;

    fill_sequence(password, sizeof(password), 0x01u);
    fill_sequence(salt, sizeof(salt), 0x02u);
    fill_sequence(secret, sizeof(secret), 0x03u);
    fill_sequence(associated_data, sizeof(associated_data), 0x04u);
    memset(output, 0, sizeof(output));
    memset(&context, 0, sizeof(context));

    context.out = output;
    context.outlen = (uint32_t)sizeof(output);
    context.pwd = password;
    context.pwdlen = (uint32_t)sizeof(password);
    context.salt = salt;
    context.saltlen = (uint32_t)sizeof(salt);
    context.secret = secret;
    context.secretlen = (uint32_t)sizeof(secret);
    context.ad = associated_data;
    context.adlen = (uint32_t)sizeof(associated_data);
    context.t_cost = 3u;
    context.m_cost = 32u;
    context.lanes = 4u;
    context.threads = 4u;
    context.version = ARGON2_VERSION_13;
    context.flags = ARGON2_DEFAULT_FLAGS;

    result = argon2_ctx(&context, Argon2_id);
    CHECK(result == ARGON2_OK);
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);
}

static bool initialize_test_hmac_provider(
    struct aurora_identity_hmac_provider *provider,
    struct aurora_identity_hmac_drbg *drbg) {
    uint8_t entropy[32];
    uint8_t nonce[16];
    uint8_t lookup_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE];
    uint8_t grant_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE];
    uint8_t reauth_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE];

    fill_sequence(entropy, sizeof(entropy), 0x11u);
    fill_sequence(nonce, sizeof(nonce), 0x22u);
    fill_sequence(lookup_key, sizeof(lookup_key), 0x33u);
    fill_sequence(grant_key, sizeof(grant_key), 0x44u);
    fill_sequence(reauth_key, sizeof(reauth_key), 0x55u);

    if (!aurora_identity_hmac_drbg_instantiate(
            drbg,
            entropy,
            sizeof(entropy),
            nonce,
            sizeof(nonce),
            NULL,
            0u)) {
        return false;
    }

    return aurora_identity_hmac_provider_init(
        provider,
        lookup_key,
        grant_key,
        reauth_key,
        drbg);
}

static struct aurora_identity_argon2id_limits test_limits(void) {
    struct aurora_identity_argon2id_limits limits;

    memset(&limits, 0, sizeof(limits));
    limits.minimum_memory_kib = 8u;
    limits.maximum_memory_kib = 65536u;
    limits.minimum_time_cost = 1u;
    limits.maximum_time_cost = 8u;
    limits.minimum_parallelism = 1u;
    limits.maximum_parallelism = 8u;
    limits.minimum_salt_size = 8u;
    limits.maximum_salt_size = AURORA_IDENTITY_SALT_MAX_SIZE;
    limits.minimum_verifier_size = 16u;
    limits.maximum_verifier_size = AURORA_IDENTITY_VERIFIER_MAX_SIZE;
    return limits;
}

static void test_provider_derivation_and_verification(void) {
    static const char normalized_key[] = "AURORAKEY1234";
    static const char wrong_key[] = "AURORAKEY9999";
    uint8_t salt[16];
    uint8_t expected[32];
    uint8_t actual[32];
    struct aurora_identity_hmac_drbg drbg;
    struct aurora_identity_hmac_provider hmac_provider;
    struct aurora_identity_argon2id_provider provider;
    struct aurora_identity_argon2id_limits limits = test_limits();
    struct aurora_identity_kdf_params kdf;
    struct aurora_identity_key_record record;
    struct aurora_identity_crypto_ops ops;
    bool matches;
    int reference_result;

    memset(&drbg, 0, sizeof(drbg));
    memset(&hmac_provider, 0, sizeof(hmac_provider));
    memset(&provider, 0, sizeof(provider));
    memset(&kdf, 0, sizeof(kdf));
    memset(&record, 0, sizeof(record));
    memset(expected, 0, sizeof(expected));
    memset(actual, 0, sizeof(actual));
    fill_sequence(salt, sizeof(salt), 0x5au);

    CHECK(initialize_test_hmac_provider(&hmac_provider, &drbg));
    CHECK(aurora_identity_argon2id_provider_init(
        &provider,
        &hmac_provider,
        &limits));

    kdf.algorithm = AURORA_IDENTITY_KDF_ARGON2ID;
    kdf.parameters_version = AURORA_IDENTITY_ARGON2ID_PARAMETERS_VERSION_1;
    kdf.memory_kib = 32u;
    kdf.time_cost = 3u;
    kdf.parallelism = 4u;

    reference_result = argon2id_hash_raw(
        kdf.time_cost,
        kdf.memory_kib,
        kdf.parallelism,
        normalized_key,
        sizeof(normalized_key) - 1u,
        salt,
        sizeof(salt),
        expected,
        sizeof(expected));
    CHECK(reference_result == ARGON2_OK);

    CHECK(aurora_identity_argon2id_provider_derive_key_verifier(
        &provider,
        normalized_key,
        sizeof(normalized_key) - 1u,
        &kdf,
        salt,
        sizeof(salt),
        actual,
        sizeof(actual)));
    CHECK(memcmp(actual, expected, sizeof(actual)) == 0);

    record.kdf = kdf;
    memcpy(record.salt, salt, sizeof(salt));
    record.salt_size = sizeof(salt);
    memcpy(record.verifier, actual, sizeof(actual));
    record.verifier_size = sizeof(actual);

    matches = false;
    CHECK(aurora_identity_argon2id_provider_verify_key(
        &provider,
        normalized_key,
        sizeof(normalized_key) - 1u,
        &record,
        &matches));
    CHECK(matches);

    matches = true;
    CHECK(aurora_identity_argon2id_provider_verify_key(
        &provider,
        wrong_key,
        sizeof(wrong_key) - 1u,
        &record,
        &matches));
    CHECK(!matches);

    ops = aurora_identity_argon2id_provider_crypto_ops(&provider);
    CHECK(ops.context == &provider);
    CHECK(ops.derive_lookup_tag != NULL);
    CHECK(ops.derive_key_verifier != NULL);
    CHECK(ops.verify_key != NULL);

    aurora_identity_argon2id_provider_clear(&provider);
    CHECK(!provider.initialized);
    CHECK(provider.hmac_provider == NULL);
    aurora_identity_hmac_provider_clear(&hmac_provider);
    aurora_identity_hmac_drbg_clear(&drbg);
}

static void test_provider_rejects_out_of_policy_kdf(void) {
    static const char normalized_key[] = "AURORAKEY1234";
    uint8_t salt[16];
    uint8_t output[32];
    struct aurora_identity_hmac_drbg drbg;
    struct aurora_identity_hmac_provider hmac_provider;
    struct aurora_identity_argon2id_provider provider;
    struct aurora_identity_argon2id_limits limits = test_limits();
    struct aurora_identity_kdf_params kdf;

    memset(&drbg, 0, sizeof(drbg));
    memset(&hmac_provider, 0, sizeof(hmac_provider));
    memset(&provider, 0, sizeof(provider));
    memset(&kdf, 0, sizeof(kdf));
    fill_sequence(salt, sizeof(salt), 0x6bu);
    memset(output, 0xaa, sizeof(output));

    CHECK(initialize_test_hmac_provider(&hmac_provider, &drbg));
    CHECK(aurora_identity_argon2id_provider_init(
        &provider,
        &hmac_provider,
        &limits));

    kdf.algorithm = AURORA_IDENTITY_KDF_ARGON2ID;
    kdf.parameters_version = AURORA_IDENTITY_ARGON2ID_PARAMETERS_VERSION_1;
    kdf.memory_kib = 32u;
    kdf.time_cost = 2u;
    kdf.parallelism = 2u;

    kdf.parameters_version = 2u;
    CHECK(!aurora_identity_argon2id_provider_derive_key_verifier(
        &provider,
        normalized_key,
        sizeof(normalized_key) - 1u,
        &kdf,
        salt,
        sizeof(salt),
        output,
        sizeof(output)));

    kdf.parameters_version = AURORA_IDENTITY_ARGON2ID_PARAMETERS_VERSION_1;
    kdf.memory_kib = limits.maximum_memory_kib + 1u;
    CHECK(!aurora_identity_argon2id_provider_derive_key_verifier(
        &provider,
        normalized_key,
        sizeof(normalized_key) - 1u,
        &kdf,
        salt,
        sizeof(salt),
        output,
        sizeof(output)));

    kdf.memory_kib = 32u;
    kdf.time_cost = limits.maximum_time_cost + 1u;
    CHECK(!aurora_identity_argon2id_provider_derive_key_verifier(
        &provider,
        normalized_key,
        sizeof(normalized_key) - 1u,
        &kdf,
        salt,
        sizeof(salt),
        output,
        sizeof(output)));

    kdf.time_cost = 2u;
    kdf.parallelism = limits.maximum_parallelism + 1u;
    CHECK(!aurora_identity_argon2id_provider_derive_key_verifier(
        &provider,
        normalized_key,
        sizeof(normalized_key) - 1u,
        &kdf,
        salt,
        sizeof(salt),
        output,
        sizeof(output)));

    kdf.parallelism = 8u;
    kdf.memory_kib = 32u;
    CHECK(!aurora_identity_argon2id_provider_derive_key_verifier(
        &provider,
        normalized_key,
        sizeof(normalized_key) - 1u,
        &kdf,
        salt,
        sizeof(salt),
        output,
        sizeof(output)));

    aurora_identity_argon2id_provider_clear(&provider);
    aurora_identity_hmac_provider_clear(&hmac_provider);
    aurora_identity_hmac_drbg_clear(&drbg);
}

static void test_invalid_limits_are_rejected(void) {
    struct aurora_identity_hmac_drbg drbg;
    struct aurora_identity_hmac_provider hmac_provider;
    struct aurora_identity_argon2id_provider provider;
    struct aurora_identity_argon2id_limits limits = test_limits();

    memset(&drbg, 0, sizeof(drbg));
    memset(&hmac_provider, 0, sizeof(hmac_provider));
    memset(&provider, 0, sizeof(provider));

    CHECK(initialize_test_hmac_provider(&hmac_provider, &drbg));

    limits.maximum_memory_kib = limits.minimum_memory_kib - 1u;
    CHECK(!aurora_identity_argon2id_provider_init(
        &provider,
        &hmac_provider,
        &limits));

    limits = test_limits();
    limits.minimum_salt_size = 7u;
    CHECK(!aurora_identity_argon2id_provider_init(
        &provider,
        &hmac_provider,
        &limits));

    aurora_identity_hmac_provider_clear(&hmac_provider);
    aurora_identity_hmac_drbg_clear(&drbg);
}

int main(void) {
    test_rfc9106_argon2id_vector();
    test_provider_derivation_and_verification();
    test_provider_rejects_out_of_policy_kdf();
    test_invalid_limits_are_rejected();

    if (failures != 0) {
        fprintf(stderr, "Aurora Identity Argon2id tests: %d failure(s)\n", failures);
        return 1;
    }

    puts("Aurora Identity Argon2id tests: PASS");
    return 0;
}
