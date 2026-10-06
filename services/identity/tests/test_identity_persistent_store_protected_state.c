#include "aurora/identity/persistent_store_protected_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fake_record {
    uint8_t data[AURORA_IDENTITY_STORE_MAX_IMAGE_SIZE];
    size_t size;
    bool present;
};

struct fake_transport {
    struct fake_record slots[AURORA_IDENTITY_STORE_SLOT_COUNT];
    bool fail_replace;
};

static void expect(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static int name_to_slot(const char *name) {
    if (name != NULL && strcmp(name, "identity-store.a") == 0) return 0;
    if (name != NULL && strcmp(name, "identity-store.b") == 0) return 1;
    return -1;
}

static enum aurora_identity_protected_state_read_result fake_read(
    void *context,
    const char *name,
    uint8_t *buffer,
    size_t capacity,
    size_t *out_size) {
    struct fake_transport *transport = context;
    int slot = name_to_slot(name);

    if (transport == NULL || slot < 0 || buffer == NULL || out_size == NULL) {
        return AURORA_IDENTITY_PROTECTED_STATE_READ_ERROR;
    }

    struct fake_record *record = &transport->slots[(size_t)slot];
    if (!record->present) {
        *out_size = 0u;
        return AURORA_IDENTITY_PROTECTED_STATE_READ_NOT_FOUND;
    }
    if (record->size == 0u || record->size > capacity) {
        *out_size = 0u;
        return AURORA_IDENTITY_PROTECTED_STATE_READ_ERROR;
    }

    memcpy(buffer, record->data, record->size);
    *out_size = record->size;
    return AURORA_IDENTITY_PROTECTED_STATE_READ_OK;
}

static bool fake_replace(
    void *context,
    const char *name,
    const uint8_t *buffer,
    size_t size) {
    struct fake_transport *transport = context;
    int slot = name_to_slot(name);

    if (transport == NULL || transport->fail_replace || slot < 0 ||
        buffer == NULL || size == 0u ||
        size > AURORA_IDENTITY_STORE_MAX_IMAGE_SIZE) {
        return false;
    }

    struct fake_record *record = &transport->slots[(size_t)slot];
    memcpy(record->data, buffer, size);
    record->size = size;
    record->present = true;
    return true;
}

int main(void) {
    struct fake_transport backend;
    memset(&backend, 0, sizeof(backend));

    struct aurora_identity_protected_state_transport_ops transport;
    memset(&transport, 0, sizeof(transport));
    transport.context = &backend;
    transport.read_record = fake_read;
    transport.replace_record_durable = fake_replace;

    struct aurora_identity_persistent_store_protected_state adapter;
    expect(
        aurora_identity_persistent_store_protected_state_init(
            &adapter, &transport),
        "adapter init should accept read+replace transport");

    struct aurora_identity_persistent_io_ops io =
        aurora_identity_persistent_store_protected_state_io_ops(&adapter);

    uint8_t readback[64];
    size_t read_size = 123u;
    expect(
        io.read_slot(io.context, 0u, readback, sizeof(readback), &read_size) ==
            AURORA_IDENTITY_PERSISTENT_IO_NOT_FOUND &&
        read_size == 0u,
        "empty slot should map to NOT_FOUND");

    static const uint8_t first[] = { 0x41u, 0x55u, 0x52u, 0x31u };
    static const uint8_t second[] = { 0x41u, 0x55u, 0x52u, 0x32u, 0x21u };
    static const uint8_t other[] = { 0x42u, 0x2Du, 0x53u, 0x4Cu, 0x4Fu, 0x54u };

    expect(
        io.write_slot_atomic(io.context, 0u, first, sizeof(first)),
        "first slot publication should succeed");
    expect(
        io.read_slot(io.context, 0u, readback, sizeof(readback), &read_size) ==
            AURORA_IDENTITY_PERSISTENT_IO_OK &&
        read_size == sizeof(first) &&
        memcmp(readback, first, sizeof(first)) == 0,
        "first slot image should round-trip");

    expect(
        io.write_slot_atomic(io.context, 0u, second, sizeof(second)),
        "existing inactive slot should be durably replaceable");
    expect(
        io.read_slot(io.context, 0u, readback, sizeof(readback), &read_size) ==
            AURORA_IDENTITY_PERSISTENT_IO_OK &&
        read_size == sizeof(second) &&
        memcmp(readback, second, sizeof(second)) == 0,
        "replacement slot image should be visible in full");

    expect(
        io.write_slot_atomic(io.context, 1u, other, sizeof(other)),
        "second slot publication should succeed");
    expect(
        io.read_slot(io.context, 1u, readback, sizeof(readback), &read_size) ==
            AURORA_IDENTITY_PERSISTENT_IO_OK &&
        read_size == sizeof(other) &&
        memcmp(readback, other, sizeof(other)) == 0,
        "second slot should remain independent");

    backend.fail_replace = true;
    expect(
        !io.write_slot_atomic(io.context, 0u, first, sizeof(first)),
        "transport replacement failure must propagate");
    backend.fail_replace = false;
    expect(
        io.read_slot(io.context, 0u, readback, sizeof(readback), &read_size) ==
            AURORA_IDENTITY_PERSISTENT_IO_OK &&
        read_size == sizeof(second) &&
        memcmp(readback, second, sizeof(second)) == 0,
        "failed replacement must not corrupt the previous visible test image");

    expect(
        io.read_slot(io.context, 2u, readback, sizeof(readback), &read_size) ==
            AURORA_IDENTITY_PERSISTENT_IO_ERROR,
        "invalid slot must fail closed");
    expect(
        !io.write_slot_atomic(io.context, 2u, first, sizeof(first)),
        "invalid slot write must fail closed");

    struct aurora_identity_protected_state_transport_ops incomplete = transport;
    incomplete.replace_record_durable = NULL;
    expect(
        !aurora_identity_persistent_store_protected_state_init(
            &adapter, &incomplete),
        "adapter init must reject transport without durable replacement");

    puts("Aurora Identity Protected State persistent-store tests passed");
    return 0;
}
