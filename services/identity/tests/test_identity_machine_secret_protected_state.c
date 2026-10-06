#include "aurora/identity/machine_secret.h"
#include "aurora/identity/machine_secret_protected_state.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RECORD_SIZE 84u

struct memory_transport {
    bool present[2];
    uint8_t data[2][RECORD_SIZE];
    size_t size[2];
};

struct fake_random {
    uint8_t next;
    unsigned calls;
};

static int name_to_index(const char *name) {
    if (strcmp(name, "machine-secret.a") == 0) return 0;
    if (strcmp(name, "machine-secret.b") == 0) return 1;
    return -1;
}

static enum aurora_identity_protected_state_read_result read_record(
    void *context,
    const char *name,
    uint8_t *buffer,
    size_t capacity,
    size_t *out_size) {
    struct memory_transport *transport = context;
    int index = name_to_index(name);
    if (transport == NULL || buffer == NULL || out_size == NULL || index < 0) {
        return AURORA_IDENTITY_PROTECTED_STATE_READ_ERROR;
    }
    if (!transport->present[index]) {
        return AURORA_IDENTITY_PROTECTED_STATE_READ_NOT_FOUND;
    }
    if (transport->size[index] > capacity) {
        return AURORA_IDENTITY_PROTECTED_STATE_READ_ERROR;
    }
    memcpy(buffer, transport->data[index], transport->size[index]);
    *out_size = transport->size[index];
    return AURORA_IDENTITY_PROTECTED_STATE_READ_OK;
}

static enum aurora_identity_protected_state_create_result create_record_once_durable(
    void *context,
    const char *name,
    const uint8_t *buffer,
    size_t size) {
    struct memory_transport *transport = context;
    int index = name_to_index(name);
    if (transport == NULL || buffer == NULL || index < 0 || size != RECORD_SIZE) {
        return AURORA_IDENTITY_PROTECTED_STATE_CREATE_ERROR;
    }
    if (transport->present[index]) {
        return AURORA_IDENTITY_PROTECTED_STATE_CREATE_EXISTS;
    }
    memcpy(transport->data[index], buffer, size);
    transport->size[index] = size;
    transport->present[index] = true;
    return AURORA_IDENTITY_PROTECTED_STATE_CREATE_OK;
}

static bool fill_random(void *context, uint8_t *buffer, size_t size) {
    struct fake_random *random = context;
    ++random->calls;
    for (size_t i = 0u; i < size; ++i) {
        buffer[i] = random->next++;
        if (buffer[i] == 0u) buffer[i] = random->next++;
    }
    return true;
}

static struct aurora_identity_machine_secret_store_ops make_store(
    struct memory_transport *transport,
    struct aurora_identity_machine_secret_protected_state_store *store) {
    const struct aurora_identity_protected_state_transport_ops ops = {
        .context = transport,
        .read_record = read_record,
        .create_record_once_durable = create_record_once_durable
    };
    assert(aurora_identity_machine_secret_protected_state_store_init(store, &ops));
    return aurora_identity_machine_secret_protected_state_store_ops(store);
}

static void test_provision_and_reopen(void) {
    struct memory_transport transport;
    struct aurora_identity_machine_secret_protected_state_store adapter;
    struct fake_random random = { .next = 1u };
    memset(&transport, 0, sizeof(transport));

    struct aurora_identity_machine_secret_core core = {
        .random = { .context = &random, .fill_random = fill_random },
        .store = make_store(&transport, &adapter)
    };

    struct aurora_identity_machine_secret first;
    assert(aurora_identity_machine_secret_load_or_provision(&core, &first) ==
        AURORA_IDENTITY_MACHINE_SECRET_OK);
    assert(random.calls == 1u);
    assert(transport.present[0] && transport.present[1]);

    struct aurora_identity_machine_secret_protected_state_store reopened_adapter;
    struct aurora_identity_machine_secret_store_ops reopened =
        make_store(&transport, &reopened_adapter);
    struct aurora_identity_machine_secret second;
    assert(aurora_identity_machine_secret_load(&reopened, &second) ==
        AURORA_IDENTITY_MACHINE_SECRET_OK);
    assert(memcmp(first.bytes, second.bytes, sizeof(first.bytes)) == 0);

    aurora_identity_machine_secret_clear(&first);
    aurora_identity_machine_secret_clear(&second);
}

static void test_create_once_conflict_mapping(void) {
    struct memory_transport transport;
    struct aurora_identity_machine_secret_protected_state_store adapter;
    memset(&transport, 0, sizeof(transport));
    transport.present[0] = true;
    transport.size[0] = RECORD_SIZE;

    struct aurora_identity_machine_secret_store_ops store =
        make_store(&transport, &adapter);
    struct aurora_identity_machine_secret_record record;
    memset(&record, 0, sizeof(record));

    assert(store.publish_replica(store.context, 0u, &record) ==
        AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_CONFLICT);
}

static void test_corruption_fails_closed_without_rng(void) {
    struct memory_transport transport;
    struct aurora_identity_machine_secret_protected_state_store adapter;
    struct fake_random random = { .next = 7u };
    memset(&transport, 0, sizeof(transport));

    struct aurora_identity_machine_secret_core core = {
        .random = { .context = &random, .fill_random = fill_random },
        .store = make_store(&transport, &adapter)
    };
    struct aurora_identity_machine_secret secret;
    assert(aurora_identity_machine_secret_load_or_provision(&core, &secret) ==
        AURORA_IDENTITY_MACHINE_SECRET_OK);
    unsigned calls = random.calls;

    transport.data[0][0] ^= 0xFFu;
    transport.data[1][0] ^= 0xFFu;

    struct aurora_identity_machine_secret output;
    memset(&output, 0xA5, sizeof(output));
    assert(aurora_identity_machine_secret_load_or_provision(&core, &output) ==
        AURORA_IDENTITY_MACHINE_SECRET_CORRUPT);
    assert(random.calls == calls);
    assert(aurora_identity_machine_secret_is_zero(&output));
    aurora_identity_machine_secret_clear(&secret);
}

int main(void) {
    test_provision_and_reopen();
    test_create_once_conflict_mapping();
    test_corruption_fails_closed_without_rng();
    puts("Aurora Identity Protected State adapter tests: PASS");
    return 0;
}
