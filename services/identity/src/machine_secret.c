#include "aurora/identity/machine_secret.h"
#include "aurora/identity/crypto_foundation.h"

#include <string.h>

static const uint8_t checksum_domain[] =
    "AURORA.IDENTITY.MACHINE-SECRET-RECORD.V1";
static const uint8_t lookup_key_domain[] =
    "AURORA.IDENTITY.LOOKUP-KEY.V1";

static void secure_zero(void *buffer, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    if (buffer == NULL) return;
    while (size > 0u) {
        *bytes++ = 0u;
        --size;
    }
}

static void store_u32_le(uint8_t out[4u], uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
    out[2] = (uint8_t)(value >> 16u);
    out[3] = (uint8_t)(value >> 24u);
}

static void store_u64_le(uint8_t out[8u], uint64_t value) {
    for (size_t i = 0u; i < 8u; ++i) {
        out[i] = (uint8_t)(value >> (i * 8u));
    }
}

bool aurora_identity_machine_secret_is_zero(
    const struct aurora_identity_machine_secret *secret) {
    uint8_t aggregate = 0u;
    if (secret == NULL) return true;
    for (size_t i = 0u; i < sizeof(secret->bytes); ++i) {
        aggregate |= secret->bytes[i];
    }
    return aggregate == 0u;
}

void aurora_identity_machine_secret_clear(
    struct aurora_identity_machine_secret *secret) {
    if (secret != NULL) secure_zero(secret, sizeof(*secret));
}

static bool compute_record_checksum(
    const struct aurora_identity_machine_secret_record *record,
    uint8_t out[AURORA_IDENTITY_MACHINE_SECRET_CHECKSUM_SIZE]) {
    uint8_t material[
        sizeof(checksum_domain) - 1u + 1u + 4u + 8u +
        AURORA_IDENTITY_MACHINE_SECRET_SIZE];
    size_t offset = 0u;

    if (record == NULL || out == NULL) return false;

    memcpy(material + offset, checksum_domain, sizeof(checksum_domain) - 1u);
    offset += sizeof(checksum_domain) - 1u;
    material[offset++] = 0u;
    store_u32_le(material + offset, record->record_version);
    offset += 4u;
    store_u64_le(material + offset, record->generation);
    offset += 8u;
    memcpy(material + offset, record->secret.bytes, sizeof(record->secret.bytes));
    offset += sizeof(record->secret.bytes);

    bool ok = aurora_identity_sha256(material, offset, out);
    secure_zero(material, sizeof(material));
    return ok;
}

static bool record_valid(
    const struct aurora_identity_machine_secret_record *record) {
    uint8_t expected[AURORA_IDENTITY_MACHINE_SECRET_CHECKSUM_SIZE];
    bool valid = false;

    if (record == NULL ||
        record->record_version != AURORA_IDENTITY_MACHINE_SECRET_RECORD_VERSION ||
        record->generation != UINT64_C(1) ||
        aurora_identity_machine_secret_is_zero(&record->secret)) {
        return false;
    }

    if (compute_record_checksum(record, expected)) {
        valid = aurora_identity_constant_time_equal(
            expected,
            record->checksum,
            sizeof(expected));
    }

    secure_zero(expected, sizeof(expected));
    return valid;
}

static bool records_same_secret(
    const struct aurora_identity_machine_secret_record *left,
    const struct aurora_identity_machine_secret_record *right) {
    return left->record_version == right->record_version &&
        left->generation == right->generation &&
        aurora_identity_constant_time_equal(
            left->secret.bytes,
            right->secret.bytes,
            AURORA_IDENTITY_MACHINE_SECRET_SIZE);
}

enum aurora_identity_machine_secret_result aurora_identity_machine_secret_load(
    const struct aurora_identity_machine_secret_store_ops *store,
    struct aurora_identity_machine_secret *out_secret) {
    struct aurora_identity_machine_secret_record records[
        AURORA_IDENTITY_MACHINE_SECRET_REPLICA_COUNT];
    bool valid[AURORA_IDENTITY_MACHINE_SECRET_REPLICA_COUNT] = { false, false };
    uint32_t valid_count = 0u;
    uint32_t present_count = 0u;
    uint32_t first_valid = 0u;

    if (store == NULL || store->load_replica == NULL || out_secret == NULL) {
        return AURORA_IDENTITY_MACHINE_SECRET_INVALID_ARGUMENT;
    }

    secure_zero(out_secret, sizeof(*out_secret));
    secure_zero(records, sizeof(records));

    for (uint32_t i = 0u; i < AURORA_IDENTITY_MACHINE_SECRET_REPLICA_COUNT; ++i) {
        enum aurora_identity_machine_secret_load_result result =
            store->load_replica(store->context, i, &records[i]);

        if (result == AURORA_IDENTITY_MACHINE_SECRET_LOAD_ERROR) {
            secure_zero(records, sizeof(records));
            return AURORA_IDENTITY_MACHINE_SECRET_BACKEND_ERROR;
        }
        if (result == AURORA_IDENTITY_MACHINE_SECRET_LOAD_ABSENT) continue;
        if (result != AURORA_IDENTITY_MACHINE_SECRET_LOAD_OK) {
            secure_zero(records, sizeof(records));
            return AURORA_IDENTITY_MACHINE_SECRET_BACKEND_ERROR;
        }

        ++present_count;
        valid[i] = record_valid(&records[i]);
        if (valid[i]) {
            if (valid_count == 0u) first_valid = i;
            ++valid_count;
        }
    }

    if (present_count == 0u) {
        secure_zero(records, sizeof(records));
        return AURORA_IDENTITY_MACHINE_SECRET_NOT_PROVISIONED;
    }

    if (valid_count == 0u) {
        secure_zero(records, sizeof(records));
        return AURORA_IDENTITY_MACHINE_SECRET_CORRUPT;
    }

    if (valid_count == AURORA_IDENTITY_MACHINE_SECRET_REPLICA_COUNT &&
        !records_same_secret(&records[0], &records[1])) {
        secure_zero(records, sizeof(records));
        return AURORA_IDENTITY_MACHINE_SECRET_CONFLICT;
    }

    memcpy(out_secret, &records[first_valid].secret, sizeof(*out_secret));
    secure_zero(records, sizeof(records));
    return AURORA_IDENTITY_MACHINE_SECRET_OK;
}

static enum aurora_identity_machine_secret_result generate_record(
    const struct aurora_identity_random_ops *random,
    struct aurora_identity_machine_secret_record *record) {
    if (random == NULL || random->fill_random == NULL || record == NULL) {
        return AURORA_IDENTITY_MACHINE_SECRET_INVALID_ARGUMENT;
    }

    secure_zero(record, sizeof(*record));
    record->record_version = AURORA_IDENTITY_MACHINE_SECRET_RECORD_VERSION;
    record->generation = UINT64_C(1);

    bool generated = false;
    for (uint32_t attempt = 0u;
         attempt < AURORA_IDENTITY_MACHINE_SECRET_GENERATION_ATTEMPTS;
         ++attempt) {
        secure_zero(&record->secret, sizeof(record->secret));
        if (!random->fill_random(
                random->context,
                record->secret.bytes,
                sizeof(record->secret.bytes))) {
            secure_zero(record, sizeof(*record));
            return AURORA_IDENTITY_MACHINE_SECRET_RANDOM_ERROR;
        }
        if (!aurora_identity_machine_secret_is_zero(&record->secret)) {
            generated = true;
            break;
        }
    }

    if (!generated || !compute_record_checksum(record, record->checksum)) {
        secure_zero(record, sizeof(*record));
        return generated
            ? AURORA_IDENTITY_MACHINE_SECRET_BACKEND_ERROR
            : AURORA_IDENTITY_MACHINE_SECRET_RANDOM_ERROR;
    }

    return AURORA_IDENTITY_MACHINE_SECRET_OK;
}

enum aurora_identity_machine_secret_result
    aurora_identity_machine_secret_load_or_provision(
        const struct aurora_identity_machine_secret_core *core,
        struct aurora_identity_machine_secret *out_secret) {
    struct aurora_identity_machine_secret_record record;
    enum aurora_identity_machine_secret_result existing;

    if (core == NULL || out_secret == NULL ||
        core->store.load_replica == NULL || core->store.publish_replica == NULL ||
        core->random.fill_random == NULL) {
        return AURORA_IDENTITY_MACHINE_SECRET_INVALID_ARGUMENT;
    }

    existing = aurora_identity_machine_secret_load(&core->store, out_secret);
    if (existing == AURORA_IDENTITY_MACHINE_SECRET_OK) return existing;
    if (existing != AURORA_IDENTITY_MACHINE_SECRET_NOT_PROVISIONED) return existing;

    enum aurora_identity_machine_secret_result generated =
        generate_record(&core->random, &record);
    if (generated != AURORA_IDENTITY_MACHINE_SECRET_OK) {
        secure_zero(out_secret, sizeof(*out_secret));
        return generated;
    }

    for (uint32_t i = 0u; i < AURORA_IDENTITY_MACHINE_SECRET_REPLICA_COUNT; ++i) {
        enum aurora_identity_machine_secret_publish_result publish =
            core->store.publish_replica(core->store.context, i, &record);

        if (publish == AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_CONFLICT) {
            struct aurora_identity_machine_secret concurrent;
            enum aurora_identity_machine_secret_result reload =
                aurora_identity_machine_secret_load(&core->store, &concurrent);
            if (reload == AURORA_IDENTITY_MACHINE_SECRET_OK &&
                aurora_identity_constant_time_equal(
                    concurrent.bytes,
                    record.secret.bytes,
                    sizeof(concurrent.bytes))) {
                memcpy(out_secret, &concurrent, sizeof(*out_secret));
                aurora_identity_machine_secret_clear(&concurrent);
                secure_zero(&record, sizeof(record));
                return AURORA_IDENTITY_MACHINE_SECRET_OK;
            }
            aurora_identity_machine_secret_clear(&concurrent);
            secure_zero(&record, sizeof(record));
            secure_zero(out_secret, sizeof(*out_secret));
            return reload == AURORA_IDENTITY_MACHINE_SECRET_BACKEND_ERROR
                ? reload
                : AURORA_IDENTITY_MACHINE_SECRET_CONFLICT;
        }

        if (publish != AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_OK) {
            secure_zero(&record, sizeof(record));
            secure_zero(out_secret, sizeof(*out_secret));
            return AURORA_IDENTITY_MACHINE_SECRET_BACKEND_ERROR;
        }
    }

    memcpy(out_secret, &record.secret, sizeof(*out_secret));
    secure_zero(&record, sizeof(record));
    return AURORA_IDENTITY_MACHINE_SECRET_OK;
}

bool aurora_identity_machine_secret_derive_lookup_key(
    const struct aurora_identity_machine_secret *secret,
    uint8_t out_lookup_key[32u]) {
    if (secret == NULL || out_lookup_key == NULL ||
        aurora_identity_machine_secret_is_zero(secret)) {
        return false;
    }

    return aurora_identity_hmac_sha256(
        secret->bytes,
        sizeof(secret->bytes),
        lookup_key_domain,
        sizeof(lookup_key_domain) - 1u,
        out_lookup_key);
}
