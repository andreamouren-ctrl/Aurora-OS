#ifndef AURORA_IDENTITY_MACHINE_SECRET_PROTECTED_STATE_H
#define AURORA_IDENTITY_MACHINE_SECRET_PROTECTED_STATE_H

#include "aurora/identity/machine_secret.h"

#include <stdbool.h>
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
};

struct aurora_identity_machine_secret_protected_state_store {
    struct aurora_identity_protected_state_transport_ops transport;
    bool initialized;
};

bool aurora_identity_machine_secret_protected_state_store_init(
    struct aurora_identity_machine_secret_protected_state_store *store,
    const struct aurora_identity_protected_state_transport_ops *transport);

struct aurora_identity_machine_secret_store_ops
    aurora_identity_machine_secret_protected_state_store_ops(
        struct aurora_identity_machine_secret_protected_state_store *store);

#endif
