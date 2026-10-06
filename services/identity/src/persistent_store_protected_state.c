#include "aurora/identity/persistent_store_protected_state.h"

#include <string.h>

static const char *slot_name(uint32_t slot) {
    if (slot == 0u) return "identity-store.a";
    if (slot == 1u) return "identity-store.b";
    return NULL;
}

static enum aurora_identity_persistent_io_result protected_read_slot(
    void *context,
    uint32_t slot,
    uint8_t *buffer,
    size_t capacity,
    size_t *out_size
) {
    struct aurora_identity_persistent_protected_state_store *store = context;
    const char *name = slot_name(slot);

    if (store == NULL || !store->initialized || name == NULL ||
        buffer == NULL || out_size == NULL || capacity == 0u) {
        return AURORA_IDENTITY_PERSISTENT_IO_ERROR;
    }

    enum aurora_identity_protected_state_read_result result =
        store->transport.read_record(
            store->transport.context,
            name,
            buffer,
            capacity,
            out_size);

    if (result == AURORA_IDENTITY_PROTECTED_STATE_READ_OK)
        return AURORA_IDENTITY_PERSISTENT_IO_OK;
    if (result == AURORA_IDENTITY_PROTECTED_STATE_READ_NOT_FOUND)
        return AURORA_IDENTITY_PERSISTENT_IO_NOT_FOUND;
    return AURORA_IDENTITY_PERSISTENT_IO_ERROR;
}

static bool protected_write_slot_atomic(
    void *context,
    uint32_t slot,
    const uint8_t *buffer,
    size_t size
) {
    struct aurora_identity_persistent_protected_state_store *store = context;
    const char *name = slot_name(slot);

    if (store == NULL || !store->initialized || name == NULL ||
        buffer == NULL || size == 0u ||
        size > AURORA_IDENTITY_STORE_MAX_IMAGE_SIZE) {
        return false;
    }

    return store->transport.replace_record_durable(
        store->transport.context,
        name,
        buffer,
        size) == AURORA_IDENTITY_PROTECTED_STATE_REPLACE_OK;
}

bool aurora_identity_persistent_protected_state_store_init(
    struct aurora_identity_persistent_protected_state_store *store,
    const struct aurora_identity_protected_state_transport_ops *transport
) {
    if (store == NULL || transport == NULL ||
        transport->read_record == NULL ||
        transport->replace_record_durable == NULL) {
        return false;
    }

    memset(store, 0, sizeof(*store));
    store->transport = *transport;
    store->initialized = true;
    return true;
}

struct aurora_identity_persistent_io_ops
    aurora_identity_persistent_protected_state_io_ops(
        struct aurora_identity_persistent_protected_state_store *store
    ) {
    struct aurora_identity_persistent_io_ops ops;
    memset(&ops, 0, sizeof(ops));
    ops.context = store;
    ops.read_slot = protected_read_slot;
    ops.write_slot_atomic = protected_write_slot_atomic;
    return ops;
}
