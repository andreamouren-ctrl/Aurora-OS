#include "aurora/identity/audit_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c,m) do { if (!(c)) { fprintf(stderr,"FAIL: %s\n",m); exit(1); } } while (0)

struct fake_io {
    uint8_t slots[AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT]
        [AURORA_IDENTITY_AUDIT_STORE_MAX_IMAGE_SIZE];
    size_t sizes[AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT];
    bool present[AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT];
    bool fail_read;
    bool fail_write;
};

static enum aurora_identity_audit_io_result read_slot(
    void *opaque, uint32_t slot, uint8_t *buffer, size_t capacity, size_t *out_size
) {
    struct fake_io *io = opaque;
    if (io == NULL || slot >= AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT ||
        buffer == NULL || out_size == NULL || io->fail_read)
        return AURORA_IDENTITY_AUDIT_IO_ERROR;
    if (!io->present[slot]) return AURORA_IDENTITY_AUDIT_IO_NOT_FOUND;
    if (io->sizes[slot] > capacity) return AURORA_IDENTITY_AUDIT_IO_ERROR;
    memcpy(buffer, io->slots[slot], io->sizes[slot]);
    *out_size = io->sizes[slot];
    return AURORA_IDENTITY_AUDIT_IO_OK;
}

static bool write_slot(
    void *opaque, uint32_t slot, const uint8_t *buffer, size_t size
) {
    struct fake_io *io = opaque;
    if (io == NULL || slot >= AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT ||
        buffer == NULL || size == 0u ||
        size > AURORA_IDENTITY_AUDIT_STORE_MAX_IMAGE_SIZE || io->fail_write)
        return false;
    memcpy(io->slots[slot], buffer, size);
    io->sizes[slot] = size;
    io->present[slot] = true;
    return true;
}

static struct aurora_identity_audit_io_ops ops(struct fake_io *io) {
    struct aurora_identity_audit_io_ops result;
    memset(&result, 0, sizeof(result));
    result.context = io;
    result.read_slot = read_slot;
    result.write_slot_atomic = write_slot;
    return result;
}

static void fill_user(struct aurora_identity_user_id *user, uint8_t seed) {
    memset(user, 0, sizeof(*user));
    for (size_t i = 0u; i < sizeof(user->bytes); ++i)
        user->bytes[i] = (uint8_t)(seed + i + 1u);
}

static void test_empty_append_reopen(void) {
    struct fake_io io;
    struct aurora_identity_audit_store store, reopened;
    struct aurora_identity_audit_record record;
    struct aurora_identity_user_id user;
    memset(&io, 0, sizeof(io));
    fill_user(&user, 0x20u);
    struct aurora_identity_audit_io_ops io_ops = ops(&io);

    CHECK(aurora_identity_audit_store_open(&store, &io_ops) ==
        AURORA_IDENTITY_AUDIT_OPEN_EMPTY, "empty open");
    CHECK(aurora_identity_audit_store_append(
        &store, AURORA_IDENTITY_AUDIT_EVENT_AUTH_SUCCESS,
        AURORA_IDENTITY_AUDIT_OUTCOME_SUCCESS, 0u, 100u, 3u, &user),
        "durable append");
    CHECK(aurora_identity_audit_store_generation(&store) == 1u, "generation one");
    CHECK(aurora_identity_audit_store_count(&store) == 1u, "count one");

    CHECK(aurora_identity_audit_store_open(&reopened, &io_ops) ==
        AURORA_IDENTITY_AUDIT_OPEN_OK, "reopen");
    CHECK(aurora_identity_audit_store_generation(&reopened) == 1u, "reopen generation");
    CHECK(aurora_identity_audit_store_get_oldest(&reopened, 0u, &record), "reopen record");
    CHECK(record.event_type == AURORA_IDENTITY_AUDIT_EVENT_AUTH_SUCCESS, "reopen event");
    CHECK(record.sequence == 1u, "reopen sequence");
}

static void test_write_failure_preserves_live_state(void) {
    struct fake_io io;
    struct aurora_identity_audit_store store;
    memset(&io, 0, sizeof(io));
    struct aurora_identity_audit_io_ops io_ops = ops(&io);
    CHECK(aurora_identity_audit_store_open(&store, &io_ops) ==
        AURORA_IDENTITY_AUDIT_OPEN_EMPTY, "failure empty open");
    CHECK(aurora_identity_audit_store_append(
        &store, AURORA_IDENTITY_AUDIT_EVENT_AUTH_FAILURE,
        AURORA_IDENTITY_AUDIT_OUTCOME_FAILURE, 1u, 10u, 0u, NULL),
        "first append");

    io.fail_write = true;
    CHECK(!aurora_identity_audit_store_append(
        &store, AURORA_IDENTITY_AUDIT_EVENT_REAUTH_FAILURE,
        AURORA_IDENTITY_AUDIT_OUTCOME_FAILURE, 2u, 20u, 1u, NULL),
        "write failure rejected");
    CHECK(aurora_identity_audit_store_generation(&store) == 1u,
        "failed write generation unchanged");
    CHECK(aurora_identity_audit_store_count(&store) == 1u,
        "failed write count unchanged");
}

static void test_corrupt_newest_recovers_previous(void) {
    struct fake_io io;
    struct aurora_identity_audit_store store, reopened;
    struct aurora_identity_audit_record record;
    memset(&io, 0, sizeof(io));
    struct aurora_identity_audit_io_ops io_ops = ops(&io);
    CHECK(aurora_identity_audit_store_open(&store, &io_ops) ==
        AURORA_IDENTITY_AUDIT_OPEN_EMPTY, "recovery empty open");

    CHECK(aurora_identity_audit_store_append(
        &store, AURORA_IDENTITY_AUDIT_EVENT_AUTH_SUCCESS,
        AURORA_IDENTITY_AUDIT_OUTCOME_SUCCESS, 1u, 10u, 1u, NULL),
        "generation one append");
    CHECK(aurora_identity_audit_store_append(
        &store, AURORA_IDENTITY_AUDIT_EVENT_REAUTH_SUCCESS,
        AURORA_IDENTITY_AUDIT_OUTCOME_SUCCESS, 2u, 20u, 1u, NULL),
        "generation two append");

    uint32_t newest = store.active_slot;
    CHECK(io.present[newest] && io.sizes[newest] > 48u, "newest exists");
    io.slots[newest][48u] ^= 0x5au;

    CHECK(aurora_identity_audit_store_open(&reopened, &io_ops) ==
        AURORA_IDENTITY_AUDIT_OPEN_OK, "recover previous slot");
    CHECK(aurora_identity_audit_store_generation(&reopened) == 1u,
        "previous generation selected");
    CHECK(aurora_identity_audit_store_count(&reopened) == 1u,
        "previous count selected");
    CHECK(aurora_identity_audit_store_get_oldest(&reopened, 0u, &record),
        "previous record readable");
    CHECK(record.event_type == AURORA_IDENTITY_AUDIT_EVENT_AUTH_SUCCESS,
        "previous event preserved");
}

static void test_all_corrupt_fails_closed(void) {
    struct fake_io io;
    struct aurora_identity_audit_store store, reopened;
    memset(&io, 0, sizeof(io));
    struct aurora_identity_audit_io_ops io_ops = ops(&io);
    CHECK(aurora_identity_audit_store_open(&store, &io_ops) ==
        AURORA_IDENTITY_AUDIT_OPEN_EMPTY, "corrupt empty open");
    CHECK(aurora_identity_audit_store_append(
        &store, AURORA_IDENTITY_AUDIT_EVENT_AUTH_SUCCESS,
        AURORA_IDENTITY_AUDIT_OUTCOME_SUCCESS, 0u, 1u, 1u, NULL), "append one");
    CHECK(aurora_identity_audit_store_append(
        &store, AURORA_IDENTITY_AUDIT_EVENT_SESSION_STARTED,
        AURORA_IDENTITY_AUDIT_OUTCOME_SUCCESS, 0u, 2u, 1u, NULL), "append two");
    for (uint32_t i = 0u; i < AURORA_IDENTITY_AUDIT_STORE_SLOT_COUNT; ++i) {
        CHECK(io.present[i], "both slots present");
        io.slots[i][0] ^= 0xffu;
    }
    CHECK(aurora_identity_audit_store_open(&reopened, &io_ops) ==
        AURORA_IDENTITY_AUDIT_OPEN_CORRUPT, "all corrupt fail closed");
}

static void test_wrap_survives_reopen(void) {
    struct fake_io io;
    struct aurora_identity_audit_store store, reopened;
    struct aurora_identity_audit_record record;
    memset(&io, 0, sizeof(io));
    struct aurora_identity_audit_io_ops io_ops = ops(&io);
    CHECK(aurora_identity_audit_store_open(&store, &io_ops) ==
        AURORA_IDENTITY_AUDIT_OPEN_EMPTY, "wrap empty open");

    for (size_t i = 0u; i < AURORA_IDENTITY_AUDIT_CAPACITY + 3u; ++i) {
        CHECK(aurora_identity_audit_store_append(
            &store, AURORA_IDENTITY_AUDIT_EVENT_SESSION_STARTED,
            AURORA_IDENTITY_AUDIT_OUTCOME_SUCCESS, (uint32_t)i,
            (uint64_t)i, (uint64_t)(i + 1u), NULL), "wrap durable append");
    }
    CHECK(aurora_identity_audit_store_open(&reopened, &io_ops) ==
        AURORA_IDENTITY_AUDIT_OPEN_OK, "wrap reopen");
    CHECK(aurora_identity_audit_store_count(&reopened) ==
        AURORA_IDENTITY_AUDIT_CAPACITY, "wrap count");
    CHECK(aurora_identity_audit_store_get_oldest(&reopened, 0u, &record),
        "wrap oldest");
    CHECK(record.sequence == 4u, "wrap oldest sequence");
}

int main(void) {
    test_empty_append_reopen();
    test_write_failure_preserves_live_state();
    test_corrupt_newest_recovers_previous();
    test_all_corrupt_fails_closed();
    test_wrap_survives_reopen();
    puts("Aurora Identity durable audit store tests: PASS");
    return 0;
}
