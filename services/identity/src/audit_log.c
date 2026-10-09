#include "aurora/identity/audit_log.h"

#include <string.h>

bool aurora_identity_audit_event_type_valid(uint32_t event_type) {
    return event_type > AURORA_IDENTITY_AUDIT_EVENT_NONE &&
        event_type < AURORA_IDENTITY_AUDIT_EVENT_COUNT;
}

bool aurora_identity_audit_outcome_valid(uint32_t outcome) {
    return outcome > AURORA_IDENTITY_AUDIT_OUTCOME_NONE &&
        outcome < AURORA_IDENTITY_AUDIT_OUTCOME_COUNT;
}

void aurora_identity_audit_log_init(struct aurora_identity_audit_log *log) {
    if (log == NULL) return;
    memset(log, 0, sizeof(*log));
    log->next_sequence = UINT64_C(1);
}

void aurora_identity_audit_log_clear(struct aurora_identity_audit_log *log) {
    aurora_identity_audit_log_init(log);
}

bool aurora_identity_audit_append(
    struct aurora_identity_audit_log *log,
    uint32_t event_type,
    uint32_t outcome,
    uint32_t reason_code,
    uint64_t monotonic_ms,
    uint64_t session_generation,
    const struct aurora_identity_user_id *user_id
) {
    if (log == NULL ||
        !aurora_identity_audit_event_type_valid(event_type) ||
        !aurora_identity_audit_outcome_valid(outcome) ||
        log->next_sequence == 0u ||
        log->next_index >= AURORA_IDENTITY_AUDIT_CAPACITY ||
        log->count > AURORA_IDENTITY_AUDIT_CAPACITY) {
        return false;
    }

    if (user_id != NULL && aurora_identity_user_id_is_zero(user_id)) {
        return false;
    }

    struct aurora_identity_audit_record record;
    memset(&record, 0, sizeof(record));
    record.record_version = AURORA_IDENTITY_AUDIT_RECORD_VERSION;
    record.event_type = event_type;
    record.outcome = outcome;
    record.reason_code = reason_code;
    record.sequence = log->next_sequence++;
    record.monotonic_ms = monotonic_ms;
    record.session_generation = session_generation;

    if (user_id != NULL) {
        record.has_user_id = true;
        record.user_id = *user_id;
    }

    log->entries[log->next_index] = record;
    log->next_index = (log->next_index + 1u) % AURORA_IDENTITY_AUDIT_CAPACITY;
    if (log->count < AURORA_IDENTITY_AUDIT_CAPACITY) ++log->count;
    return true;
}

size_t aurora_identity_audit_count(const struct aurora_identity_audit_log *log) {
    return log == NULL ? 0u : log->count;
}

bool aurora_identity_audit_get_oldest(
    const struct aurora_identity_audit_log *log,
    size_t offset,
    struct aurora_identity_audit_record *out_record
) {
    if (log == NULL || out_record == NULL || offset >= log->count ||
        log->count > AURORA_IDENTITY_AUDIT_CAPACITY ||
        log->next_index >= AURORA_IDENTITY_AUDIT_CAPACITY) {
        return false;
    }

    size_t oldest = log->count == AURORA_IDENTITY_AUDIT_CAPACITY
        ? log->next_index
        : 0u;
    size_t index = (oldest + offset) % AURORA_IDENTITY_AUDIT_CAPACITY;
    *out_record = log->entries[index];

    return out_record->record_version == AURORA_IDENTITY_AUDIT_RECORD_VERSION &&
        aurora_identity_audit_event_type_valid(out_record->event_type) &&
        aurora_identity_audit_outcome_valid(out_record->outcome) &&
        out_record->sequence != 0u &&
        (!out_record->has_user_id ||
         !aurora_identity_user_id_is_zero(&out_record->user_id));
}
