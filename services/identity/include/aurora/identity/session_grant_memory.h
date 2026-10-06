#ifndef AURORA_IDENTITY_SESSION_GRANT_MEMORY_H
#define AURORA_IDENTITY_SESSION_GRANT_MEMORY_H

#include "aurora/identity/session_grant.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURORA_IDENTITY_SESSION_GRANT_MEMORY_CAPACITY 32u

struct aurora_identity_session_grant_memory_entry {
    struct aurora_identity_session_grant_record record;
    bool occupied;
};

struct aurora_identity_session_grant_memory_store {
    struct aurora_identity_session_grant_memory_entry
        entries[AURORA_IDENTITY_SESSION_GRANT_MEMORY_CAPACITY];
};

void aurora_identity_session_grant_memory_init(
    struct aurora_identity_session_grant_memory_store *store);

void aurora_identity_session_grant_memory_clear(
    struct aurora_identity_session_grant_memory_store *store);

size_t aurora_identity_session_grant_memory_count(
    const struct aurora_identity_session_grant_memory_store *store);

struct aurora_identity_session_grant_store_ops
    aurora_identity_session_grant_memory_ops(
        struct aurora_identity_session_grant_memory_store *store);

#endif
