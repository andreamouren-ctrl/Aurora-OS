#define _POSIX_C_SOURCE 200809L

#include "aurora/identity/machine_secret.h"
#include "aurora/identity/machine_secret_posix.h"

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct fake_random {
    uint8_t next;
    unsigned calls;
    unsigned zero_calls;
    bool fail;
};

static bool fake_fill_random(void *context, uint8_t *buffer, size_t size) {
    struct fake_random *random = context;
    ++random->calls;
    if (random->fail) return false;
    if (random->zero_calls > 0u) {
        --random->zero_calls;
        memset(buffer, 0, size);
        return true;
    }
    for (size_t i = 0u; i < size; ++i) {
        buffer[i] = random->next++;
        if (buffer[i] == 0u) buffer[i] = random->next++;
    }
    return true;
}

static struct aurora_identity_random_ops random_ops(struct fake_random *random) {
    struct aurora_identity_random_ops ops;
    memset(&ops, 0, sizeof(ops));
    ops.context = random;
    ops.fill_random = fake_fill_random;
    return ops;
}

static void build_path(char *out, size_t capacity, const char *dir, const char *name) {
    int written = snprintf(out, capacity, "%s/%s", dir, name);
    assert(written > 0 && (size_t)written < capacity);
}

static void remove_tree(const char *dir) {
    char path[768];
    build_path(path, sizeof(path), dir, "machine-secret.a");
    (void)unlink(path);
    build_path(path, sizeof(path), dir, "machine-secret.b");
    (void)unlink(path);
    (void)rmdir(dir);
}

static void corrupt_file(const char *path) {
    int fd = open(path, O_WRONLY | O_TRUNC);
    assert(fd >= 0);
    static const uint8_t garbage[] = { 0xBAu, 0xD0u, 0x0Du };
    assert(write(fd, garbage, sizeof(garbage)) == (ssize_t)sizeof(garbage));
    assert(fsync(fd) == 0);
    assert(close(fd) == 0);
}

static void test_provision_reopen_and_no_regeneration(void) {
    char directory[] = "/tmp/aurora-machine-secret-XXXXXX";
    assert(mkdtemp(directory) != NULL);

    struct aurora_identity_machine_secret_posix_store store;
    assert(aurora_identity_machine_secret_posix_store_init(&store, directory));

    struct fake_random random = { .next = 1u };
    struct aurora_identity_machine_secret_core core;
    memset(&core, 0, sizeof(core));
    core.random = random_ops(&random);
    core.store = aurora_identity_machine_secret_posix_store_ops(&store);

    struct aurora_identity_machine_secret first;
    assert(aurora_identity_machine_secret_load_or_provision(&core, &first) ==
        AURORA_IDENTITY_MACHINE_SECRET_OK);
    assert(random.calls == 1u);
    assert(!aurora_identity_machine_secret_is_zero(&first));

    uint8_t lookup_key_first[32];
    assert(aurora_identity_machine_secret_derive_lookup_key(&first, lookup_key_first));

    struct stat st;
    assert(stat(directory, &st) == 0);
    assert((st.st_mode & 0777) == 0700);

    char replica_a[768];
    char replica_b[768];
    build_path(replica_a, sizeof(replica_a), directory, "machine-secret.a");
    build_path(replica_b, sizeof(replica_b), directory, "machine-secret.b");
    assert(stat(replica_a, &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(stat(replica_b, &st) == 0 && (st.st_mode & 0777) == 0600);

    struct aurora_identity_machine_secret_posix_store reopened;
    assert(aurora_identity_machine_secret_posix_store_init(&reopened, directory));

    struct fake_random must_not_run = { .fail = true };
    struct aurora_identity_machine_secret_core reopened_core;
    memset(&reopened_core, 0, sizeof(reopened_core));
    reopened_core.random = random_ops(&must_not_run);
    reopened_core.store = aurora_identity_machine_secret_posix_store_ops(&reopened);

    struct aurora_identity_machine_secret second;
    assert(aurora_identity_machine_secret_load_or_provision(&reopened_core, &second) ==
        AURORA_IDENTITY_MACHINE_SECRET_OK);
    assert(must_not_run.calls == 0u);
    assert(memcmp(first.bytes, second.bytes, sizeof(first.bytes)) == 0);

    uint8_t lookup_key_second[32];
    assert(aurora_identity_machine_secret_derive_lookup_key(&second, lookup_key_second));
    assert(memcmp(lookup_key_first, lookup_key_second, sizeof(lookup_key_first)) == 0);

    memset(lookup_key_first, 0, sizeof(lookup_key_first));
    memset(lookup_key_second, 0, sizeof(lookup_key_second));
    aurora_identity_machine_secret_clear(&first);
    aurora_identity_machine_secret_clear(&second);
    remove_tree(directory);
}

static void test_zero_candidate_retry(void) {
    char directory[] = "/tmp/aurora-machine-zero-XXXXXX";
    assert(mkdtemp(directory) != NULL);

    struct aurora_identity_machine_secret_posix_store store;
    assert(aurora_identity_machine_secret_posix_store_init(&store, directory));

    struct fake_random random = { .next = 7u, .zero_calls = 1u };
    struct aurora_identity_machine_secret_core core;
    memset(&core, 0, sizeof(core));
    core.random = random_ops(&random);
    core.store = aurora_identity_machine_secret_posix_store_ops(&store);

    struct aurora_identity_machine_secret secret;
    assert(aurora_identity_machine_secret_load_or_provision(&core, &secret) ==
        AURORA_IDENTITY_MACHINE_SECRET_OK);
    assert(random.calls == 2u);
    assert(!aurora_identity_machine_secret_is_zero(&secret));

    aurora_identity_machine_secret_clear(&secret);
    remove_tree(directory);
}

static void test_single_replica_corruption_recovers_valid_copy(void) {
    char directory[] = "/tmp/aurora-machine-recover-XXXXXX";
    assert(mkdtemp(directory) != NULL);

    struct aurora_identity_machine_secret_posix_store store;
    assert(aurora_identity_machine_secret_posix_store_init(&store, directory));
    struct fake_random random = { .next = 23u };
    struct aurora_identity_machine_secret_core core = {
        .random = { .context = &random, .fill_random = fake_fill_random },
        .store = {0}
    };
    core.store = aurora_identity_machine_secret_posix_store_ops(&store);

    struct aurora_identity_machine_secret expected;
    assert(aurora_identity_machine_secret_load_or_provision(&core, &expected) ==
        AURORA_IDENTITY_MACHINE_SECRET_OK);

    char replica_b[768];
    build_path(replica_b, sizeof(replica_b), directory, "machine-secret.b");
    corrupt_file(replica_b);

    struct aurora_identity_machine_secret recovered;
    assert(aurora_identity_machine_secret_load(&core.store, &recovered) ==
        AURORA_IDENTITY_MACHINE_SECRET_OK);
    assert(memcmp(expected.bytes, recovered.bytes, sizeof(expected.bytes)) == 0);

    aurora_identity_machine_secret_clear(&expected);
    aurora_identity_machine_secret_clear(&recovered);
    remove_tree(directory);
}

static void test_both_corrupt_fail_closed(void) {
    char directory[] = "/tmp/aurora-machine-corrupt-XXXXXX";
    assert(mkdtemp(directory) != NULL);

    struct aurora_identity_machine_secret_posix_store store;
    assert(aurora_identity_machine_secret_posix_store_init(&store, directory));
    struct fake_random random = { .next = 41u };
    struct aurora_identity_machine_secret_core core;
    memset(&core, 0, sizeof(core));
    core.random = random_ops(&random);
    core.store = aurora_identity_machine_secret_posix_store_ops(&store);

    struct aurora_identity_machine_secret secret;
    assert(aurora_identity_machine_secret_load_or_provision(&core, &secret) ==
        AURORA_IDENTITY_MACHINE_SECRET_OK);
    unsigned calls_after_provision = random.calls;

    char path[768];
    build_path(path, sizeof(path), directory, "machine-secret.a");
    corrupt_file(path);
    build_path(path, sizeof(path), directory, "machine-secret.b");
    corrupt_file(path);

    struct aurora_identity_machine_secret output;
    memset(&output, 0xA5, sizeof(output));
    assert(aurora_identity_machine_secret_load_or_provision(&core, &output) ==
        AURORA_IDENTITY_MACHINE_SECRET_CORRUPT);
    assert(random.calls == calls_after_provision);
    assert(aurora_identity_machine_secret_is_zero(&output));

    aurora_identity_machine_secret_clear(&secret);
    remove_tree(directory);
}

static void test_random_failure_keeps_store_unprovisioned(void) {
    char directory[] = "/tmp/aurora-machine-rngfail-XXXXXX";
    assert(mkdtemp(directory) != NULL);

    struct aurora_identity_machine_secret_posix_store store;
    assert(aurora_identity_machine_secret_posix_store_init(&store, directory));
    struct fake_random random = { .fail = true };
    struct aurora_identity_machine_secret_core core;
    memset(&core, 0, sizeof(core));
    core.random = random_ops(&random);
    core.store = aurora_identity_machine_secret_posix_store_ops(&store);

    struct aurora_identity_machine_secret output;
    assert(aurora_identity_machine_secret_load_or_provision(&core, &output) ==
        AURORA_IDENTITY_MACHINE_SECRET_RANDOM_ERROR);
    assert(aurora_identity_machine_secret_is_zero(&output));
    assert(aurora_identity_machine_secret_load(&core.store, &output) ==
        AURORA_IDENTITY_MACHINE_SECRET_NOT_PROVISIONED);

    remove_tree(directory);
}

int main(void) {
    test_provision_reopen_and_no_regeneration();
    test_zero_candidate_retry();
    test_single_replica_corruption_recovers_valid_copy();
    test_both_corrupt_fail_closed();
    test_random_failure_keeps_store_unprovisioned();
    puts("Aurora Identity machine-secret tests: PASS");
    return 0;
}
