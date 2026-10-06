#ifndef AURORA_IDENTITY_PROTECTED_STATE_TRANSPORT_H
#define AURORA_IDENTITY_PROTECTED_STATE_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

enum aurora_identity_protected_state_read_result {
    AURORA_IDENTITY_PROTECTED_STATE_READ_OK = 0,
    AURORA_IDENTITY_PROTECTED_STATE_READ_NOT_FOUND,
    AURORA_IDENTITY_PROTECTED_STATE_READ_ERROR
};

enum aurora_identity_protected_state_create_result {
    AURORA_IDENTITY_PROTECTED_STATE_CREATE_OK = 0,
    AURORA_IDENTITY_PROTECTED_STATE_CREATE_EXISTS,
    AURORA_IDENTITY_PROTECTED_STATE_CREATE_ERROR
};

enum aurora_identity_protected_state_replace_result {
    AURORA_IDENTITY_PROTECTED_STATE_REPLACE_OK = 0,
    AURORA_IDENTITY_PROTECTED_STATE_REPLACE_ERROR
};

struct aurora_identity_protected_state_transport_ops {
    void *context;

    enum aurora_identity_protected_state_read_result (*read_record)(
        void *context,
        const char *name,
        uint8_t *buffer,
        size_t capacity,
        size_t *out_size);

    enum aurora_identity_protected_state_create_result (*create_record_once_durable)(
        void *context,
        const char *name,
        const uint8_t *buffer,
        size_t size);

    /*
     * Publish a complete replacement record for a redundancy protocol. A
     * failed operation may leave the old complete record or no record, but
     * must never expose a partially written target.
     */
    enum aurora_identity_protected_state_replace_result (*replace_record_durable)(
        void *context,
        const char *name,
        const uint8_t *buffer,
        size_t size);
};

#endif
