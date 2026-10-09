#ifndef AURORA_IDENTITY_AUDIT_LOG_H
#define AURORA_IDENTITY_AUDIT_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aurora/identity/core.h"

#define AURORA_IDENTITY_AUDIT_CAPACITY 64u
#define AURORA_IDENTITY_AUDIT_RECORD_VERSION 1u

enum aurora_identity_audit_event_type {
    AURORA_IDENTITY_AUDIT_EVENT_NONE = 0,
    AURORA_IDENTITY_AUDIT_EVENT_AUTH_SUCCESS,
    AURORA_IDENTITY_AUDIT_EVENT_AUTH_FAILURE,
    AURORA_IDENTITY_AUDIT_EVENT_SESSION_STARTED,
    AURORA_IDENTITY_AUDIT_EVENT_SESSION_LOCKED,
    AURORA_IDENTITY_AUDIT_EVENT_SESSION_UNLOCKED,
    AURORA_IDENTITY_AUDIT_EVENT_SESSION_LOGOUT,
    AURORA_IDENTITY_AUDIT_EVENT_SESSION_TERMINATED,
    AURORA_IDENTITY_AUDIT_EVENT_REAUTH_SUCCESS,
    AURORA_IDENTITY_AUDIT_EVENT_REAUTH_FAILURE,
    AURORA_IDENTITY_AUDIT_EVENT_CREDENTIAL_ROTATED,
    AURORA_IDENTITY_AUDIT_EVENT_COUNT
};

enum aurora_identity_audit_outcome {
    AURORA_IDENTITY_AUDIT_OUTCOME_NONE = 0,
    AURORA_IDENTITY_AUDIT_OUTCOME_SUCCESS,
    AURORA_IDENTITY_AUDIT_OUTCOME_FAILURE,
    AURORA_IDENTITY_AUDIT_OUTCOME_DENIED,
    AURORA_IDENTITY_AUDIT_OUTCOME_ERROR,
    AURORA_IDENTITY_AUDIT_OUTCOME_COUNT
};

struct aurora_identity_audit_record {
    uint32_t record_version;
    uint32_t event_type;
    uint32_t outcome;
    uint32_t reason_code;
    uint64_t sequence;
    uint64_t monotonic_ms;
    uint64_t session_generation;
    bool has_user_id;
    uint8_t reserved[7];
    struct aurora_identity_user_id user_id;
};

struct aurora_identity_audit_log {
    struct aurora_identity_audit_record entries[AURORA_IDENTITY_AUDIT_CAPACITY];
    uint64_t next_sequence;
    size_t count;
    size_t next_index;
};

bool aurora_identity_audit_event_type_valid(uint32_t event_type);
bool aurora_identity_audit_outcome_valid(uint32_t outcome);

void aurora_identity_audit_log_init(struct aurora_identity_audit_log *log);
void aurora_identity_audit_log_clear(struct aurora_identity_audit_log *log);

bool aurora_identity_audit_append(
    struct aurora_identity_audit_log *log,
    uint32_t event_type,
    uint32_t outcome,
    uint32_t reason_code,
    uint64_t monotonic_ms,
    uint64_t session_generation,
    const struct aurora_identity_user_id *user_id);

size_t aurora_identity_audit_count(const struct aurora_identity_audit_log *log);

bool aurora_identity_audit_get_oldest(
    const struct aurora_identity_audit_log *log,
    size_t offset,
    struct aurora_identity_audit_record *out_record);

#endif
