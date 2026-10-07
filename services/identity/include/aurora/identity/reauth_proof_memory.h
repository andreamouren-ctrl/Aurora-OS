#ifndef AURORA_IDENTITY_REAUTH_PROOF_MEMORY_H
#define AURORA_IDENTITY_REAUTH_PROOF_MEMORY_H

#include "aurora/identity/reauth_proof.h"

#include <stdbool.h>
#include <stddef.h>

#define AURORA_IDENTITY_REAUTH_PROOF_MEMORY_CAPACITY 32u

struct aurora_identity_reauth_memory_entry {
    struct aurora_identity_reauth_record record;
    bool occupied;
};

struct aurora_identity_reauth_memory_store {
    struct aurora_identity_reauth_memory_entry
        entries[AURORA_IDENTITY_REAUTH_PROOF_MEMORY_CAPACITY];
};

void aurora_identity_reauth_memory_init(
    struct aurora_identity_reauth_memory_store *store);

void aurora_identity_reauth_memory_clear(
    struct aurora_identity_reauth_memory_store *store);

size_t aurora_identity_reauth_memory_count(
    const struct aurora_identity_reauth_memory_store *store);

struct aurora_identity_reauth_store_ops aurora_identity_reauth_memory_ops(
    struct aurora_identity_reauth_memory_store *store);

#endif
