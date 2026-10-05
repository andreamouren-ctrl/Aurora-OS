#include "aurora/identity/machine_secret_protected_state.h"
#include "machine_secret_codec.h"

#include <string.h>

static const char *replica_name(uint32_t replica_index) {
    if (replica_index == 0u) return "machine-secret.a";
    if (replica_index == 1u) return "machine-secret.b";
    return NULL;
}

static void secure_zero(void *buffer, size_t size) {
    volatile uint8_t *bytes = buffer;
    if (buffer == NULL) return;
    while (size > 0u) {
        *bytes++ = 0u;
        --size;
    }
}

static enum aurora_identity_machine_secret_load_result protected_load_replica(
    void *context,
    uint32_t replica_index,
    struct aurora_identity_machine_secret_record *out_record) {
    struct aurora_identity_machine_secret_protected_state_store *store = context;
    uint8_t disk[AURORA_IDENTITY_MACHINE_SECRET_DISK_SIZE];
    size_t size = 0u;
    const char *name = replica_name(replica_index);

    if (store == NULL || !store->initialized || out_record == NULL || name == NULL) {
        return AURORA_IDENTITY_MACHINE_SECRET_LOAD_ERROR;
    }

    memset(out_record, 0, sizeof(*out_record));
    secure_zero(disk, sizeof(disk));

    enum aurora_identity_protected_state_read_result result =
        store->transport.read_record(
            store->transport.context,
            name,
            disk,
            sizeof(disk),
            &size);

    if (result == AURORA_IDENTITY_PROTECTED_STATE_READ_NOT_FOUND) {
        secure_zero(disk, sizeof(disk));
        return AURORA_IDENTITY_MACHINE_SECRET_LOAD_ABSENT;
    }
    if (result != AURORA_IDENTITY_PROTECTED_STATE_READ_OK) {
        secure_zero(disk, sizeof(disk));
        return AURORA_IDENTITY_MACHINE_SECRET_LOAD_ERROR;
    }

    if (!aurora_identity_machine_secret_record_decode(
            disk,
            size,
            out_record)) {
        memset(out_record, 0, sizeof(*out_record));
    }

    secure_zero(disk, sizeof(disk));
    return AURORA_IDENTITY_MACHINE_SECRET_LOAD_OK;
}

static enum aurora_identity_machine_secret_publish_result protected_publish_replica(
    void *context,
    uint32_t replica_index,
    const struct aurora_identity_machine_secret_record *record) {
    struct aurora_identity_machine_secret_protected_state_store *store = context;
    uint8_t disk[AURORA_IDENTITY_MACHINE_SECRET_DISK_SIZE];
    const char *name = replica_name(replica_index);

    if (store == NULL || !store->initialized || record == NULL || name == NULL ||
        !aurora_identity_machine_secret_record_encode(record, disk)) {
        return AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_ERROR;
    }

    enum aurora_identity_protected_state_create_result result =
        store->transport.create_record_once_durable(
            store->transport.context,
            name,
            disk,
            sizeof(disk));

    secure_zero(disk, sizeof(disk));

    if (result == AURORA_IDENTITY_PROTECTED_STATE_CREATE_OK) {
        return AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_OK;
    }
    if (result == AURORA_IDENTITY_PROTECTED_STATE_CREATE_EXISTS) {
        return AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_CONFLICT;
    }
    return AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_ERROR;
}

bool aurora_identity_machine_secret_protected_state_store_init(
    struct aurora_identity_machine_secret_protected_state_store *store,
    const struct aurora_identity_protected_state_transport_ops *transport) {
    if (store == NULL || transport == NULL ||
        transport->read_record == NULL ||
        transport->create_record_once_durable == NULL) {
        return false;
    }

    memset(store, 0, sizeof(*store));
    store->transport = *transport;
    store->initialized = true;
    return true;
}

struct aurora_identity_machine_secret_store_ops
    aurora_identity_machine_secret_protected_state_store_ops(
        struct aurora_identity_machine_secret_protected_state_store *store) {
    struct aurora_identity_machine_secret_store_ops ops;
    memset(&ops, 0, sizeof(ops));
    ops.context = store;
    ops.load_replica = protected_load_replica;
    ops.publish_replica = protected_publish_replica;
    return ops;
}
