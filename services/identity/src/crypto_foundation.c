#include "aurora/identity/crypto_foundation.h"

#include <limits.h>
#include <string.h>

#define SHA256_BLOCK_SIZE 64u

struct sha256_context {
    uint32_t state[8];
    uint64_t total_size;
    uint8_t block[SHA256_BLOCK_SIZE];
    size_t block_size;
};

static const uint32_t sha256_constants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
};

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

static uint32_t rotate_right(uint32_t value, uint32_t amount) {
    return (value >> amount) | (value << (32u - amount));
}

static uint32_t load_be32(const uint8_t *input) {
    return ((uint32_t)input[0] << 24u) |
           ((uint32_t)input[1] << 16u) |
           ((uint32_t)input[2] << 8u) |
           (uint32_t)input[3];
}

static void store_be32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)(value >> 24u);
    output[1] = (uint8_t)(value >> 16u);
    output[2] = (uint8_t)(value >> 8u);
    output[3] = (uint8_t)value;
}

static void store_be64(uint8_t *output, uint64_t value) {
    uint32_t index;

    for (index = 0u; index < 8u; ++index) {
        output[7u - index] = (uint8_t)(value >> (index * 8u));
    }
}

static void sha256_transform(struct sha256_context *context, const uint8_t block[SHA256_BLOCK_SIZE]) {
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t index;

    for (index = 0u; index < 16u; ++index) {
        words[index] = load_be32(block + (size_t)index * 4u);
    }

    for (index = 16u; index < 64u; ++index) {
        uint32_t s0 = rotate_right(words[index - 15u], 7u) ^
                      rotate_right(words[index - 15u], 18u) ^
                      (words[index - 15u] >> 3u);
        uint32_t s1 = rotate_right(words[index - 2u], 17u) ^
                      rotate_right(words[index - 2u], 19u) ^
                      (words[index - 2u] >> 10u);
        words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (index = 0u; index < 64u; ++index) {
        uint32_t sum1 = rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + sum1 + choose + sha256_constants[index] + words[index];
        uint32_t sum0 = rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;

    secure_zero(words, sizeof(words));
}

static void sha256_init(struct sha256_context *context) {
    memset(context, 0, sizeof(*context));
    context->state[0] = UINT32_C(0x6a09e667);
    context->state[1] = UINT32_C(0xbb67ae85);
    context->state[2] = UINT32_C(0x3c6ef372);
    context->state[3] = UINT32_C(0xa54ff53a);
    context->state[4] = UINT32_C(0x510e527f);
    context->state[5] = UINT32_C(0x9b05688c);
    context->state[6] = UINT32_C(0x1f83d9ab);
    context->state[7] = UINT32_C(0x5be0cd19);
}

static bool sha256_update(struct sha256_context *context, const uint8_t *data, size_t data_size) {
    size_t consumed = 0u;

    if (context == NULL || (data == NULL && data_size != 0u)) {
        return false;
    }

    if (data_size > UINT64_MAX - context->total_size) {
        return false;
    }

    context->total_size += (uint64_t)data_size;

    if (context->block_size != 0u) {
        size_t available = SHA256_BLOCK_SIZE - context->block_size;
        size_t take = data_size < available ? data_size : available;

        if (take != 0u) {
            memcpy(context->block + context->block_size, data, take);
            context->block_size += take;
            consumed += take;
        }

        if (context->block_size == SHA256_BLOCK_SIZE) {
            sha256_transform(context, context->block);
            context->block_size = 0u;
        }
    }

    while (data_size - consumed >= SHA256_BLOCK_SIZE) {
        sha256_transform(context, data + consumed);
        consumed += SHA256_BLOCK_SIZE;
    }

    if (consumed < data_size) {
        size_t remaining = data_size - consumed;
        memcpy(context->block, data + consumed, remaining);
        context->block_size = remaining;
    }

    return true;
}

static bool sha256_final(struct sha256_context *context, uint8_t out_digest[AURORA_IDENTITY_SHA256_SIZE]) {
    uint64_t bit_size;
    size_t index;

    if (context == NULL || out_digest == NULL || context->total_size > (UINT64_MAX >> 3u)) {
        return false;
    }

    bit_size = context->total_size << 3u;
    context->block[context->block_size++] = 0x80u;

    if (context->block_size > 56u) {
        memset(context->block + context->block_size, 0, SHA256_BLOCK_SIZE - context->block_size);
        sha256_transform(context, context->block);
        context->block_size = 0u;
    }

    memset(context->block + context->block_size, 0, 56u - context->block_size);
    store_be64(context->block + 56u, bit_size);
    sha256_transform(context, context->block);

    for (index = 0u; index < 8u; ++index) {
        store_be32(out_digest + index * 4u, context->state[index]);
    }

    secure_zero(context, sizeof(*context));
    return true;
}

bool aurora_identity_sha256(
    const uint8_t *data,
    size_t data_size,
    uint8_t out_digest[AURORA_IDENTITY_SHA256_SIZE]) {
    struct sha256_context context;

    if (out_digest == NULL || (data == NULL && data_size != 0u)) {
        return false;
    }

    sha256_init(&context);
    if (!sha256_update(&context, data, data_size) || !sha256_final(&context, out_digest)) {
        secure_zero(&context, sizeof(context));
        return false;
    }

    return true;
}

static bool hmac_sha256_parts(
    const uint8_t *key,
    size_t key_size,
    const uint8_t *part1,
    size_t part1_size,
    const uint8_t *part2,
    size_t part2_size,
    const uint8_t *part3,
    size_t part3_size,
    uint8_t out_mac[AURORA_IDENTITY_HMAC_SHA256_SIZE]) {
    uint8_t key_block[SHA256_BLOCK_SIZE];
    uint8_t inner_pad[SHA256_BLOCK_SIZE];
    uint8_t outer_pad[SHA256_BLOCK_SIZE];
    uint8_t inner_digest[AURORA_IDENTITY_SHA256_SIZE];
    uint8_t hashed_key[AURORA_IDENTITY_SHA256_SIZE];
    struct sha256_context context;
    size_t index;
    bool ok = false;

    if (out_mac == NULL || (key == NULL && key_size != 0u) ||
        (part1 == NULL && part1_size != 0u) ||
        (part2 == NULL && part2_size != 0u) ||
        (part3 == NULL && part3_size != 0u)) {
        return false;
    }

    memset(key_block, 0, sizeof(key_block));
    memset(hashed_key, 0, sizeof(hashed_key));

    if (key_size > SHA256_BLOCK_SIZE) {
        if (!aurora_identity_sha256(key, key_size, hashed_key)) {
            goto cleanup;
        }
        memcpy(key_block, hashed_key, sizeof(hashed_key));
    } else if (key_size != 0u) {
        memcpy(key_block, key, key_size);
    }

    for (index = 0u; index < SHA256_BLOCK_SIZE; ++index) {
        inner_pad[index] = (uint8_t)(key_block[index] ^ 0x36u);
        outer_pad[index] = (uint8_t)(key_block[index] ^ 0x5cu);
    }

    sha256_init(&context);
    if (!sha256_update(&context, inner_pad, sizeof(inner_pad)) ||
        !sha256_update(&context, part1, part1_size) ||
        !sha256_update(&context, part2, part2_size) ||
        !sha256_update(&context, part3, part3_size) ||
        !sha256_final(&context, inner_digest)) {
        secure_zero(&context, sizeof(context));
        goto cleanup;
    }

    sha256_init(&context);
    if (!sha256_update(&context, outer_pad, sizeof(outer_pad)) ||
        !sha256_update(&context, inner_digest, sizeof(inner_digest)) ||
        !sha256_final(&context, out_mac)) {
        secure_zero(&context, sizeof(context));
        goto cleanup;
    }

    ok = true;

cleanup:
    secure_zero(key_block, sizeof(key_block));
    secure_zero(inner_pad, sizeof(inner_pad));
    secure_zero(outer_pad, sizeof(outer_pad));
    secure_zero(inner_digest, sizeof(inner_digest));
    secure_zero(hashed_key, sizeof(hashed_key));
    return ok;
}

bool aurora_identity_hmac_sha256(
    const uint8_t *key,
    size_t key_size,
    const uint8_t *data,
    size_t data_size,
    uint8_t out_mac[AURORA_IDENTITY_HMAC_SHA256_SIZE]) {
    return hmac_sha256_parts(
        key,
        key_size,
        data,
        data_size,
        NULL,
        0u,
        NULL,
        0u,
        out_mac);
}

bool aurora_identity_constant_time_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size) {
    uint8_t difference = 0u;
    size_t index;

    if (left == NULL || right == NULL) {
        return false;
    }

    for (index = 0u; index < size; ++index) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }

    return difference == 0u;
}

static bool drbg_update(
    struct aurora_identity_hmac_drbg *drbg,
    const uint8_t *provided,
    size_t provided_size) {
    uint8_t separator = 0x00u;
    uint8_t new_key[AURORA_IDENTITY_HMAC_SHA256_SIZE];
    uint8_t new_value[AURORA_IDENTITY_HMAC_SHA256_SIZE];

    if (drbg == NULL || (provided == NULL && provided_size != 0u)) {
        return false;
    }

    if (!hmac_sha256_parts(
            drbg->key,
            sizeof(drbg->key),
            drbg->value,
            sizeof(drbg->value),
            &separator,
            1u,
            provided,
            provided_size,
            new_key)) {
        return false;
    }
    memcpy(drbg->key, new_key, sizeof(drbg->key));

    if (!aurora_identity_hmac_sha256(
            drbg->key,
            sizeof(drbg->key),
            drbg->value,
            sizeof(drbg->value),
            new_value)) {
        secure_zero(new_key, sizeof(new_key));
        return false;
    }
    memcpy(drbg->value, new_value, sizeof(drbg->value));

    if (provided_size != 0u) {
        separator = 0x01u;
        if (!hmac_sha256_parts(
                drbg->key,
                sizeof(drbg->key),
                drbg->value,
                sizeof(drbg->value),
                &separator,
                1u,
                provided,
                provided_size,
                new_key)) {
            secure_zero(new_key, sizeof(new_key));
            secure_zero(new_value, sizeof(new_value));
            return false;
        }
        memcpy(drbg->key, new_key, sizeof(drbg->key));

        if (!aurora_identity_hmac_sha256(
                drbg->key,
                sizeof(drbg->key),
                drbg->value,
                sizeof(drbg->value),
                new_value)) {
            secure_zero(new_key, sizeof(new_key));
            secure_zero(new_value, sizeof(new_value));
            return false;
        }
        memcpy(drbg->value, new_value, sizeof(drbg->value));
    }

    secure_zero(new_key, sizeof(new_key));
    secure_zero(new_value, sizeof(new_value));
    return true;
}

bool aurora_identity_hmac_drbg_instantiate(
    struct aurora_identity_hmac_drbg *drbg,
    const uint8_t *entropy,
    size_t entropy_size,
    const uint8_t *nonce,
    size_t nonce_size,
    const uint8_t *personalization,
    size_t personalization_size) {
    uint8_t seed_material[
        AURORA_IDENTITY_HMAC_DRBG_MAX_ENTROPY_SIZE +
        AURORA_IDENTITY_HMAC_DRBG_MAX_NONCE_SIZE +
        AURORA_IDENTITY_HMAC_DRBG_MAX_PERSONALIZATION_SIZE];
    size_t seed_size = 0u;

    if (drbg == NULL || entropy == NULL || nonce == NULL ||
        entropy_size < AURORA_IDENTITY_HMAC_DRBG_MIN_ENTROPY_SIZE ||
        entropy_size > AURORA_IDENTITY_HMAC_DRBG_MAX_ENTROPY_SIZE ||
        nonce_size < AURORA_IDENTITY_HMAC_DRBG_MIN_NONCE_SIZE ||
        nonce_size > AURORA_IDENTITY_HMAC_DRBG_MAX_NONCE_SIZE ||
        personalization_size > AURORA_IDENTITY_HMAC_DRBG_MAX_PERSONALIZATION_SIZE ||
        (personalization == NULL && personalization_size != 0u)) {
        return false;
    }

    memcpy(seed_material + seed_size, entropy, entropy_size);
    seed_size += entropy_size;
    memcpy(seed_material + seed_size, nonce, nonce_size);
    seed_size += nonce_size;
    if (personalization_size != 0u) {
        memcpy(seed_material + seed_size, personalization, personalization_size);
        seed_size += personalization_size;
    }

    memset(drbg->key, 0x00, sizeof(drbg->key));
    memset(drbg->value, 0x01, sizeof(drbg->value));
    drbg->reseed_counter = 0u;
    drbg->instantiated = false;

    if (!drbg_update(drbg, seed_material, seed_size)) {
        aurora_identity_hmac_drbg_clear(drbg);
        secure_zero(seed_material, sizeof(seed_material));
        return false;
    }

    drbg->reseed_counter = 1u;
    drbg->instantiated = true;
    secure_zero(seed_material, sizeof(seed_material));
    return true;
}

bool aurora_identity_hmac_drbg_reseed(
    struct aurora_identity_hmac_drbg *drbg,
    const uint8_t *entropy,
    size_t entropy_size,
    const uint8_t *additional,
    size_t additional_size) {
    uint8_t seed_material[
        AURORA_IDENTITY_HMAC_DRBG_MAX_ENTROPY_SIZE +
        AURORA_IDENTITY_HMAC_DRBG_MAX_ADDITIONAL_SIZE];
    size_t seed_size = 0u;

    if (drbg == NULL || !drbg->instantiated || entropy == NULL ||
        entropy_size < AURORA_IDENTITY_HMAC_DRBG_MIN_ENTROPY_SIZE ||
        entropy_size > AURORA_IDENTITY_HMAC_DRBG_MAX_ENTROPY_SIZE ||
        additional_size > AURORA_IDENTITY_HMAC_DRBG_MAX_ADDITIONAL_SIZE ||
        (additional == NULL && additional_size != 0u)) {
        return false;
    }

    memcpy(seed_material + seed_size, entropy, entropy_size);
    seed_size += entropy_size;
    if (additional_size != 0u) {
        memcpy(seed_material + seed_size, additional, additional_size);
        seed_size += additional_size;
    }

    if (!drbg_update(drbg, seed_material, seed_size)) {
        secure_zero(seed_material, sizeof(seed_material));
        return false;
    }

    drbg->reseed_counter = 1u;
    secure_zero(seed_material, sizeof(seed_material));
    return true;
}

bool aurora_identity_hmac_drbg_generate(
    struct aurora_identity_hmac_drbg *drbg,
    uint8_t *out,
    size_t out_size,
    const uint8_t *additional,
    size_t additional_size) {
    size_t produced = 0u;

    if (drbg == NULL || !drbg->instantiated || out == NULL || out_size == 0u ||
        out_size > AURORA_IDENTITY_HMAC_DRBG_MAX_REQUEST_SIZE ||
        additional_size > AURORA_IDENTITY_HMAC_DRBG_MAX_ADDITIONAL_SIZE ||
        (additional == NULL && additional_size != 0u) ||
        drbg->reseed_counter > AURORA_IDENTITY_HMAC_DRBG_RESEED_INTERVAL) {
        return false;
    }

    if (additional_size != 0u && !drbg_update(drbg, additional, additional_size)) {
        return false;
    }

    while (produced < out_size) {
        uint8_t next_value[AURORA_IDENTITY_HMAC_SHA256_SIZE];
        size_t remaining = out_size - produced;
        size_t take = remaining < sizeof(next_value) ? remaining : sizeof(next_value);

        if (!aurora_identity_hmac_sha256(
                drbg->key,
                sizeof(drbg->key),
                drbg->value,
                sizeof(drbg->value),
                next_value)) {
            secure_zero(next_value, sizeof(next_value));
            secure_zero(out, out_size);
            return false;
        }

        memcpy(drbg->value, next_value, sizeof(drbg->value));
        memcpy(out + produced, next_value, take);
        produced += take;
        secure_zero(next_value, sizeof(next_value));
    }

    if (!drbg_update(drbg, additional, additional_size)) {
        secure_zero(out, out_size);
        return false;
    }

    ++drbg->reseed_counter;
    return true;
}

void aurora_identity_hmac_drbg_clear(struct aurora_identity_hmac_drbg *drbg) {
    if (drbg != NULL) {
        secure_zero(drbg, sizeof(*drbg));
    }
}
