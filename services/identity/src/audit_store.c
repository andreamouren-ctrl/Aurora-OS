#include "aurora/identity/audit_store.h"

#include <limits.h>
#include <string.h>

static const uint8_t audit_magic[8] = {'A','U','R','A','U','D','1','\0'};

enum decode_result {
    DECODE_OK = 0,
    DECODE_CORRUPT,
    DECODE_UNSUPPORTED
};

static void put_u32_le(uint8_t *b, size_t o, uint32_t v) {
    b[o] = (uint8_t)v;
    b[o+1u] = (uint8_t)(v >> 8u);
    b[o+2u] = (uint8_t)(v >> 16u);
    b[o+3u] = (uint8_t)(v >> 24u);
}

static void put_u64_le(uint8_t *b, size_t o, uint64_t v) {
    for (uint32_t i = 0u; i < 8u; ++i) b[o+i] = (uint8_t)(v >> (i*8u));
}

static uint32_t get_u32_le(const uint8_t *b, size_t o) {
    return ((uint32_t)b[o]) |
        ((uint32_t)b[o+1u] << 8u) |
        ((uint32_t)b[o+2u] << 16u) |
        ((uint32_t)b[o+3u] << 24u);
}

static uint64_t get_u64_le(const uint8_t *b, size_t o) {
    uint64_t v = 0u;
    for (uint32_t i = 0u; i < 8u; ++i) v |= ((uint64_t)b[o+i]) << (i*8u);
    return v;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *buffer, size_t size) {
    for (size_t i = 0u; i < size; ++i) {
        crc ^= (uint32_t)buffer[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)(0u - (crc & 1u));
            crc = (crc >> 1u) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc;
}

static uint32_t image_crc32(const uint8_t *buffer, size_t size) {
    if (buffer == NULL || size < AURORA_IDENTITY_AUDIT_STORE_HEADER_SIZE) return 0u;
    uint32_t crc = UINT32_C(0xffffffff);
    crc = crc32_update(crc, buffer, 28u);
    crc = crc32_update(crc, buffer + 32u, size - 32u);
    return ~crc;
}

static size_t image_size_for_count(size_t count) {
    if (count > AURORA_IDENTITY_AUDIT_CAPACITY) return 0u;
    return AURORA_IDENTITY_AUDIT_STORE_HEADER_SIZE +
        count * AURORA_IDENTITY_AUDIT_STORE_RECORD_SIZE;
}

static bool encode_record(
    const struct aurora_identity_audit_record *record,
    uint8_t *out
) {
    if (record == NULL || out == NULL ||
        record->record_version != AURORA_IDENTITY_AUDIT_RECORD_VERSION ||
        !aurora_identity_audit_event_type_valid(record->event_type) ||
        !aurora_identity_audit_outcome_valid(record->outcome) ||
        record->sequence == 0u ||
        (record->has_user_id &&
         aurora_identity_user_id_is_zero(&record->user_id))) {
        return false;
    }

    memset(out, 0, AURORA_IDENTITY_AUDIT_STORE_RECORD_SIZE);
    put_u32_le(out, 0u, record->record_version);
    put_u32_le(out, 4u, record->event_type);
    put_u32_le(out, 8u, record->outcome);
    put_u32_le(out, 12u, record->reason_code);
    put_u64_le(out, 16u, record->sequence);
    put_u64_le(out, 24u, record->monotonic_ms);
    put_u64_le(out, 32u, record->session_generation);
    put_u32_le(out, 40u, record->has_user_id ? 1u : 0u);
    if (record->has_user_id) {
        memcpy(out + 48u, record->user_id.bytes, AURORA_IDENTITY_USER_ID_SIZE);
    }
    return true;
}

static bool decode_record(
    const uint8_t *in,
    struct aurora_identity_audit_record *record
) {
    if (in == NULL || record == NULL) return false;
    memset(record, 0, sizeof(*record));
    record->record_version = get_u32_le(in, 0u);
    record->event_type = get_u32_le(in, 4u);
    record->outcome = get_u32_le(in, 8u);
    record->reason_code = get_u32_le(in, 12u);
    record->sequence = get_u64_le(in, 16u);
    record->monotonic_ms = get_u64_le(in, 24u);
    record->session_generation = get_u64_le(in, 32u);
    uint32_t has_user = get_u32_le(in, 40u);
    if (has_user > 1u || get_u32_le(in, 44u) != 0u) return false;
    record->has_user_id = has_user == 1u;
    memcpy(record->user_id.bytes, in + 48u, AURORA_IDENTITY_USER_ID_SIZE);

    if (record->record_version != AURORA_IDENTITY_AUDIT_RECORD_VERSION ||
        !aurora_identity_audit_event_type_valid(record->event_type) ||
        !aurora_identity_audit_outcome_valid(record->outcome) ||
        record->sequence == 0u ||
        (record->has_user_id && aurora_identity_user_id_is_zero(&record->user_id))) {
        return false;
    }

    if (!record->has_user_id) {
        for (size_t i = 0u; i < AURORA_IDENTITY_USER_ID_SIZE; ++i) {
            if (record->user_id.bytes[i] != 0u) return false;
        }
    }
    return true;
}

static bool encode_log(
    const struct aurora_identity_audit_log *log,
    uint64_t generation,
    uint8_t *buffer,
    size_t capacity,
    size_t *out_size
) {
    if (log == NULL || buffer == NULL || out_size == NULL ||
        generation == 0u || log->next_sequence == 0u ||
        log->count > AURORA_IDENTITY_AUDIT_CAPACITY) return false;

    size_t size = image_size_for_count(log->count);
    if (size == 0u || size > capacity) return false;

    memset(buffer, 0, size);
    memcpy(buffer, audit_magic, sizeof(audit_magic));
    put_u32_le(buffer, 8u, AURORA_IDENTITY_AUDIT_STORE_SCHEMA_VERSION);
    put_u32_le(buffer, 12u, AURORA_IDENTITY_AUDIT_STORE_HEADER_SIZE);
    put_u64_le(buffer, 16u, generation);
    put_u32_le(buffer, 24u, (uint32_t)log->count);
    put_u64_le(buffer, 32u, log->next_sequence);

    uint64_t previous_sequence = 0u;
    for (size_t i = 0u; i < log->count; ++i) {
        struct aurora_identity_audit_record record;
        if (!aurora_identity_audit_get_oldest(log, i, &record) ||
            record.sequence <= previous_sequence ||
            !encode_record(
                &record,
                buffer + AURORA_IDENTITY_AUDIT_STORE_HEADER_SIZE +
                    i * AURORA_IDENTITY_AUDIT_STORE_RECORD_SIZE)) {
            return false;
        }
        previous_sequence = record.sequence;
    }
    if (log->count != 0u && log->next_sequence <= previous_sequence) return false;

    put_u32_le(buffer, 28u, image_crc32(buffer, size));
    *out_size = size;
    return true;
}

static enum decode_result decode_log(
    const uint8_t *buffer,
    size_t size,
    struct aurora_identity_audit_log *out_log,
    uint64_t *out_generation
) {
    if (buffer == NULL || out_log == NULL || out_generation == NULL ||
        size < AURORA_IDENTITY_AUDIT_STORE_HEADER_SIZE ||
        memcmp(buffer, audit_magic, sizeof(audit_magic)) != 0) {
        return DECODE_CORRUPT;
    }

    if (get_u32_le(buffer, 8u) != AURORA_IDENTITY_AUDIT_STORE_SCHEMA_VERSION)
        return DECODE_UNSUPPORTED;
    if (get_u32_le(buffer, 12u) != AURORA_IDENTITY_AUDIT_STORE_HEADER_SIZE)
        return DECODE_CORRUPT;

    uint64_t generation = get_u64_le(buffer, 16u);
    uint32_t count = get_u32_le(buffer, 24u);
    uint32_t checksum = get_u32_le(buffer, 28u);
    uint64_t next_sequence = get_u64_le(buffer, 32u);
    if (generation == 0u || next_sequence == 0u ||
        count > AURORA_IDENTITY_AUDIT_CAPACITY ||
        size != image_size_for_count(count) ||
        checksum != image_crc32(buffer, size)) return DECODE_CORRUPT;

    struct aurora_identity_audit_log decoded;
    aurora_identity_audit_log_init(&decoded);
    decoded.next_sequence = next_sequence;

    uint64_t previous_sequence = 0u;
    for (uint32_t i = 0u; i < count; ++i) {
        struct aurora_identity_audit_record record;
        if (!decode_record(
                buffer + AURORA_IDENTITY_AUDIT_STORE_HEADER_SIZE +
                    (size_t)i * AURORA_IDENTITY_AUDIT_STORE_RECORD_SIZE,
                &record) ||
            record.sequence <= previous_sequence ||
            record.sequence >= next_sequence) {
            return DECODE_CORRUPT;
        }
        decoded.entries[i] = record;
        decoded.count++;
        decoded.next_index = (decoded.next_index + 1u) % AURORA_IDENTITY_AUDIT_CAPACITY;
        previous_sequence = record.sequence;
    }

    *out_log = decoded;
    *out_generation = generation;
    return DECODE_OK;
}

enum aurora_identity_audit_open_result aurora_identity_audit_store_open(
    struct aurora_identity_audit_store *store,
    const struct aurora_identity_audit_io_ops *io
) {
    if (store == NULL || io == NULL ||
        io->read_slot == NULL || io->write_slot_atomic == NULL) {
        return AURORA_IDENTITY_AUDIT_OPEN_IO_ERROR;
    }

    uint8_t images[AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT]
        [AURORA_IDENTITY_AUDIT_STORE_MAX_IMAGE_SIZE];
    size_t sizes[AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT] = {0u, 0u};
    struct aurora_identity_audit_log logs[AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT];
    uint64_t generations[AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT] = {0u, 0u};
    enum decode_result decoded[AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT] = {
        DECODE_CORRUPT, DECODE_CORRUPT
    };
    bool any_found = false, any_valid = false;
    uint32_t best = 0u;

    memset(store, 0, sizeof(*store));
    store->io = *io;

    for (uint32_t slot = 0u; slot < AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT; ++slot) {
        enum aurora_identity_audit_io_result read = io->read_slot(
            io->context, slot, images[slot], sizeof(images[slot]), &sizes[slot]);
        if (read == AURORA_IDENTITY_AUDIT_IO_ERROR) {
            memset(store, 0, sizeof(*store));
            return AURORA_IDENTITY_AUDIT_OPEN_IO_ERROR;
        }
        if (read == AURORA_IDENTITY_AUDIT_IO_NOT_FOUND) continue;

        any_found = true;
        decoded[slot] = decode_log(
            images[slot], sizes[slot], &logs[slot], &generations[slot]);
        if (decoded[slot] == DECODE_UNSUPPORTED) {
            memset(store, 0, sizeof(*store));
            return AURORA_IDENTITY_AUDIT_OPEN_UNSUPPORTED_SCHEMA;
        }
        if (decoded[slot] == DECODE_OK) {
            if (!any_valid || generations[slot] > generations[best]) best = slot;
            any_valid = true;
        }
    }

    if (!any_found) {
        aurora_identity_audit_log_init(&store->log);
        store->opened = true;
        return AURORA_IDENTITY_AUDIT_OPEN_EMPTY;
    }
    if (!any_valid ||
        (decoded[0] == DECODE_OK && decoded[1] == DECODE_OK &&
         generations[0] == generations[1])) {
        memset(store, 0, sizeof(*store));
        return AURORA_IDENTITY_AUDIT_OPEN_CORRUPT;
    }

    store->log = logs[best];
    store->generation = generations[best];
    store->active_slot = best;
    store->opened = true;
    store->has_snapshot = true;
    return AURORA_IDENTITY_AUDIT_OPEN_OK;
}

bool aurora_identity_audit_store_append(
    struct aurora_identity_audit_store *store,
    uint32_t event_type,
    uint32_t outcome,
    uint32_t reason_code,
    uint64_t monotonic_ms,
    uint64_t session_generation,
    const struct aurora_identity_user_id *user_id
) {
    if (store == NULL || !store->opened ||
        store->io.write_slot_atomic == NULL ||
        store->generation == UINT64_MAX) return false;

    struct aurora_identity_audit_log candidate = store->log;
    if (!aurora_identity_audit_append(
            &candidate, event_type, outcome, reason_code,
            monotonic_ms, session_generation, user_id)) return false;

    uint64_t generation = store->generation + 1u;
    uint32_t slot = (uint32_t)(generation % AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT);
    uint8_t image[AURORA_IDENTITY_AUDIT_STORE_MAX_IMAGE_SIZE];
    size_t image_size = 0u;
    if (!encode_log(&candidate, generation, image, sizeof(image), &image_size) ||
        !store->io.write_slot_atomic(
            store->io.context, slot, image, image_size)) {
        memset(image, 0, sizeof(image));
        return false;
    }

    memset(image, 0, sizeof(image));
    store->log = candidate;
    store->generation = generation;
    store->active_slot = slot;
    store->has_snapshot = true;
    return true;
}

uint64_t aurora_identity_audit_store_generation(
    const struct aurora_identity_audit_store *store
) {
    return store != NULL && store->opened ? store->generation : 0u;
}

size_t aurora_identity_audit_store_count(
    const struct aurora_identity_audit_store *store
) {
    return store != NULL && store->opened
        ? aurora_identity_audit_count(&store->log)
        : 0u;
}

bool aurora_identity_audit_store_get_oldest(
    const struct aurora_identity_audit_store *store,
    size_t offset,
    struct aurora_identity_audit_record *out_record
) {
    return store != NULL && store->opened &&
        aurora_identity_audit_get_oldest(&store->log, offset, out_record);
}

static bool audit_user_id_equal(
    const struct aurora_identity_user_id *left,
    const struct aurora_identity_user_id *right
) {
    uint8_t difference = 0u;
    if (left == NULL || right == NULL) return false;
    for (size_t i = 0u; i < sizeof(left->bytes); ++i) {
        difference |= (uint8_t)(left->bytes[i] ^ right->bytes[i]);
    }
    return difference == 0u;
}

bool aurora_identity_audit_store_get_newest_before_for_user(
    const struct aurora_identity_audit_store *store,
    const struct aurora_identity_user_id *user_id,
    uint64_t before_sequence,
    struct aurora_identity_audit_record *out_record
) {
    struct aurora_identity_audit_record candidate;
    bool found = false;

    if (store == NULL || user_id == NULL || out_record == NULL ||
        !store->opened || aurora_identity_user_id_is_zero(user_id)) {
        return false;
    }

    memset(&candidate, 0, sizeof(candidate));
    for (size_t offset = 0u; offset < store->log.count; ++offset) {
        struct aurora_identity_audit_record record;
        memset(&record, 0, sizeof(record));
        if (!aurora_identity_audit_get_oldest(&store->log, offset, &record)) {
            memset(&candidate, 0, sizeof(candidate));
            return false;
        }

        if (!record.has_user_id ||
            !audit_user_id_equal(&record.user_id, user_id) ||
            (before_sequence != 0u && record.sequence >= before_sequence)) {
            memset(&record, 0, sizeof(record));
            continue;
        }

        if (!found || record.sequence > candidate.sequence) {
            candidate = record;
            found = true;
        }
        memset(&record, 0, sizeof(record));
    }

    if (!found) {
        memset(&candidate, 0, sizeof(candidate));
        return false;
    }

    *out_record = candidate;
    memset(&candidate, 0, sizeof(candidate));
    return true;
}
