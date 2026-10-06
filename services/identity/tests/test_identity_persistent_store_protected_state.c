#include "aurora/identity/persistent_store.h"
#include "aurora/identity/persistent_store_protected_state.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct memory_transport {
    bool present[AURORA_IDENTITY_STORE_SLOT_COUNT];
    uint8_t data[AURORA_IDENTITY_STORE_SLOT_COUNT][AURORA_IDENTITY_STORE_MAX_IMAGE_SIZE];
    size_t size[AURORA_IDENTITY_STORE_SLOT_COUNT];
    bool fail_replace;
    unsigned replace_calls;
};

static int slot_index(const char *name) {
    if (strcmp(name, "identity-store.a") == 0) return 0;
    if (strcmp(name, "identity-store.b") == 0) return 1;
    return -1;
}

static enum aurora_identity_protected_state_read_result read_record(
    void *context,
    const char *name,
    uint8_t *buffer,
    size_t capacity,
    size_t *out_size) {
    struct memory_transport *transport = context;
    int index = slot_index(name);

    if (transport == NULL || buffer == NULL || out_size == NULL || index < 0) {
        return AURORA_IDENTITY_PROTECTED_STATE_READ_ERROR;
    }
    if (!transport->present[index]) {
        *out_size = 0u;
        return AURORA_IDENTITY_PROTECTED_STATE_READ_NOT_FOUND;
    }
    if (transport->size[index] == 0u || transport->size[index] > capacity) {
        return AURORA_IDENTITY_PROTECTED_STATE_READ_ERROR;
    }

    memcpy(buffer, transport->data[index], transport->size[index]);
    *out_size = transport->size[index];
    return AURORA_IDENTITY_PROTECTED_STATE_READ_OK;
}

static enum aurora_identity_protected_state_replace_result replace_record_durable(
    void *context,
    const char *name,
    const uint8_t *buffer,
    size_t size) {
    struct memory_transport *transport = context;
    int index = slot_index(name);

    if (transport == NULL || buffer == NULL || index < 0 || size == 0u ||
        size > AURORA_IDENTITY_STORE_MAX_IMAGE_SIZE) {
        return AURORA_IDENTITY_PROTECTED_STATE_REPLACE_ERROR;
    }

    ++transport->replace_calls;
    if (transport->fail_replace) {
        return AURORA_IDENTITY_PROTECTED_STATE_REPLACE_ERROR;
    }

    memcpy(transport->data[index], buffer, size);
    transport->size[index] = size;
    transport->present[index] = true;
    return AURORA_IDENTITY_PROTECTED_STATE_REPLACE_OK;
}

static void fill_bytes(uint8_t *buffer, size_t size, uint8_t seed) {
    for (size_t i = 0u; i < size; ++i) {
        buffer[i] = (uint8_t)(seed + (uint8_t)i);
    }
}

static struct aurora_identity_record make_identity(uint8_t seed) {
    struct aurora_identity_record record;
    memset(&record, 0, sizeof(record));
    fill_bytes(record.user_id.bytes, sizeof(record.user_id.bytes), seed);
    record.status = AURORA_IDENTITY_RECORD_ACTIVE;
    record.role = AURORA_IDENTITY_ROLE_UNASSIGNED;
    record.policy_version = 1u;
    record.record_version = 1u;
    return record;
}

static struct aurora_identity_key_record make_key(
    const struct aurora_identity_record *identity,
    uint8_t seed) {
    struct aurora_identity_key_record key;
    memset(&key, 0, sizeof(key));
    fill_bytes(key.credential_id.bytes, sizeof(key.credential_id.bytes), seed);
    key.user_id = identity->user_id;
    fill_bytes(key.lookup_tag, sizeof(key.lookup_tag), (uint8_t)(seed + 9u));
    key.kdf.algorithm = AURORA_IDENTITY_KDF_ARGON2ID;
    key.kdf.parameters_version = 1u;
    key.kdf.memory_kib = 65536u;
    key.kdf.time_cost = 3u;
    key.kdf.parallelism = 1u;
    key.salt_size = 16u;
    fill_bytes(key.salt, key.salt_size, (uint8_t)(seed + 17u));
    key.verifier_size = 32u;
    fill_bytes(key.verifier, key.verifier_size, (uint8_t)(seed + 29u));
    key.status = AURORA_IDENTITY_RECORD_ACTIVE;
    return key;
}

static struct aurora_identity_persistent_io_ops make_io(
    struct memory_transport *transport,
    struct aurora_identity_persistent_protected_state_store *adapter) {
    const struct aurora_identity_protected_state_transport_ops transport_ops = {
        .context = transport,
        .read_record = read_record,
        .create_record_once_durable = NULL,
        .replace_record_durable = replace_record_durable
    };

    assert(aurora_identity_persistent_protected_state_store_init(
        adapter, &transport_ops));
    return aurora_identity_persistent_protected_state_io_ops(adapter);
}

static void test_dual_slot_roundtrip(void) {
    struct memory_transport transport;
    struct aurora_identity_persistent_protected_state_store adapter;
    struct aurora_identity_persistent_store store;
    struct aurora_identity_persistent_store reopened;
    memset(&transport, 0, sizeof(transport));

    struct aurora_identity_persistent_io_ops io = make_io(&transport, &adapter);
    assert(aurora_identity_persistent_store_open(&store, &io) ==
        AURORA_IDENTITY_PERSISTENT_OPEN_EMPTY);

    struct aurora_identity_record identity = make_identity(0x11u);
    struct aurora_identity_key_record key = make_key(&identity, 0x41u);
    struct aurora_identity_store_ops core =
        aurora_identity_persistent_store_core_ops(&store);

    assert(core.create_identity_with_key(
        core.context, &identity, &key) == AURORA_IDENTITY_STORE_CREATE_OK);
    assert(aurora_identity_persistent_store_generation(&store) == 1u);
    assert(transport.replace_calls == 1u);

    assert(aurora_identity_persistent_store_open(&reopened, &io) ==
        AURORA_IDENTITY_PERSISTENT_OPEN_OK);

    struct aurora_identity_record found_identity;
    memset(&found_identity, 0, sizeof(found_identity));
    assert(aurora_identity_persistent_store_find_identity(
        &reopened, &identity.user_id, &found_identity) == AURORA_IDENTITY_STORE_OK);
    assert(found_identity.role == AURORA_IDENTITY_ROLE_ADMINISTRATOR);

    core = aurora_identity_persistent_store_core_ops(&reopened);
    struct aurora_identity_key_record found_key;
    memset(&found_key, 0, sizeof(found_key));
    assert(core.find_key_record_by_lookup_tag(
        core.context, key.lookup_tag, &found_key) == AURORA_IDENTITY_STORE_OK);
    assert(memcmp(found_key.credential_id.bytes,
                  key.credential_id.bytes,
                  sizeof(key.credential_id.bytes)) == 0);

    assert(core.store_failure_state(core.context, &identity.user_id, 3u, 99999u));
    assert(aurora_identity_persistent_store_generation(&reopened) == 2u);
    assert(transport.present[0] && transport.present[1]);

    assert(aurora_identity_persistent_store_open(&store, &io) ==
        AURORA_IDENTITY_PERSISTENT_OPEN_OK);
    core = aurora_identity_persistent_store_core_ops(&store);
    memset(&found_key, 0, sizeof(found_key));
    assert(core.find_key_record_by_lookup_tag(
        core.context, key.lookup_tag, &found_key) == AURORA_IDENTITY_STORE_OK);
    assert(found_key.failed_attempts == 3u);
    assert(found_key.throttle_until_ms == 0u);
}

static void test_replace_failure_is_propagated(void) {
    struct memory_transport transport;
    struct aurora_identity_persistent_protected_state_store adapter;
    memset(&transport, 0, sizeof(transport));

    struct aurora_identity_persistent_io_ops io = make_io(&transport, &adapter);
    uint8_t first[32];
    uint8_t second[32];
    memset(first, 0x11, sizeof(first));
    memset(second, 0x22, sizeof(second));

    assert(io.write_slot_atomic(io.context, 0u, first, sizeof(first)));
    assert(transport.present[0]);
    assert(transport.size[0] == sizeof(first));

    transport.fail_replace = true;
    assert(!io.write_slot_atomic(io.context, 0u, second, sizeof(second)));
    assert(transport.size[0] == sizeof(first));
    assert(memcmp(transport.data[0], first, sizeof(first)) == 0);

    assert(!io.write_slot_atomic(
        io.context,
        AURORA_IDENTITY_STORE_SLOT_COUNT,
        second,
        sizeof(second)));
}

int main(void) {
    test_dual_slot_roundtrip();
    test_replace_failure_is_propagated();
    puts("Aurora Identity Protected State persistent-store tests: PASS");
    return 0;
}
