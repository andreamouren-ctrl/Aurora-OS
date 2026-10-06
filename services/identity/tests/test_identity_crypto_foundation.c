#include "aurora/identity/crypto_foundation.h"
#include "aurora/identity/crypto_provider.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (0)

static bool test_sha256_vectors(void) {
    static const uint8_t empty_digest[32] = {
        0xe3u, 0xb0u, 0xc4u, 0x42u, 0x98u, 0xfcu, 0x1cu, 0x14u,
        0x9au, 0xfbu, 0xf4u, 0xc8u, 0x99u, 0x6fu, 0xb9u, 0x24u,
        0x27u, 0xaeu, 0x41u, 0xe4u, 0x64u, 0x9bu, 0x93u, 0x4cu,
        0xa4u, 0x95u, 0x99u, 0x1bu, 0x78u, 0x52u, 0xb8u, 0x55u
    };
    static const uint8_t abc[] = {'a', 'b', 'c'};
    static const uint8_t abc_digest[32] = {
        0xbau, 0x78u, 0x16u, 0xbfu, 0x8fu, 0x01u, 0xcfu, 0xeau,
        0x41u, 0x41u, 0x40u, 0xdeu, 0x5du, 0xaeu, 0x22u, 0x23u,
        0xb0u, 0x03u, 0x61u, 0xa3u, 0x96u, 0x17u, 0x7au, 0x9cu,
        0xb4u, 0x10u, 0xffu, 0x61u, 0xf2u, 0x00u, 0x15u, 0xadu
    };
    uint8_t digest[32];

    CHECK(aurora_identity_sha256(NULL, 0u, digest));
    CHECK(memcmp(digest, empty_digest, sizeof(digest)) == 0);
    CHECK(aurora_identity_sha256(abc, sizeof(abc), digest));
    CHECK(memcmp(digest, abc_digest, sizeof(digest)) == 0);
    return true;
}

static bool test_hmac_vectors(void) {
    uint8_t key1[20];
    static const uint8_t data1[] = {'H','i',' ','T','h','e','r','e'};
    static const uint8_t expected1[32] = {
        0xb0u, 0x34u, 0x4cu, 0x61u, 0xd8u, 0xdbu, 0x38u, 0x53u,
        0x5cu, 0xa8u, 0xafu, 0xceu, 0xafu, 0x0bu, 0xf1u, 0x2bu,
        0x88u, 0x1du, 0xc2u, 0x00u, 0xc9u, 0x83u, 0x3du, 0xa7u,
        0x26u, 0xe9u, 0x37u, 0x6cu, 0x2eu, 0x32u, 0xcfu, 0xf7u
    };
    static const uint8_t key2[] = {'J','e','f','e'};
    static const uint8_t data2[] = {
        'w','h','a','t',' ','d','o',' ','y','a',' ','w','a','n','t',' ',
        'f','o','r',' ','n','o','t','h','i','n','g','?'
    };
    static const uint8_t expected2[32] = {
        0x5bu, 0xdcu, 0xc1u, 0x46u, 0xbfu, 0x60u, 0x75u, 0x4eu,
        0x6au, 0x04u, 0x24u, 0x26u, 0x08u, 0x95u, 0x75u, 0xc7u,
        0x5au, 0x00u, 0x3fu, 0x08u, 0x9du, 0x27u, 0x39u, 0x83u,
        0x9du, 0xecu, 0x58u, 0xb9u, 0x64u, 0xecu, 0x38u, 0x43u
    };
    uint8_t mac[32];

    memset(key1, 0x0b, sizeof(key1));
    CHECK(aurora_identity_hmac_sha256(key1, sizeof(key1), data1, sizeof(data1), mac));
    CHECK(aurora_identity_constant_time_equal(mac, expected1, sizeof(mac)));
    CHECK(aurora_identity_hmac_sha256(key2, sizeof(key2), data2, sizeof(data2), mac));
    CHECK(aurora_identity_constant_time_equal(mac, expected2, sizeof(mac)));
    CHECK(!aurora_identity_constant_time_equal(expected1, expected2, sizeof(expected1)));
    return true;
}

static bool test_hmac_drbg_known_answer(void) {
    struct aurora_identity_hmac_drbg drbg;
    uint8_t entropy[32];
    uint8_t nonce[16];
    static const uint8_t personalization[] = "AuroraIdentityTest";
    static const uint8_t expected[64] = {
        0x43u,0xcfu,0x14u,0x6du,0xc9u,0xbau,0x4bu,0xf9u,
        0x82u,0xc9u,0xc1u,0xd3u,0x18u,0xa2u,0x28u,0x3fu,
        0xbbu,0xcfu,0x25u,0x71u,0xc6u,0x04u,0x7au,0x22u,
        0x47u,0x42u,0xc3u,0x29u,0x31u,0x1eu,0x15u,0x8bu,
        0x2du,0xadu,0x5cu,0x2eu,0xe8u,0xa9u,0x95u,0x02u,
        0xd9u,0xe3u,0xdfu,0x36u,0x69u,0x12u,0xd4u,0x28u,
        0x81u,0x8eu,0x42u,0xe0u,0x39u,0xd0u,0xc3u,0x5eu,
        0xe8u,0xb6u,0x30u,0xd4u,0x95u,0x5au,0xa1u,0xf6u
    };
    uint8_t output[64];
    size_t i;

    for (i = 0u; i < sizeof(entropy); ++i) {
        entropy[i] = (uint8_t)i;
    }
    for (i = 0u; i < sizeof(nonce); ++i) {
        nonce[i] = (uint8_t)(32u + i);
    }

    CHECK(aurora_identity_hmac_drbg_instantiate(
        &drbg,
        entropy,
        sizeof(entropy),
        nonce,
        sizeof(nonce),
        personalization,
        sizeof(personalization) - 1u));
    CHECK(drbg.instantiated);
    CHECK(drbg.reseed_counter == 1u);
    CHECK(aurora_identity_hmac_drbg_generate(&drbg, output, sizeof(output), NULL, 0u));
    CHECK(memcmp(output, expected, sizeof(output)) == 0);
    CHECK(drbg.reseed_counter == 2u);

    aurora_identity_hmac_drbg_clear(&drbg);
    CHECK(!drbg.instantiated);
    CHECK(!aurora_identity_hmac_drbg_generate(&drbg, output, sizeof(output), NULL, 0u));
    return true;
}

static bool test_provider_domains_and_random(void) {
    struct aurora_identity_hmac_drbg drbg;
    struct aurora_identity_hmac_provider provider;
    struct aurora_identity_random_ops random_ops;
    struct aurora_identity_session_grant_crypto_ops grant_ops;
    uint8_t entropy[32];
    uint8_t nonce[16];
    uint8_t lookup_key[32];
    uint8_t session_key[32];
    static const char normalized_key[] = "AUR7K4PN9Q2XM6D";
    uint8_t token[AURORA_IDENTITY_SESSION_GRANT_TOKEN_SIZE];
    static const uint8_t expected_lookup[32] = {
        0x24u,0xbau,0xe6u,0x8du,0x04u,0x28u,0x4cu,0x99u,
        0xd0u,0x03u,0x14u,0xafu,0x75u,0x5eu,0xebu,0xe5u,
        0xceu,0xd9u,0xe3u,0x6cu,0xd3u,0x71u,0x7au,0x31u,
        0x79u,0x60u,0xafu,0xb2u,0x33u,0xd2u,0x49u,0x63u
    };
    static const uint8_t expected_session[32] = {
        0x03u,0x18u,0x47u,0x6au,0xcbu,0x8fu,0x52u,0xa6u,
        0x2du,0xe9u,0x39u,0x0au,0x79u,0x16u,0xf5u,0x20u,
        0x3fu,0x1au,0x5au,0x7eu,0x1cu,0x59u,0xf0u,0xb0u,
        0x1eu,0x0du,0x95u,0x0fu,0xb4u,0x3du,0x56u,0x19u
    };
    uint8_t lookup_tag[32];
    uint8_t grant_tag[32];
    uint8_t random_bytes[32];
    size_t i;

    for (i = 0u; i < 32u; ++i) {
        entropy[i] = (uint8_t)(0xa0u + i);
        lookup_key[i] = (uint8_t)i;
        session_key[i] = (uint8_t)(32u + i);
        token[i] = (uint8_t)i;
    }
    for (i = 0u; i < 16u; ++i) {
        nonce[i] = (uint8_t)(0x40u + i);
    }

    CHECK(aurora_identity_hmac_drbg_instantiate(
        &drbg, entropy, sizeof(entropy), nonce, sizeof(nonce), NULL, 0u));
    CHECK(aurora_identity_hmac_provider_init(
        &provider, lookup_key, session_key, &drbg));

    CHECK(aurora_identity_hmac_provider_derive_lookup_tag(
        &provider,
        normalized_key,
        sizeof(normalized_key) - 1u,
        lookup_tag));
    CHECK(memcmp(lookup_tag, expected_lookup, sizeof(lookup_tag)) == 0);

    grant_ops = aurora_identity_hmac_provider_session_grant_crypto_ops(&provider);
    CHECK(grant_ops.derive_token_tag(
        grant_ops.context,
        token,
        grant_tag));
    CHECK(memcmp(grant_tag, expected_session, sizeof(grant_tag)) == 0);
    CHECK(memcmp(lookup_tag, grant_tag, sizeof(lookup_tag)) != 0);

    random_ops = aurora_identity_hmac_provider_random_ops(&provider);
    CHECK(random_ops.fill_random(random_ops.context, random_bytes, sizeof(random_bytes)));
    CHECK(memcmp(random_bytes, token, sizeof(random_bytes)) != 0);

    aurora_identity_hmac_provider_clear(&provider);
    CHECK(!provider.initialized);
    aurora_identity_hmac_drbg_clear(&drbg);
    return true;
}

static bool test_provider_entropy_degraded_mode(void) {
    struct aurora_identity_hmac_provider provider;
    struct aurora_identity_random_ops random_ops;
    uint8_t lookup_key[32];
    uint8_t session_key[32];
    uint8_t lookup_tag[32];
    uint8_t random_bytes[16];
    static const char normalized_key[] = "AUR7K4PN9Q2XM6D";
    size_t i;

    for (i = 0u; i < sizeof(lookup_key); ++i) {
        lookup_key[i] = (uint8_t)i;
        session_key[i] = (uint8_t)(32u + i);
    }
    memset(&provider, 0, sizeof(provider));
    memset(lookup_tag, 0, sizeof(lookup_tag));
    memset(random_bytes, 0, sizeof(random_bytes));

    CHECK(aurora_identity_hmac_provider_init(
        &provider, lookup_key, session_key, NULL));
    CHECK(provider.initialized);
    CHECK(provider.drbg == NULL);
    CHECK(aurora_identity_hmac_provider_derive_lookup_tag(
        &provider,
        normalized_key,
        sizeof(normalized_key) - 1u,
        lookup_tag));

    random_ops = aurora_identity_hmac_provider_random_ops(&provider);
    CHECK(!random_ops.fill_random(
        random_ops.context,
        random_bytes,
        sizeof(random_bytes)));

    aurora_identity_hmac_provider_clear(&provider);
    CHECK(!provider.initialized);
    return true;
}

int main(void) {
    if (!test_sha256_vectors() ||
        !test_hmac_vectors() ||
        !test_hmac_drbg_known_answer() ||
        !test_provider_domains_and_random() ||
        !test_provider_entropy_degraded_mode()) {
        return 1;
    }

    puts("Aurora Identity crypto-foundation tests: PASS");
    return 0;
}
