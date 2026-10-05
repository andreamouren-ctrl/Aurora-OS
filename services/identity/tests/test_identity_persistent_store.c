#define _POSIX_C_SOURCE 200809L

#include "aurora/identity/persistent_store.h"
#include "aurora/identity/persistent_store_posix.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (0)

static void fill_bytes(uint8_t *buffer, size_t size, uint8_t seed) {
    size_t i;

    for (i = 0u; i < size; ++i) {
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
    uint8_t credential_seed,
    uint8_t lookup_seed) {
    struct aurora_identity_key_record key;

    memset(&key, 0, sizeof(key));
    fill_bytes(key.credential_id.bytes, sizeof(key.credential_id.bytes), credential_seed);
    key.user_id = identity->user_id;
    fill_bytes(key.lookup_tag, sizeof(key.lookup_tag), lookup_seed);
    key.kdf.algorithm = AURORA_IDENTITY_KDF_ARGON2ID;
    key.kdf.parameters_version = 1u;
    key.kdf.memory_kib = 65536u;
    key.kdf.time_cost = 3u;
    key.kdf.parallelism = 1u;
    key.salt_size = 16u;
    fill_bytes(key.salt, key.salt_size, (uint8_t)(credential_seed + 3u));
    key.verifier_size = 32u;
    fill_bytes(key.verifier, key.verifier_size, (uint8_t)(credential_seed + 7u));
    key.status = AURORA_IDENTITY_RECORD_ACTIVE;
    return key;
}

static bool key_matches(
    const struct aurora_identity_key_record *record,
    const struct aurora_identity_key_record *expected) {
    return memcmp(
               record->credential_id.bytes,
               expected->credential_id.bytes,
               AURORA_IDENTITY_CREDENTIAL_ID_SIZE) == 0 &&
           memcmp(
               record->user_id.bytes,
               expected->user_id.bytes,
               AURORA_IDENTITY_USER_ID_SIZE) == 0 &&
           memcmp(
               record->lookup_tag,
               expected->lookup_tag,
               AURORA_IDENTITY_LOOKUP_TAG_SIZE) == 0 &&
           record->failed_attempts == expected->failed_attempts &&
           record->throttle_until_ms == expected->throttle_until_ms;
}

static bool path_for_slot(
    const char *base_path,
    uint32_t slot,
    char *out,
    size_t capacity) {
    int written = snprintf(out, capacity, "%s.slot%u", base_path, (unsigned int)slot);
    return written >= 0 && (size_t)written < capacity;
}

static bool overwrite_with_bad_data(const char *path) {
    const uint8_t bad[] = {0x42u, 0x41u, 0x44u};
    int fd = open(path, O_WRONLY | O_TRUNC);
    ssize_t written;

    if (fd < 0) {
        return false;
    }

    written = write(fd, bad, sizeof(bad));
    if (written != (ssize_t)sizeof(bad) || fsync(fd) != 0) {
        (void)close(fd);
        return false;
    }

    return close(fd) == 0;
}

struct failing_io {
    struct aurora_identity_persistent_io_ops inner;
    bool fail_writes;
};

static enum aurora_identity_persistent_io_result failing_read(
    void *context,
    uint32_t slot,
    uint8_t *buffer,
    size_t capacity,
    size_t *out_size) {
    struct failing_io *wrapper = (struct failing_io *)context;

    return wrapper->inner.read_slot(
        wrapper->inner.context,
        slot,
        buffer,
        capacity,
        out_size);
}

static bool failing_write(
    void *context,
    uint32_t slot,
    const uint8_t *buffer,
    size_t size) {
    struct failing_io *wrapper = (struct failing_io *)context;

    if (wrapper->fail_writes) {
        return false;
    }

    return wrapper->inner.write_slot_atomic(
        wrapper->inner.context,
        slot,
        buffer,
        size);
}

static bool expect_role(
    const struct aurora_identity_persistent_store *store,
    const struct aurora_identity_user_id *user_id,
    enum aurora_identity_role expected_role) {
    struct aurora_identity_record found;

    memset(&found, 0, sizeof(found));
    CHECK(aurora_identity_persistent_store_find_identity(
              store,
              user_id,
              &found) == AURORA_IDENTITY_STORE_OK);
    CHECK(found.role == expected_role);
    return true;
}

static bool run_persistence_test(void) {
    char template_path[] = "/tmp/aurora-identity-store-XXXXXX";
    char *directory;
    char base_path[512];
    char slot_path[2][544];
    struct aurora_identity_posix_store posix;
    struct aurora_identity_persistent_io_ops io;
    struct aurora_identity_persistent_store store;
    struct aurora_identity_persistent_store reopened;
    struct aurora_identity_store_ops core_ops;
    struct aurora_identity_rotation_store_ops rotation_ops;
    struct aurora_identity_record identity1;
    struct aurora_identity_record identity2;
    struct aurora_identity_record forged_admin;
    struct aurora_identity_key_record key1;
    struct aurora_identity_key_record key2;
    struct aurora_identity_key_record forged_key;
    struct aurora_identity_key_record rotated;
    struct aurora_identity_key_record found;
    struct stat st;
    uint64_t generation_before_failure;
    uint64_t generation_before_rearm;
    uint32_t latest_slot;
    enum aurora_identity_persistent_open_result open_result;

    directory = mkdtemp(template_path);
    CHECK(directory != NULL);
    CHECK(snprintf(base_path, sizeof(base_path), "%s/identity.db", directory) > 0);
    CHECK(path_for_slot(base_path, 0u, slot_path[0], sizeof(slot_path[0])));
    CHECK(path_for_slot(base_path, 1u, slot_path[1], sizeof(slot_path[1])));

    CHECK(aurora_identity_posix_store_init(&posix, base_path));
    io = aurora_identity_posix_store_io(&posix);

    open_result = aurora_identity_persistent_store_open(&store, &io);
    CHECK(open_result == AURORA_IDENTITY_PERSISTENT_OPEN_EMPTY);
    CHECK(aurora_identity_persistent_store_generation(&store) == 0u);

    /* First committed persistent identity becomes administrator atomically. */
    identity1 = make_identity(0x10u);
    key1 = make_key(&identity1, 0x30u, 0x50u);
    core_ops = aurora_identity_persistent_store_core_ops(&store);
    CHECK(core_ops.create_identity_with_key(
              core_ops.context,
              &identity1,
              &key1) == AURORA_IDENTITY_STORE_CREATE_OK);
    CHECK(aurora_identity_persistent_store_generation(&store) == 1u);
    CHECK(aurora_identity_persistent_store_identity_count(&store) == 1u);
    CHECK(aurora_identity_persistent_store_key_record_count(&store) == 1u);
    CHECK(expect_role(
        &store,
        &identity1.user_id,
        AURORA_IDENTITY_ROLE_ADMINISTRATOR));

    CHECK(aurora_identity_persistent_store_open(&reopened, &io) ==
          AURORA_IDENTITY_PERSISTENT_OPEN_OK);
    CHECK(expect_role(
        &reopened,
        &identity1.user_id,
        AURORA_IDENTITY_ROLE_ADMINISTRATOR));

    core_ops = aurora_identity_persistent_store_core_ops(&reopened);
    memset(&found, 0, sizeof(found));
    CHECK(core_ops.find_key_record_by_lookup_tag(
              core_ops.context,
              key1.lookup_tag,
              &found) == AURORA_IDENTITY_STORE_OK);
    CHECK(key_matches(&found, &key1));

    /* failed_attempts is durable; the current monotonic deadline is not. */
    CHECK(core_ops.store_failure_state(
        core_ops.context,
        &identity1.user_id,
        4u,
        123456u));
    CHECK(aurora_identity_persistent_store_generation(&reopened) == 2u);
    memset(&found, 0, sizeof(found));
    CHECK(core_ops.find_key_record_by_lookup_tag(
              core_ops.context,
              key1.lookup_tag,
              &found) == AURORA_IDENTITY_STORE_OK);
    CHECK(found.failed_attempts == 4u);
    CHECK(found.throttle_until_ms == 123456u);

    CHECK(aurora_identity_persistent_store_open(&store, &io) ==
          AURORA_IDENTITY_PERSISTENT_OPEN_OK);
    core_ops = aurora_identity_persistent_store_core_ops(&store);
    memset(&found, 0, sizeof(found));
    CHECK(core_ops.find_key_record_by_lookup_tag(
              core_ops.context,
              key1.lookup_tag,
              &found) == AURORA_IDENTITY_STORE_OK);
    CHECK(found.failed_attempts == 4u);
    CHECK(found.throttle_until_ms == 0u);

    /* Re-arming the same durable failure count is volatile: no disk generation. */
    generation_before_rearm = aurora_identity_persistent_store_generation(&store);
    CHECK(core_ops.store_failure_state(
        core_ops.context,
        &identity1.user_id,
        4u,
        777u));
    CHECK(aurora_identity_persistent_store_generation(&store) == generation_before_rearm);
    memset(&found, 0, sizeof(found));
    CHECK(core_ops.find_key_record_by_lookup_tag(
              core_ops.context,
              key1.lookup_tag,
              &found) == AURORA_IDENTITY_STORE_OK);
    CHECK(found.failed_attempts == 4u);
    CHECK(found.throttle_until_ms == 777u);

    rotated = make_key(&identity1, 0x70u, 0x90u);
    rotation_ops = aurora_identity_persistent_store_rotation_ops(&store);
    CHECK(rotation_ops.replace_key_credential(
              rotation_ops.context,
              &identity1.user_id,
              &key1.credential_id,
              &rotated) == AURORA_IDENTITY_ROTATION_STORE_OK);
    CHECK(aurora_identity_persistent_store_generation(&store) == 3u);

    CHECK(aurora_identity_persistent_store_open(&reopened, &io) ==
          AURORA_IDENTITY_PERSISTENT_OPEN_OK);
    core_ops = aurora_identity_persistent_store_core_ops(&reopened);
    CHECK(core_ops.find_key_record_by_lookup_tag(
              core_ops.context,
              key1.lookup_tag,
              &found) == AURORA_IDENTITY_STORE_NOT_FOUND);
    CHECK(core_ops.find_key_record_by_lookup_tag(
              core_ops.context,
              rotated.lookup_tag,
              &found) == AURORA_IDENTITY_STORE_OK);
    CHECK(memcmp(
              found.user_id.bytes,
              identity1.user_id.bytes,
              AURORA_IDENTITY_USER_ID_SIZE) == 0);

    /* Every later persistent identity starts as standard user. */
    identity2 = make_identity(0xb0u);
    key2 = make_key(&identity2, 0xc0u, 0xd0u);
    CHECK(core_ops.create_identity_with_key(
              core_ops.context,
              &identity2,
              &key2) == AURORA_IDENTITY_STORE_CREATE_OK);
    CHECK(aurora_identity_persistent_store_generation(&reopened) == 4u);
    CHECK(expect_role(
        &reopened,
        &identity2.user_id,
        AURORA_IDENTITY_ROLE_STANDARD_USER));

    /* A caller cannot self-assign administrator by bypassing the core. */
    forged_admin = make_identity(0xe0u);
    forged_admin.role = AURORA_IDENTITY_ROLE_ADMINISTRATOR;
    forged_key = make_key(&forged_admin, 0x21u, 0x41u);
    CHECK(core_ops.create_identity_with_key(
              core_ops.context,
              &forged_admin,
              &forged_key) == AURORA_IDENTITY_STORE_CREATE_ERROR);
    CHECK(aurora_identity_persistent_store_generation(&reopened) == 4u);
    CHECK(aurora_identity_persistent_store_identity_count(&reopened) == 2u);

    CHECK(stat(slot_path[0], &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    CHECK(stat(slot_path[1], &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);

    /* A failed durable publication never changes the visible generation/state. */
    {
        struct failing_io wrapper;
        struct aurora_identity_persistent_io_ops failing_ops;
        struct aurora_identity_persistent_store failure_store;
        struct aurora_identity_store_ops failure_core;

        wrapper.inner = io;
        wrapper.fail_writes = false;
        failing_ops.context = &wrapper;
        failing_ops.read_slot = failing_read;
        failing_ops.write_slot_atomic = failing_write;

        CHECK(aurora_identity_persistent_store_open(&failure_store, &failing_ops) ==
              AURORA_IDENTITY_PERSISTENT_OPEN_OK);
        generation_before_failure =
            aurora_identity_persistent_store_generation(&failure_store);
        wrapper.fail_writes = true;
        failure_core = aurora_identity_persistent_store_core_ops(&failure_store);
        CHECK(!failure_core.store_failure_state(
            failure_core.context,
            &identity1.user_id,
            9u,
            999999u));
        CHECK(aurora_identity_persistent_store_generation(&failure_store) ==
              generation_before_failure);
    }

    CHECK(aurora_identity_persistent_store_open(&store, &io) ==
          AURORA_IDENTITY_PERSISTENT_OPEN_OK);
    CHECK(aurora_identity_persistent_store_generation(&store) == 4u);
    core_ops = aurora_identity_persistent_store_core_ops(&store);
    CHECK(core_ops.find_key_record_by_lookup_tag(
              core_ops.context,
              rotated.lookup_tag,
              &found) == AURORA_IDENTITY_STORE_OK);
    CHECK(found.failed_attempts == 0u);
    CHECK(found.throttle_until_ms == 0u);
    CHECK(expect_role(
        &store,
        &identity1.user_id,
        AURORA_IDENTITY_ROLE_ADMINISTRATOR));
    CHECK(expect_role(
        &store,
        &identity2.user_id,
        AURORA_IDENTITY_ROLE_STANDARD_USER));

    latest_slot = (uint32_t)(
        aurora_identity_persistent_store_generation(&store) %
        AURORA_IDENTITY_STORE_SLOT_COUNT);
    CHECK(overwrite_with_bad_data(slot_path[latest_slot]));
    CHECK(aurora_identity_persistent_store_open(&reopened, &io) ==
          AURORA_IDENTITY_PERSISTENT_OPEN_OK);
    CHECK(aurora_identity_persistent_store_generation(&reopened) == 3u);
    CHECK(aurora_identity_persistent_store_identity_count(&reopened) == 1u);
    CHECK(expect_role(
        &reopened,
        &identity1.user_id,
        AURORA_IDENTITY_ROLE_ADMINISTRATOR));

    CHECK(overwrite_with_bad_data(slot_path[0]));
    CHECK(overwrite_with_bad_data(slot_path[1]));
    CHECK(aurora_identity_persistent_store_open(&store, &io) ==
          AURORA_IDENTITY_PERSISTENT_OPEN_CORRUPT);

    (void)unlink(slot_path[0]);
    (void)unlink(slot_path[1]);
    CHECK(rmdir(directory) == 0);
    return true;
}

int main(void) {
    if (!run_persistence_test()) {
        return 1;
    }

    puts("Aurora Identity persistent-store tests: PASS");
    return 0;
}
