#include "aurora/identity/session_grant_memory.h"

#include <string.h>

static void secure_zero(void *buffer, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    if (buffer == NULL) return;
    while (size != 0u) {
        *bytes++ = 0u;
        --size;
    }
}

static bool bytes_are_zero(const uint8_t *bytes, size_t size) {
    uint8_t aggregate = 0u;
    if (bytes == NULL) return true;
    for (size_t i = 0u; i < size; ++i) aggregate |= bytes[i];
    return aggregate == 0u;
}

static bool tags_equal(
    const uint8_t left[AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE],
    const uint8_t right[AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE]
) {
    uint8_t difference = 0u;
    for (size_t i = 0u; i < AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE; ++i) {
        difference |= (uint8_t)(left[i] ^ right[i]);
    }
    return difference == 0u;
}

static bool record_valid(const struct aurora_identity_session_grant_record *record) {
    return record != NULL &&
        !aurora_identity_user_id_is_zero(&record->user_id) &&
        !bytes_are_zero(record->token_tag, sizeof(record->token_tag)) &&
        record->record_version == AURORA_IDENTITY_SESSION_GRANT_RECORD_VERSION &&
        record->expires_at_ms > record->issued_at_ms;
}

static void clear_entry(struct aurora_identity_session_grant_memory_entry *entry) {
    if (entry == NULL) return;
    secure_zero(entry, sizeof(*entry));
}

static enum aurora_identity_session_grant_store_issue_result memory_issue_grant(
    void *opaque,
    const struct aurora_identity_session_grant_record *record
) {
    struct aurora_identity_session_grant_memory_store *store =
        (struct aurora_identity_session_grant_memory_store *)opaque;
    size_t free_index = AURORA_IDENTITY_SESSION_GRANT_MEMORY_CAPACITY;

    if (store == NULL || !record_valid(record)) {
        return AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_ERROR;
    }

    for (size_t i = 0u; i < AURORA_IDENTITY_SESSION_GRANT_MEMORY_CAPACITY; ++i) {
        struct aurora_identity_session_grant_memory_entry *entry = &store->entries[i];

        if (entry->occupied && entry->record.expires_at_ms <= record->issued_at_ms) {
            clear_entry(entry);
        }

        if (!entry->occupied) {
            if (free_index == AURORA_IDENTITY_SESSION_GRANT_MEMORY_CAPACITY) {
                free_index = i;
            }
            continue;
        }

        if (tags_equal(entry->record.token_tag, record->token_tag)) {
            return AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_CONFLICT;
        }
    }

    if (free_index == AURORA_IDENTITY_SESSION_GRANT_MEMORY_CAPACITY) {
        return AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_ERROR;
    }

    store->entries[free_index].record = *record;
    store->entries[free_index].occupied = true;
    return AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_OK;
}

static enum aurora_identity_session_grant_store_consume_result memory_consume_grant(
    void *opaque,
    const uint8_t token_tag[AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE],
    uint64_t now_ms,
    struct aurora_identity_session_grant_record *out_record
) {
    struct aurora_identity_session_grant_memory_store *store =
        (struct aurora_identity_session_grant_memory_store *)opaque;

    if (store == NULL || token_tag == NULL || out_record == NULL ||
        bytes_are_zero(token_tag, AURORA_IDENTITY_SESSION_GRANT_TAG_SIZE)) {
        return AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_ERROR;
    }

    secure_zero(out_record, sizeof(*out_record));

    for (size_t i = 0u; i < AURORA_IDENTITY_SESSION_GRANT_MEMORY_CAPACITY; ++i) {
        struct aurora_identity_session_grant_memory_entry *entry = &store->entries[i];
        if (!entry->occupied || !tags_equal(entry->record.token_tag, token_tag)) {
            continue;
        }

        if (entry->record.expires_at_ms <= now_ms) {
            clear_entry(entry);
            return AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_EXPIRED;
        }

        *out_record = entry->record;
        clear_entry(entry);
        return AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_OK;
    }

    return AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_NOT_FOUND;
}

void aurora_identity_session_grant_memory_init(
    struct aurora_identity_session_grant_memory_store *store
) {
    if (store != NULL) secure_zero(store, sizeof(*store));
}

void aurora_identity_session_grant_memory_clear(
    struct aurora_identity_session_grant_memory_store *store
) {
    if (store != NULL) secure_zero(store, sizeof(*store));
}

size_t aurora_identity_session_grant_memory_count(
    const struct aurora_identity_session_grant_memory_store *store
) {
    size_t count = 0u;
    if (store == NULL) return 0u;
    for (size_t i = 0u; i < AURORA_IDENTITY_SESSION_GRANT_MEMORY_CAPACITY; ++i) {
        if (store->entries[i].occupied) ++count;
    }
    return count;
}

struct aurora_identity_session_grant_store_ops
    aurora_identity_session_grant_memory_ops(
        struct aurora_identity_session_grant_memory_store *store
) {
    struct aurora_identity_session_grant_store_ops ops;
    memset(&ops, 0, sizeof(ops));
    ops.context = store;
    ops.issue_grant = memory_issue_grant;
    ops.consume_grant = memory_consume_grant;
    return ops;
}
