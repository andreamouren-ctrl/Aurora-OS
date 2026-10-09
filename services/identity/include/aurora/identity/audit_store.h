#ifndef AURORA_IDENTITY_AUDIT_STORE_H
#define AURORA_IDENTITY_AUDIT_STORE_H

#include "aurora/identity/audit_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT 2u
#define AURORA_IDENTITY_AUDIT_STORE_SCHEMA_VERSION 1u
#define AURORA_IDENTITY_AUDIT_STORE_HEADER_SIZE 40u
#define AURORA_IDENTITY_AUDIT_STORE_RECORD_SIZE 64u
#define AURORA_IDENTITY_AUDIT_STORE_MAX_IMAGE_SIZE \
    (AURORA_IDENTITY_AUDIT_STORE_HEADER_SIZE + \
     AURORA_IDENTITY_AUDIT_CAPACITY * AURORA_IDENTITY_AUDIT_STORE_RECORD_SIZE)

enum aurora_identity_audit_io_result {
    AURORA_IDENTITY_AUDIT_IO_OK = 0,
    AURORA_IDENTITY_AUDIT_IO_NOT_FOUND,
    AURORA_IDENTITY_AUDIT_IO_ERROR
};

enum aurora_identity_audit_open_result {
    AURORA_IDENTITY_AUDIT_OPEN_OK = 0,
    AURORA_IDENTITY_AUDIT_OPEN_EMPTY,
    AURORA_IDENTITY_AUDIT_OPEN_IO_ERROR,
    AURORA_IDENTITY_AUDIT_OPEN_CORRUPT,
    AURORA_IDENTITY_AUDIT_OPEN_UNSUPPORTED_SCHEMA
};

struct aurora_identity_audit_io_ops {
    void *context;
    enum aurora_identity_audit_io_result (*read_slot)(
        void *context,
        uint32_t slot,
        uint8_t *buffer,
        size_t capacity,
        size_t *out_size);
    bool (*write_slot_atomic)(
        void *context,
        uint32_t slot,
        const uint8_t *buffer,
        size_t size);
};

struct aurora_identity_audit_store {
    struct aurora_identity_audit_io_ops io;
    struct aurora_identity_audit_log log;
    uint64_t generation;
    uint32_t active_slot;
    bool opened;
    bool has_snapshot;
};

enum aurora_identity_audit_open_result aurora_identity_audit_store_open(
    struct aurora_identity_audit_store *store,
    const struct aurora_identity_audit_io_ops *io);

bool aurora_identity_audit_store_append(
    struct aurora_identity_audit_store *store,
    uint32_t event_type,
    uint32_t outcome,
    uint32_t reason_code,
    uint64_t monotonic_ms,
    uint64_t session_generation,
    const struct aurora_identity_user_id *user_id);

uint64_t aurora_identity_audit_store_generation(
    const struct aurora_identity_audit_store *store);

size_t aurora_identity_audit_store_count(
    const struct aurora_identity_audit_store *store);

bool aurora_identity_audit_store_get_oldest(
    const struct aurora_identity_audit_store *store,
    size_t offset,
    struct aurora_identity_audit_record *out_record);

bool aurora_identity_audit_store_get_newest_before_for_user(
    const struct aurora_identity_audit_store *store,
    const struct aurora_identity_user_id *user_id,
    uint64_t before_sequence,
    struct aurora_identity_audit_record *out_record);

#endif
