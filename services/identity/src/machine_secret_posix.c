#define _POSIX_C_SOURCE 200809L

#include "aurora/identity/machine_secret_posix.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MACHINE_SECRET_DISK_SIZE 84u

static const uint8_t disk_magic[8u] = {
    'A', 'U', 'R', 'M', 'S', 'V', '1', 0
};

static void secure_zero(void *buffer, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    if (buffer == NULL) return;
    while (size > 0u) {
        *bytes++ = 0u;
        --size;
    }
}

static void store_u32_le(uint8_t out[4u], uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
    out[2] = (uint8_t)(value >> 16u);
    out[3] = (uint8_t)(value >> 24u);
}

static uint32_t load_u32_le(const uint8_t in[4u]) {
    return (uint32_t)in[0] |
        ((uint32_t)in[1] << 8u) |
        ((uint32_t)in[2] << 16u) |
        ((uint32_t)in[3] << 24u);
}

static void store_u64_le(uint8_t out[8u], uint64_t value) {
    for (size_t i = 0u; i < 8u; ++i) out[i] = (uint8_t)(value >> (i * 8u));
}

static uint64_t load_u64_le(const uint8_t in[8u]) {
    uint64_t value = 0u;
    for (size_t i = 0u; i < 8u; ++i) value |= ((uint64_t)in[i]) << (i * 8u);
    return value;
}

static bool build_path(
    const struct aurora_identity_machine_secret_posix_store *store,
    uint32_t replica_index,
    char out[AURORA_IDENTITY_MACHINE_SECRET_POSIX_PATH_MAX]) {
    const char *name = replica_index == 0u
        ? "machine-secret.a"
        : "machine-secret.b";
    if (store == NULL || !store->initialized ||
        replica_index >= AURORA_IDENTITY_MACHINE_SECRET_REPLICA_COUNT) return false;
    int written = snprintf(
        out,
        AURORA_IDENTITY_MACHINE_SECRET_POSIX_PATH_MAX,
        "%s/%s",
        store->directory,
        name);
    return written > 0 &&
        (size_t)written < AURORA_IDENTITY_MACHINE_SECRET_POSIX_PATH_MAX;
}

static bool ensure_directory(const char *directory) {
    struct stat stat_buffer;
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) return false;
    if (stat(directory, &stat_buffer) != 0 || !S_ISDIR(stat_buffer.st_mode)) return false;
    return chmod(directory, 0700) == 0;
}

static bool write_all(int fd, const uint8_t *buffer, size_t size) {
    size_t offset = 0u;
    while (offset < size) {
        ssize_t written = write(fd, buffer + offset, size - offset);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (written == 0) return false;
        offset += (size_t)written;
    }
    return true;
}

static bool read_exact_file(int fd, uint8_t buffer[MACHINE_SECRET_DISK_SIZE]) {
    size_t offset = 0u;
    while (offset < MACHINE_SECRET_DISK_SIZE) {
        ssize_t got = read(fd, buffer + offset, MACHINE_SECRET_DISK_SIZE - offset);
        if (got < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (got == 0) return false;
        offset += (size_t)got;
    }

    uint8_t extra;
    ssize_t tail;
    do {
        tail = read(fd, &extra, 1u);
    } while (tail < 0 && errno == EINTR);
    return tail == 0;
}

static void encode_record(
    const struct aurora_identity_machine_secret_record *record,
    uint8_t out[MACHINE_SECRET_DISK_SIZE]) {
    size_t offset = 0u;
    memcpy(out + offset, disk_magic, sizeof(disk_magic));
    offset += sizeof(disk_magic);
    store_u32_le(out + offset, record->record_version);
    offset += 4u;
    store_u64_le(out + offset, record->generation);
    offset += 8u;
    memcpy(out + offset, record->secret.bytes, sizeof(record->secret.bytes));
    offset += sizeof(record->secret.bytes);
    memcpy(out + offset, record->checksum, sizeof(record->checksum));
}

static bool decode_record(
    const uint8_t in[MACHINE_SECRET_DISK_SIZE],
    struct aurora_identity_machine_secret_record *record) {
    size_t offset = 0u;
    if (memcmp(in, disk_magic, sizeof(disk_magic)) != 0) return false;
    offset += sizeof(disk_magic);
    record->record_version = load_u32_le(in + offset);
    offset += 4u;
    record->generation = load_u64_le(in + offset);
    offset += 8u;
    memcpy(record->secret.bytes, in + offset, sizeof(record->secret.bytes));
    offset += sizeof(record->secret.bytes);
    memcpy(record->checksum, in + offset, sizeof(record->checksum));
    return true;
}

static enum aurora_identity_machine_secret_load_result posix_load_replica(
    void *context,
    uint32_t replica_index,
    struct aurora_identity_machine_secret_record *out_record) {
    struct aurora_identity_machine_secret_posix_store *store = context;
    char path[AURORA_IDENTITY_MACHINE_SECRET_POSIX_PATH_MAX];
    uint8_t disk[MACHINE_SECRET_DISK_SIZE];

    if (out_record == NULL || !build_path(store, replica_index, path)) {
        return AURORA_IDENTITY_MACHINE_SECRET_LOAD_ERROR;
    }
    secure_zero(out_record, sizeof(*out_record));
    secure_zero(disk, sizeof(disk));

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return errno == ENOENT
            ? AURORA_IDENTITY_MACHINE_SECRET_LOAD_ABSENT
            : AURORA_IDENTITY_MACHINE_SECRET_LOAD_ERROR;
    }

    bool exact = read_exact_file(fd, disk);
    int close_result = close(fd);
    if (close_result != 0) exact = false;

    if (!exact || !decode_record(disk, out_record)) {
        secure_zero(out_record, sizeof(*out_record));
        secure_zero(disk, sizeof(disk));
        return AURORA_IDENTITY_MACHINE_SECRET_LOAD_OK;
    }

    secure_zero(disk, sizeof(disk));
    return AURORA_IDENTITY_MACHINE_SECRET_LOAD_OK;
}

static bool sync_directory(const char *directory) {
    int fd = open(directory, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    bool ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    return ok;
}

static enum aurora_identity_machine_secret_publish_result posix_publish_replica(
    void *context,
    uint32_t replica_index,
    const struct aurora_identity_machine_secret_record *record) {
    struct aurora_identity_machine_secret_posix_store *store = context;
    char target[AURORA_IDENTITY_MACHINE_SECRET_POSIX_PATH_MAX];
    char temporary[AURORA_IDENTITY_MACHINE_SECRET_POSIX_PATH_MAX];
    uint8_t disk[MACHINE_SECRET_DISK_SIZE];

    if (record == NULL || !build_path(store, replica_index, target)) {
        return AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_ERROR;
    }

    int n = snprintf(
        temporary,
        sizeof(temporary),
        "%s/.machine-secret.%u.%ld.tmp",
        store->directory,
        replica_index,
        (long)getpid());
    if (n <= 0 || (size_t)n >= sizeof(temporary)) {
        return AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_ERROR;
    }

    encode_record(record, disk);
    (void)unlink(temporary);

    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        secure_zero(disk, sizeof(disk));
        return AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_ERROR;
    }

    bool ok = write_all(fd, disk, sizeof(disk));
    if (ok) ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    secure_zero(disk, sizeof(disk));

    if (!ok) {
        (void)unlink(temporary);
        return AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_ERROR;
    }

    if (link(temporary, target) != 0) {
        int link_error = errno;
        (void)unlink(temporary);
        return link_error == EEXIST
            ? AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_CONFLICT
            : AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_ERROR;
    }

    bool unlinked = unlink(temporary) == 0;
    bool synced = sync_directory(store->directory);
    if (!unlinked || !synced) {
        return AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_ERROR;
    }

    return AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_OK;
}

bool aurora_identity_machine_secret_posix_store_init(
    struct aurora_identity_machine_secret_posix_store *store,
    const char *directory) {
    if (store == NULL || directory == NULL) return false;
    size_t length = strlen(directory);
    if (length == 0u || length >= sizeof(store->directory)) return false;
    if (!ensure_directory(directory)) return false;
    memset(store, 0, sizeof(*store));
    memcpy(store->directory, directory, length + 1u);
    store->initialized = true;
    return true;
}

struct aurora_identity_machine_secret_store_ops
    aurora_identity_machine_secret_posix_store_ops(
        struct aurora_identity_machine_secret_posix_store *store) {
    struct aurora_identity_machine_secret_store_ops ops;
    memset(&ops, 0, sizeof(ops));
    ops.context = store;
    ops.load_replica = posix_load_replica;
    ops.publish_replica = posix_publish_replica;
    return ops;
}
