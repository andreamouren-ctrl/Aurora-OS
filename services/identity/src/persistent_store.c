#include "aurora/identity/persistent_store.h"

#include <limits.h>
#include <string.h>

#define AURORA_IDENTITY_STORE_HEADER_SIZE 40u
#define AURORA_IDENTITY_STORE_IDENTITY_DISK_SIZE 32u
#define AURORA_IDENTITY_STORE_KEY_DISK_SIZE 196u

/*
 * Keep the legacy format magic stable and version the on-disk contract through
 * the explicit schema field. This lets schema-v1 snapshots fail as
 * UNSUPPORTED_SCHEMA instead of being mistaken for random corruption.
 */
static const uint8_t aurora_identity_store_magic[8] = {
    (uint8_t)'A', (uint8_t)'I', (uint8_t)'D', (uint8_t)'B',
    (uint8_t)'V', (uint8_t)'1', 0u, 0u
};

enum image_decode_result {
    IMAGE_DECODE_OK = 0,
    IMAGE_DECODE_CORRUPT,
    IMAGE_DECODE_UNSUPPORTED
};

static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t size) {
    size_t i;

    if (a == NULL || b == NULL) {
        return false;
    }

    for (i = 0u; i < size; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }

    return true;
}

static bool bytes_are_zero(const uint8_t *bytes, size_t size) {
    uint8_t aggregate = 0u;
    size_t i;

    if (bytes == NULL) {
        return true;
    }

    for (i = 0u; i < size; ++i) {
        aggregate |= bytes[i];
    }

    return aggregate == 0u;
}

static void put_u32_le(uint8_t *buffer, size_t offset, uint32_t value) {
    buffer[offset] = (uint8_t)(value & 0xffu);
    buffer[offset + 1u] = (uint8_t)((value >> 8u) & 0xffu);
    buffer[offset + 2u] = (uint8_t)((value >> 16u) & 0xffu);
    buffer[offset + 3u] = (uint8_t)((value >> 24u) & 0xffu);
}

static void put_u64_le(uint8_t *buffer, size_t offset, uint64_t value) {
    uint32_t i;

    for (i = 0u; i < 8u; ++i) {
        buffer[offset + i] = (uint8_t)((value >> (i * 8u)) & 0xffu);
    }
}

static uint32_t get_u32_le(const uint8_t *buffer, size_t offset) {
    return ((uint32_t)buffer[offset]) |
           ((uint32_t)buffer[offset + 1u] << 8u) |
           ((uint32_t)buffer[offset + 2u] << 16u) |
           ((uint32_t)buffer[offset + 3u] << 24u);
}

static uint64_t get_u64_le(const uint8_t *buffer, size_t offset) {
    uint64_t value = 0u;
    uint32_t i;

    for (i = 0u; i < 8u; ++i) {
        value |= ((uint64_t)buffer[offset + i]) << (i * 8u);
    }

    return value;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *buffer, size_t size) {
    size_t i;
    uint32_t bit;

    for (i = 0u; i < size; ++i) {
        crc ^= (uint32_t)buffer[i];
        for (bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)(0u - (crc & 1u));
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }

    return crc;
}

static uint32_t image_crc32(const uint8_t *buffer, size_t size) {
    uint32_t crc = 0xffffffffu;

    if (buffer == NULL || size < AURORA_IDENTITY_STORE_HEADER_SIZE) {
        return 0u;
    }

    crc = crc32_update(crc, buffer, 36u);
    crc = crc32_update(
        crc,
        buffer + AURORA_IDENTITY_STORE_HEADER_SIZE,
        size - AURORA_IDENTITY_STORE_HEADER_SIZE);
    return ~crc;
}

static bool status_is_valid(uint32_t value) {
    return value <= (uint32_t)AURORA_IDENTITY_RECORD_RECOVERY_REQUIRED;
}

static bool persisted_role_is_valid(uint32_t value) {
    return value == (uint32_t)AURORA_IDENTITY_ROLE_ADMINISTRATOR ||
           value == (uint32_t)AURORA_IDENTITY_ROLE_STANDARD_USER ||
           value == (uint32_t)AURORA_IDENTITY_ROLE_GUEST;
}

static bool identity_exists(
    const struct aurora_identity_persistent_state *state,
    const struct aurora_identity_user_id *user_id) {
    size_t i;

    if (state == NULL || user_id == NULL) {
        return false;
    }

    for (i = 0u; i < state->identity_count; ++i) {
        if (bytes_equal(
                state->identities[i].user_id.bytes,
                user_id->bytes,
                AURORA_IDENTITY_USER_ID_SIZE)) {
            return true;
        }
    }

    return false;
}

static bool state_is_valid(const struct aurora_identity_persistent_state *state) {
    size_t i;
    size_t j;

    if (state == NULL ||
        state->identity_count > AURORA_IDENTITY_STORE_MAX_IDENTITIES ||
        state->key_record_count > AURORA_IDENTITY_STORE_MAX_KEY_RECORDS) {
        return false;
    }

    for (i = 0u; i < state->identity_count; ++i) {
        const struct aurora_identity_record *identity = &state->identities[i];

        if (bytes_are_zero(identity->user_id.bytes, AURORA_IDENTITY_USER_ID_SIZE) ||
            !status_is_valid((uint32_t)identity->status) ||
            !persisted_role_is_valid((uint32_t)identity->role) ||
            identity->policy_version == 0u ||
            identity->record_version == 0u) {
            return false;
        }

        for (j = i + 1u; j < state->identity_count; ++j) {
            if (bytes_equal(
                    identity->user_id.bytes,
                    state->identities[j].user_id.bytes,
                    AURORA_IDENTITY_USER_ID_SIZE)) {
                return false;
            }
        }
    }

    for (i = 0u; i < state->key_record_count; ++i) {
        const struct aurora_identity_key_record *key = &state->key_records[i];

        if (bytes_are_zero(
                key->credential_id.bytes,
                AURORA_IDENTITY_CREDENTIAL_ID_SIZE) ||
            bytes_are_zero(key->user_id.bytes, AURORA_IDENTITY_USER_ID_SIZE) ||
            !identity_exists(state, &key->user_id) ||
            !status_is_valid((uint32_t)key->status) ||
            key->salt_size == 0u ||
            key->salt_size > AURORA_IDENTITY_SALT_MAX_SIZE ||
            key->verifier_size == 0u ||
            key->verifier_size > AURORA_IDENTITY_VERIFIER_MAX_SIZE ||
            key->kdf.algorithm == 0u ||
            key->kdf.parameters_version == 0u ||
            key->kdf.memory_kib == 0u ||
            key->kdf.time_cost == 0u ||
            key->kdf.parallelism == 0u) {
            return false;
        }

        for (j = i + 1u; j < state->key_record_count; ++j) {
            const struct aurora_identity_key_record *other = &state->key_records[j];

            if (bytes_equal(
                    key->credential_id.bytes,
                    other->credential_id.bytes,
                    AURORA_IDENTITY_CREDENTIAL_ID_SIZE) ||
                bytes_equal(
                    key->lookup_tag,
                    other->lookup_tag,
                    AURORA_IDENTITY_LOOKUP_TAG_SIZE)) {
                return false;
            }
        }
    }

    return true;
}

static size_t encoded_size(const struct aurora_identity_persistent_state *state) {
    size_t payload;

    if (state == NULL) {
        return 0u;
    }

    payload =
        state->identity_count * AURORA_IDENTITY_STORE_IDENTITY_DISK_SIZE +
        state->key_record_count * AURORA_IDENTITY_STORE_KEY_DISK_SIZE;

    if (payload > AURORA_IDENTITY_STORE_MAX_IMAGE_SIZE - AURORA_IDENTITY_STORE_HEADER_SIZE) {
        return 0u;
    }

    return AURORA_IDENTITY_STORE_HEADER_SIZE + payload;
}

static bool encode_state(
    const struct aurora_identity_persistent_state *state,
    uint8_t *buffer,
    size_t capacity,
    size_t *out_size) {
    size_t size;
    size_t offset;
    size_t i;
    uint32_t checksum;

    if (state == NULL || buffer == NULL || out_size == NULL ||
        state->generation == 0u || !state_is_valid(state)) {
        return false;
    }

    size = encoded_size(state);
    if (size == 0u || size > capacity) {
        return false;
    }

    memset(buffer, 0, size);
    memcpy(buffer, aurora_identity_store_magic, sizeof(aurora_identity_store_magic));
    put_u32_le(buffer, 8u, AURORA_IDENTITY_STORE_SCHEMA_VERSION);
    put_u32_le(buffer, 12u, AURORA_IDENTITY_STORE_HEADER_SIZE);
    put_u64_le(buffer, 16u, state->generation);
    put_u32_le(buffer, 24u, (uint32_t)state->identity_count);
    put_u32_le(buffer, 28u, (uint32_t)state->key_record_count);
    put_u32_le(buffer, 32u, (uint32_t)(size - AURORA_IDENTITY_STORE_HEADER_SIZE));
    put_u32_le(buffer, 36u, 0u);

    offset = AURORA_IDENTITY_STORE_HEADER_SIZE;
    for (i = 0u; i < state->identity_count; ++i) {
        const struct aurora_identity_record *identity = &state->identities[i];

        memcpy(buffer + offset, identity->user_id.bytes, AURORA_IDENTITY_USER_ID_SIZE);
        offset += AURORA_IDENTITY_USER_ID_SIZE;
        put_u32_le(buffer, offset, (uint32_t)identity->status);
        offset += 4u;
        put_u32_le(buffer, offset, (uint32_t)identity->role);
        offset += 4u;
        put_u32_le(buffer, offset, identity->policy_version);
        offset += 4u;
        put_u32_le(buffer, offset, identity->record_version);
        offset += 4u;
    }

    for (i = 0u; i < state->key_record_count; ++i) {
        const struct aurora_identity_key_record *key = &state->key_records[i];

        memcpy(
            buffer + offset,
            key->credential_id.bytes,
            AURORA_IDENTITY_CREDENTIAL_ID_SIZE);
        offset += AURORA_IDENTITY_CREDENTIAL_ID_SIZE;
        memcpy(buffer + offset, key->user_id.bytes, AURORA_IDENTITY_USER_ID_SIZE);
        offset += AURORA_IDENTITY_USER_ID_SIZE;
        memcpy(buffer + offset, key->lookup_tag, AURORA_IDENTITY_LOOKUP_TAG_SIZE);
        offset += AURORA_IDENTITY_LOOKUP_TAG_SIZE;

        put_u32_le(buffer, offset, key->kdf.algorithm);
        offset += 4u;
        put_u32_le(buffer, offset, key->kdf.parameters_version);
        offset += 4u;
        put_u32_le(buffer, offset, key->kdf.memory_kib);
        offset += 4u;
        put_u32_le(buffer, offset, key->kdf.time_cost);
        offset += 4u;
        put_u32_le(buffer, offset, key->kdf.parallelism);
        offset += 4u;

        put_u32_le(buffer, offset, (uint32_t)key->salt_size);
        offset += 4u;
        memcpy(buffer + offset, key->salt, AURORA_IDENTITY_SALT_MAX_SIZE);
        offset += AURORA_IDENTITY_SALT_MAX_SIZE;

        put_u32_le(buffer, offset, (uint32_t)key->verifier_size);
        offset += 4u;
        memcpy(buffer + offset, key->verifier, AURORA_IDENTITY_VERIFIER_MAX_SIZE);
        offset += AURORA_IDENTITY_VERIFIER_MAX_SIZE;

        put_u32_le(buffer, offset, (uint32_t)key->status);
        offset += 4u;
        put_u32_le(buffer, offset, key->failed_attempts);
        offset += 4u;

        /* throttle_until_ms is intentionally not serialized. */
    }

    if (offset != size) {
        return false;
    }

    checksum = image_crc32(buffer, size);
    put_u32_le(buffer, 36u, checksum);
    *out_size = size;
    return true;
}

static enum image_decode_result decode_state(
    const uint8_t *buffer,
    size_t size,
    struct aurora_identity_persistent_state *out_state) {
    struct aurora_identity_persistent_state decoded;
    uint32_t schema;
    uint32_t header_size;
    uint32_t identity_count;
    uint32_t key_count;
    uint32_t payload_size;
    uint32_t stored_checksum;
    uint32_t computed_checksum;
    size_t expected_size;
    size_t offset;
    size_t i;

    if (buffer == NULL || out_state == NULL ||
        size < AURORA_IDENTITY_STORE_HEADER_SIZE) {
        return IMAGE_DECODE_CORRUPT;
    }

    if (!bytes_equal(buffer, aurora_identity_store_magic, sizeof(aurora_identity_store_magic))) {
        return IMAGE_DECODE_CORRUPT;
    }

    schema = get_u32_le(buffer, 8u);
    if (schema != AURORA_IDENTITY_STORE_SCHEMA_VERSION) {
        return IMAGE_DECODE_UNSUPPORTED;
    }

    header_size = get_u32_le(buffer, 12u);
    if (header_size != AURORA_IDENTITY_STORE_HEADER_SIZE) {
        return IMAGE_DECODE_CORRUPT;
    }

    identity_count = get_u32_le(buffer, 24u);
    key_count = get_u32_le(buffer, 28u);
    payload_size = get_u32_le(buffer, 32u);

    if (identity_count > AURORA_IDENTITY_STORE_MAX_IDENTITIES ||
        key_count > AURORA_IDENTITY_STORE_MAX_KEY_RECORDS) {
        return IMAGE_DECODE_CORRUPT;
    }

    expected_size =
        AURORA_IDENTITY_STORE_HEADER_SIZE +
        (size_t)identity_count * AURORA_IDENTITY_STORE_IDENTITY_DISK_SIZE +
        (size_t)key_count * AURORA_IDENTITY_STORE_KEY_DISK_SIZE;

    if (expected_size != size ||
        payload_size != (uint32_t)(size - AURORA_IDENTITY_STORE_HEADER_SIZE)) {
        return IMAGE_DECODE_CORRUPT;
    }

    stored_checksum = get_u32_le(buffer, 36u);
    computed_checksum = image_crc32(buffer, size);
    if (stored_checksum != computed_checksum) {
        return IMAGE_DECODE_CORRUPT;
    }

    memset(&decoded, 0, sizeof(decoded));
    decoded.generation = get_u64_le(buffer, 16u);
    decoded.identity_count = (size_t)identity_count;
    decoded.key_record_count = (size_t)key_count;

    if (decoded.generation == 0u) {
        return IMAGE_DECODE_CORRUPT;
    }

    offset = AURORA_IDENTITY_STORE_HEADER_SIZE;
    for (i = 0u; i < decoded.identity_count; ++i) {
        struct aurora_identity_record *identity = &decoded.identities[i];
        uint32_t status;
        uint32_t role;

        memcpy(identity->user_id.bytes, buffer + offset, AURORA_IDENTITY_USER_ID_SIZE);
        offset += AURORA_IDENTITY_USER_ID_SIZE;

        status = get_u32_le(buffer, offset);
        offset += 4u;
        if (!status_is_valid(status)) {
            return IMAGE_DECODE_CORRUPT;
        }
        identity->status = (enum aurora_identity_record_status)status;

        role = get_u32_le(buffer, offset);
        offset += 4u;
        if (!persisted_role_is_valid(role)) {
            return IMAGE_DECODE_CORRUPT;
        }
        identity->role = (enum aurora_identity_role)role;

        identity->policy_version = get_u32_le(buffer, offset);
        offset += 4u;
        identity->record_version = get_u32_le(buffer, offset);
        offset += 4u;
    }

    for (i = 0u; i < decoded.key_record_count; ++i) {
        struct aurora_identity_key_record *key = &decoded.key_records[i];
        uint32_t salt_size;
        uint32_t verifier_size;
        uint32_t status;

        memcpy(
            key->credential_id.bytes,
            buffer + offset,
            AURORA_IDENTITY_CREDENTIAL_ID_SIZE);
        offset += AURORA_IDENTITY_CREDENTIAL_ID_SIZE;
        memcpy(key->user_id.bytes, buffer + offset, AURORA_IDENTITY_USER_ID_SIZE);
        offset += AURORA_IDENTITY_USER_ID_SIZE;
        memcpy(key->lookup_tag, buffer + offset, AURORA_IDENTITY_LOOKUP_TAG_SIZE);
        offset += AURORA_IDENTITY_LOOKUP_TAG_SIZE;

        key->kdf.algorithm = get_u32_le(buffer, offset);
        offset += 4u;
        key->kdf.parameters_version = get_u32_le(buffer, offset);
        offset += 4u;
        key->kdf.memory_kib = get_u32_le(buffer, offset);
        offset += 4u;
        key->kdf.time_cost = get_u32_le(buffer, offset);
        offset += 4u;
        key->kdf.parallelism = get_u32_le(buffer, offset);
        offset += 4u;

        salt_size = get_u32_le(buffer, offset);
        offset += 4u;
        if (salt_size == 0u || salt_size > AURORA_IDENTITY_SALT_MAX_SIZE) {
            return IMAGE_DECODE_CORRUPT;
        }
        key->salt_size = (size_t)salt_size;
        memcpy(key->salt, buffer + offset, AURORA_IDENTITY_SALT_MAX_SIZE);
        offset += AURORA_IDENTITY_SALT_MAX_SIZE;

        verifier_size = get_u32_le(buffer, offset);
        offset += 4u;
        if (verifier_size == 0u || verifier_size > AURORA_IDENTITY_VERIFIER_MAX_SIZE) {
            return IMAGE_DECODE_CORRUPT;
        }
        key->verifier_size = (size_t)verifier_size;
        memcpy(key->verifier, buffer + offset, AURORA_IDENTITY_VERIFIER_MAX_SIZE);
        offset += AURORA_IDENTITY_VERIFIER_MAX_SIZE;

        status = get_u32_le(buffer, offset);
        offset += 4u;
        if (!status_is_valid(status)) {
            return IMAGE_DECODE_CORRUPT;
        }
        key->status = (enum aurora_identity_record_status)status;

        key->failed_attempts = get_u32_le(buffer, offset);
        offset += 4u;
        key->throttle_until_ms = 0u;
    }

    if (offset != size || !state_is_valid(&decoded)) {
        return IMAGE_DECODE_CORRUPT;
    }

    *out_state = decoded;
    return IMAGE_DECODE_OK;
}

static bool commit_state(
    struct aurora_identity_persistent_store *store,
    const struct aurora_identity_persistent_state *candidate_without_generation) {
    struct aurora_identity_persistent_state candidate;
    uint8_t image[AURORA_IDENTITY_STORE_MAX_IMAGE_SIZE];
    size_t image_size = 0u;
    uint32_t slot;

    if (store == NULL || candidate_without_generation == NULL ||
        !store->opened || store->io.write_slot_atomic == NULL ||
        store->state.generation == UINT64_MAX) {
        return false;
    }

    candidate = *candidate_without_generation;
    candidate.generation = store->state.generation + 1u;

    if (!encode_state(&candidate, image, sizeof(image), &image_size)) {
        return false;
    }

    slot = (uint32_t)(candidate.generation % AURORA_IDENTITY_STORE_SLOT_COUNT);
    if (!store->io.write_slot_atomic(
            store->io.context,
            slot,
            image,
            image_size)) {
        return false;
    }

    store->state = candidate;
    store->active_slot = slot;
    store->has_snapshot = true;
    return true;
}

enum aurora_identity_persistent_open_result aurora_identity_persistent_store_open(
    struct aurora_identity_persistent_store *store,
    const struct aurora_identity_persistent_io_ops *io) {
    uint8_t images[AURORA_IDENTITY_STORE_SLOT_COUNT][AURORA_IDENTITY_STORE_MAX_IMAGE_SIZE];
    size_t sizes[AURORA_IDENTITY_STORE_SLOT_COUNT] = {0u, 0u};
    enum aurora_identity_persistent_io_result io_results[AURORA_IDENTITY_STORE_SLOT_COUNT];
    enum image_decode_result decode_results[AURORA_IDENTITY_STORE_SLOT_COUNT] = {
        IMAGE_DECODE_CORRUPT, IMAGE_DECODE_CORRUPT
    };
    struct aurora_identity_persistent_state states[AURORA_IDENTITY_STORE_SLOT_COUNT];
    bool any_found = false;
    bool any_valid = false;
    uint32_t best_slot = 0u;
    uint32_t slot;

    if (store == NULL || io == NULL ||
        io->read_slot == NULL || io->write_slot_atomic == NULL) {
        return AURORA_IDENTITY_PERSISTENT_OPEN_IO_ERROR;
    }

    memset(store, 0, sizeof(*store));
    store->io = *io;
    memset(states, 0, sizeof(states));

    for (slot = 0u; slot < AURORA_IDENTITY_STORE_SLOT_COUNT; ++slot) {
        io_results[slot] = io->read_slot(
            io->context,
            slot,
            images[slot],
            sizeof(images[slot]),
            &sizes[slot]);

        if (io_results[slot] == AURORA_IDENTITY_PERSISTENT_IO_ERROR) {
            memset(store, 0, sizeof(*store));
            return AURORA_IDENTITY_PERSISTENT_OPEN_IO_ERROR;
        }

        if (io_results[slot] == AURORA_IDENTITY_PERSISTENT_IO_NOT_FOUND) {
            continue;
        }

        any_found = true;
        decode_results[slot] = decode_state(images[slot], sizes[slot], &states[slot]);

        if (decode_results[slot] == IMAGE_DECODE_UNSUPPORTED) {
            memset(store, 0, sizeof(*store));
            return AURORA_IDENTITY_PERSISTENT_OPEN_UNSUPPORTED_SCHEMA;
        }

        if (decode_results[slot] == IMAGE_DECODE_OK) {
            if (!any_valid || states[slot].generation > states[best_slot].generation) {
                best_slot = slot;
            }
            any_valid = true;
        }
    }

    if (!any_found) {
        memset(&store->state, 0, sizeof(store->state));
        store->opened = true;
        store->has_snapshot = false;
        store->active_slot = 0u;
        return AURORA_IDENTITY_PERSISTENT_OPEN_EMPTY;
    }

    if (!any_valid) {
        memset(store, 0, sizeof(*store));
        return AURORA_IDENTITY_PERSISTENT_OPEN_CORRUPT;
    }

    if (decode_results[0] == IMAGE_DECODE_OK &&
        decode_results[1] == IMAGE_DECODE_OK &&
        states[0].generation == states[1].generation) {
        memset(store, 0, sizeof(*store));
        return AURORA_IDENTITY_PERSISTENT_OPEN_CORRUPT;
    }

    store->state = states[best_slot];
    store->active_slot = best_slot;
    store->opened = true;
    store->has_snapshot = true;
    return AURORA_IDENTITY_PERSISTENT_OPEN_OK;
}

uint64_t aurora_identity_persistent_store_generation(
    const struct aurora_identity_persistent_store *store) {
    if (store == NULL || !store->opened) {
        return 0u;
    }

    return store->state.generation;
}

size_t aurora_identity_persistent_store_identity_count(
    const struct aurora_identity_persistent_store *store) {
    if (store == NULL || !store->opened) {
        return 0u;
    }

    return store->state.identity_count;
}

size_t aurora_identity_persistent_store_key_record_count(
    const struct aurora_identity_persistent_store *store) {
    if (store == NULL || !store->opened) {
        return 0u;
    }

    return store->state.key_record_count;
}

enum aurora_identity_store_result aurora_identity_persistent_store_find_identity(
    const struct aurora_identity_persistent_store *store,
    const struct aurora_identity_user_id *user_id,
    struct aurora_identity_record *out_record) {
    size_t i;

    if (store == NULL || user_id == NULL || out_record == NULL || !store->opened) {
        return AURORA_IDENTITY_STORE_ERROR;
    }

    for (i = 0u; i < store->state.identity_count; ++i) {
        if (bytes_equal(
                store->state.identities[i].user_id.bytes,
                user_id->bytes,
                AURORA_IDENTITY_USER_ID_SIZE)) {
            *out_record = store->state.identities[i];
            return AURORA_IDENTITY_STORE_OK;
        }
    }

    return AURORA_IDENTITY_STORE_NOT_FOUND;
}

static enum aurora_identity_store_result persistent_find_key_record(
    void *context,
    const uint8_t lookup_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE],
    struct aurora_identity_key_record *out_record) {
    struct aurora_identity_persistent_store *store =
        (struct aurora_identity_persistent_store *)context;
    size_t i;

    if (store == NULL || lookup_tag == NULL || out_record == NULL || !store->opened) {
        return AURORA_IDENTITY_STORE_ERROR;
    }

    for (i = 0u; i < store->state.key_record_count; ++i) {
        if (bytes_equal(
                store->state.key_records[i].lookup_tag,
                lookup_tag,
                AURORA_IDENTITY_LOOKUP_TAG_SIZE)) {
            *out_record = store->state.key_records[i];
            return AURORA_IDENTITY_STORE_OK;
        }
    }

    return AURORA_IDENTITY_STORE_NOT_FOUND;
}

static bool update_failure_state(
    struct aurora_identity_persistent_store *store,
    const struct aurora_identity_user_id *user_id,
    uint32_t failed_attempts,
    uint64_t throttle_until_ms) {
    struct aurora_identity_persistent_state staged;
    size_t i;
    size_t match_index = 0u;
    size_t matches = 0u;

    if (store == NULL || user_id == NULL || !store->opened) {
        return false;
    }

    for (i = 0u; i < store->state.key_record_count; ++i) {
        const struct aurora_identity_key_record *key = &store->state.key_records[i];

        if (key->status == AURORA_IDENTITY_RECORD_ACTIVE &&
            bytes_equal(
                key->user_id.bytes,
                user_id->bytes,
                AURORA_IDENTITY_USER_ID_SIZE)) {
            match_index = i;
            ++matches;
        }
    }

    if (matches != 1u) {
        return false;
    }

    /*
     * Re-arming a deadline after reboot changes only volatile state. Avoid a
     * pointless disk generation/write when the durable failed-attempt count is
     * unchanged.
     */
    if (store->state.key_records[match_index].failed_attempts == failed_attempts) {
        store->state.key_records[match_index].throttle_until_ms = throttle_until_ms;
        return true;
    }

    staged = store->state;
    staged.key_records[match_index].failed_attempts = failed_attempts;
    staged.key_records[match_index].throttle_until_ms = throttle_until_ms;
    return commit_state(store, &staged);
}

static bool persistent_store_failure_state(
    void *context,
    const struct aurora_identity_user_id *user_id,
    uint32_t failed_attempts,
    uint64_t throttle_until_ms) {
    return update_failure_state(
        (struct aurora_identity_persistent_store *)context,
        user_id,
        failed_attempts,
        throttle_until_ms);
}

static bool persistent_clear_failure_state(
    void *context,
    const struct aurora_identity_user_id *user_id) {
    return update_failure_state(
        (struct aurora_identity_persistent_store *)context,
        user_id,
        0u,
        0u);
}

static bool identity_id_conflicts(
    const struct aurora_identity_persistent_state *state,
    const struct aurora_identity_user_id *user_id) {
    return identity_exists(state, user_id);
}

static bool key_id_conflicts(
    const struct aurora_identity_persistent_state *state,
    const struct aurora_identity_credential_id *credential_id,
    size_t ignored_index,
    bool ignore_index) {
    size_t i;

    if (state == NULL || credential_id == NULL) {
        return true;
    }

    for (i = 0u; i < state->key_record_count; ++i) {
        if (ignore_index && i == ignored_index) {
            continue;
        }

        if (bytes_equal(
                state->key_records[i].credential_id.bytes,
                credential_id->bytes,
                AURORA_IDENTITY_CREDENTIAL_ID_SIZE)) {
            return true;
        }
    }

    return false;
}

static bool lookup_tag_conflicts(
    const struct aurora_identity_persistent_state *state,
    const uint8_t lookup_tag[AURORA_IDENTITY_LOOKUP_TAG_SIZE],
    size_t ignored_index,
    bool ignore_index) {
    size_t i;

    if (state == NULL || lookup_tag == NULL) {
        return true;
    }

    for (i = 0u; i < state->key_record_count; ++i) {
        if (ignore_index && i == ignored_index) {
            continue;
        }

        if (bytes_equal(
                state->key_records[i].lookup_tag,
                lookup_tag,
                AURORA_IDENTITY_LOOKUP_TAG_SIZE)) {
            return true;
        }
    }

    return false;
}

static enum aurora_identity_store_create_result persistent_create_identity_with_key(
    void *context,
    const struct aurora_identity_record *identity,
    const struct aurora_identity_key_record *key_record) {
    struct aurora_identity_persistent_store *store =
        (struct aurora_identity_persistent_store *)context;
    struct aurora_identity_persistent_state staged;
    struct aurora_identity_record committed_identity;

    if (store == NULL || identity == NULL || key_record == NULL || !store->opened) {
        return AURORA_IDENTITY_STORE_CREATE_ERROR;
    }

    if (store->state.identity_count >= AURORA_IDENTITY_STORE_MAX_IDENTITIES ||
        store->state.key_record_count >= AURORA_IDENTITY_STORE_MAX_KEY_RECORDS ||
        bytes_are_zero(identity->user_id.bytes, AURORA_IDENTITY_USER_ID_SIZE) ||
        bytes_are_zero(
            key_record->credential_id.bytes,
            AURORA_IDENTITY_CREDENTIAL_ID_SIZE) ||
        !bytes_equal(
            identity->user_id.bytes,
            key_record->user_id.bytes,
            AURORA_IDENTITY_USER_ID_SIZE) ||
        identity->role != AURORA_IDENTITY_ROLE_UNASSIGNED ||
        identity->status != AURORA_IDENTITY_RECORD_ACTIVE ||
        key_record->status != AURORA_IDENTITY_RECORD_ACTIVE ||
        key_record->failed_attempts != 0u ||
        key_record->throttle_until_ms != 0u) {
        return AURORA_IDENTITY_STORE_CREATE_ERROR;
    }

    if (identity_id_conflicts(&store->state, &identity->user_id) ||
        key_id_conflicts(
            &store->state, &key_record->credential_id, 0u, false) ||
        lookup_tag_conflicts(
            &store->state, key_record->lookup_tag, 0u, false)) {
        return AURORA_IDENTITY_STORE_CREATE_CONFLICT;
    }

    staged = store->state;
    committed_identity = *identity;
    committed_identity.role =
        staged.identity_count == 0u
            ? AURORA_IDENTITY_ROLE_ADMINISTRATOR
            : AURORA_IDENTITY_ROLE_STANDARD_USER;

    staged.identities[staged.identity_count++] = committed_identity;
    staged.key_records[staged.key_record_count++] = *key_record;

    if (!state_is_valid(&staged) || !commit_state(store, &staged)) {
        return AURORA_IDENTITY_STORE_CREATE_ERROR;
    }

    return AURORA_IDENTITY_STORE_CREATE_OK;
}

static enum aurora_identity_rotation_store_result persistent_replace_key_credential(
    void *context,
    const struct aurora_identity_user_id *user_id,
    const struct aurora_identity_credential_id *current_credential_id,
    const struct aurora_identity_key_record *replacement) {
    struct aurora_identity_persistent_store *store =
        (struct aurora_identity_persistent_store *)context;
    struct aurora_identity_persistent_state staged;
    size_t i;
    size_t current_index = 0u;
    size_t matches = 0u;

    if (store == NULL || user_id == NULL || current_credential_id == NULL ||
        replacement == NULL || !store->opened) {
        return AURORA_IDENTITY_ROTATION_STORE_ERROR;
    }

    if (!bytes_equal(
            replacement->user_id.bytes,
            user_id->bytes,
            AURORA_IDENTITY_USER_ID_SIZE) ||
        bytes_are_zero(
            replacement->credential_id.bytes,
            AURORA_IDENTITY_CREDENTIAL_ID_SIZE) ||
        replacement->status != AURORA_IDENTITY_RECORD_ACTIVE ||
        replacement->failed_attempts != 0u ||
        replacement->throttle_until_ms != 0u) {
        return AURORA_IDENTITY_ROTATION_STORE_ERROR;
    }

    for (i = 0u; i < store->state.key_record_count; ++i) {
        const struct aurora_identity_key_record *key = &store->state.key_records[i];

        if (key->status == AURORA_IDENTITY_RECORD_ACTIVE &&
            bytes_equal(
                key->credential_id.bytes,
                current_credential_id->bytes,
                AURORA_IDENTITY_CREDENTIAL_ID_SIZE) &&
            bytes_equal(
                key->user_id.bytes,
                user_id->bytes,
                AURORA_IDENTITY_USER_ID_SIZE)) {
            current_index = i;
            ++matches;
        }
    }

    if (matches == 0u) {
        return AURORA_IDENTITY_ROTATION_STORE_CURRENT_NOT_FOUND;
    }
    if (matches != 1u) {
        return AURORA_IDENTITY_ROTATION_STORE_ERROR;
    }

    if (key_id_conflicts(
            &store->state,
            &replacement->credential_id,
            current_index,
            true) ||
        lookup_tag_conflicts(
            &store->state,
            replacement->lookup_tag,
            current_index,
            true)) {
        return AURORA_IDENTITY_ROTATION_STORE_CONFLICT;
    }

    staged = store->state;
    staged.key_records[current_index] = *replacement;

    if (!state_is_valid(&staged) || !commit_state(store, &staged)) {
        return AURORA_IDENTITY_ROTATION_STORE_ERROR;
    }

    return AURORA_IDENTITY_ROTATION_STORE_OK;
}

struct aurora_identity_store_ops aurora_identity_persistent_store_core_ops(
    struct aurora_identity_persistent_store *store) {
    struct aurora_identity_store_ops ops;

    memset(&ops, 0, sizeof(ops));
    ops.context = store;
    ops.find_key_record_by_lookup_tag = persistent_find_key_record;
    ops.store_failure_state = persistent_store_failure_state;
    ops.clear_failure_state = persistent_clear_failure_state;
    ops.create_identity_with_key = persistent_create_identity_with_key;
    return ops;
}

struct aurora_identity_rotation_store_ops aurora_identity_persistent_store_rotation_ops(
    struct aurora_identity_persistent_store *store) {
    struct aurora_identity_rotation_store_ops ops;

    memset(&ops, 0, sizeof(ops));
    ops.context = store;
    ops.replace_key_credential = persistent_replace_key_credential;
    return ops;
}
