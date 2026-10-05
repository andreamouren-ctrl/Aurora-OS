#ifndef AURORA_IDENTITY_PERSISTENT_STORE_H
#define AURORA_IDENTITY_PERSISTENT_STORE_H

#include "aurora/identity/core.h"
#include "aurora/identity/rotation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURORA_IDENTITY_STORE_SCHEMA_VERSION 2u
#define AURORA_IDENTITY_STORE_SLOT_COUNT 2u
#define AURORA_IDENTITY_STORE_MAX_IDENTITIES 32u
#define AURORA_IDENTITY_STORE_MAX_KEY_RECORDS 32u
#define AURORA_IDENTITY_STORE_MAX_IMAGE_SIZE 8192u

enum aurora_identity_persistent_io_result {
    AURORA_IDENTITY_PERSISTENT_IO_OK = 0,
    AURORA_IDENTITY_PERSISTENT_IO_NOT_FOUND,
    AURORA_IDENTITY_PERSISTENT_IO_ERROR
};

enum aurora_identity_persistent_open_result {
    AURORA_IDENTITY_PERSISTENT_OPEN_OK = 0,
    AURORA_IDENTITY_PERSISTENT_OPEN_EMPTY,
    AURORA_IDENTITY_PERSISTENT_OPEN_CORRUPT,
    AURORA_IDENTITY_PERSISTENT_OPEN_UNSUPPORTED_SCHEMA,
    AURORA_IDENTITY_PERSISTENT_OPEN_IO_ERROR
};

struct aurora_identity_persistent_io_ops {
    void *context;

    enum aurora_identity_persistent_io_result (*read_slot)(
        void *context,
        uint32_t slot,
        uint8_t *buffer,
        size_t capacity,
        size_t *out_size);

    /*
     * Publish one complete slot image atomically and durably. A failed call
     * must leave either the previous slot image or no slot image visible.
     */
    bool (*write_slot_atomic)(
        void *context,
        uint32_t slot,
        const uint8_t *buffer,
        size_t size);
};

struct aurora_identity_persistent_state {
    uint64_t generation;
    size_t identity_count;
    size_t key_record_count;
    struct aurora_identity_record identities[AURORA_IDENTITY_STORE_MAX_IDENTITIES];
    struct aurora_identity_key_record key_records[AURORA_IDENTITY_STORE_MAX_KEY_RECORDS];
};

struct aurora_identity_persistent_store {
    struct aurora_identity_persistent_io_ops io;
    struct aurora_identity_persistent_state state;
    uint32_t active_slot;
    bool opened;
    bool has_snapshot;
};

enum aurora_identity_persistent_open_result aurora_identity_persistent_store_open(
    struct aurora_identity_persistent_store *store,
    const struct aurora_identity_persistent_io_ops *io);

uint64_t aurora_identity_persistent_store_generation(
    const struct aurora_identity_persistent_store *store);

size_t aurora_identity_persistent_store_identity_count(
    const struct aurora_identity_persistent_store *store);

size_t aurora_identity_persistent_store_key_record_count(
    const struct aurora_identity_persistent_store *store);

enum aurora_identity_store_result aurora_identity_persistent_store_find_identity(
    const struct aurora_identity_persistent_store *store,
    const struct aurora_identity_user_id *user_id,
    struct aurora_identity_record *out_record);

struct aurora_identity_store_ops aurora_identity_persistent_store_core_ops(
    struct aurora_identity_persistent_store *store);

struct aurora_identity_rotation_store_ops aurora_identity_persistent_store_rotation_ops(
    struct aurora_identity_persistent_store *store);

#endif
