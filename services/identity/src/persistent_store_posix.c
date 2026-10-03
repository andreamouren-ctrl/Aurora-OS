#define _POSIX_C_SOURCE 200809L

#include "aurora/identity/persistent_store_posix.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static bool build_slot_path(
    const struct aurora_identity_posix_store *store,
    uint32_t slot,
    char *buffer,
    size_t capacity) {
    int written;

    if (store == NULL || buffer == NULL ||
        slot >= AURORA_IDENTITY_STORE_SLOT_COUNT) {
        return false;
    }

    written = snprintf(
        buffer,
        capacity,
        "%s.slot%u",
        store->base_path,
        (unsigned int)slot);

    return written >= 0 && (size_t)written < capacity;
}

static bool build_temp_path(
    const struct aurora_identity_posix_store *store,
    uint32_t slot,
    char *buffer,
    size_t capacity) {
    int written;

    if (store == NULL || buffer == NULL ||
        slot >= AURORA_IDENTITY_STORE_SLOT_COUNT) {
        return false;
    }

    written = snprintf(
        buffer,
        capacity,
        "%s.slot%u.tmp",
        store->base_path,
        (unsigned int)slot);

    return written >= 0 && (size_t)written < capacity;
}

static bool write_all(int fd, const uint8_t *buffer, size_t size) {
    size_t offset = 0u;

    while (offset < size) {
        ssize_t written = write(fd, buffer + offset, size - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            return false;
        }
        offset += (size_t)written;
    }

    return true;
}

static bool read_all(int fd, uint8_t *buffer, size_t size) {
    size_t offset = 0u;

    while (offset < size) {
        ssize_t count = read(fd, buffer + offset, size - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (count == 0) {
            return false;
        }
        offset += (size_t)count;
    }

    return true;
}

static bool fsync_parent_directory(const char *path) {
    char directory[AURORA_IDENTITY_POSIX_BASE_PATH_MAX + 32u];
    const char *slash;
    size_t length;
    int fd;
    bool ok;

    if (path == NULL) {
        return false;
    }

    slash = strrchr(path, '/');
    if (slash == NULL) {
        strcpy(directory, ".");
    } else if (slash == path) {
        strcpy(directory, "/");
    } else {
        length = (size_t)(slash - path);
        if (length >= sizeof(directory)) {
            return false;
        }
        memcpy(directory, path, length);
        directory[length] = '\0';
    }

    fd = open(directory, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    ok = fsync(fd) == 0;
    if (close(fd) != 0) {
        ok = false;
    }
    return ok;
}

static enum aurora_identity_persistent_io_result posix_read_slot(
    void *context,
    uint32_t slot,
    uint8_t *buffer,
    size_t capacity,
    size_t *out_size) {
    struct aurora_identity_posix_store *store =
        (struct aurora_identity_posix_store *)context;
    char path[AURORA_IDENTITY_POSIX_BASE_PATH_MAX + 32u];
    struct stat st;
    int fd;
    bool ok;

    if (store == NULL || buffer == NULL || out_size == NULL ||
        !build_slot_path(store, slot, path, sizeof(path))) {
        return AURORA_IDENTITY_PERSISTENT_IO_ERROR;
    }

    *out_size = 0u;
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            return AURORA_IDENTITY_PERSISTENT_IO_NOT_FOUND;
        }
        return AURORA_IDENTITY_PERSISTENT_IO_ERROR;
    }

    if (fstat(fd, &st) != 0 || st.st_size <= 0 ||
        (uintmax_t)st.st_size > (uintmax_t)capacity) {
        (void)close(fd);
        return AURORA_IDENTITY_PERSISTENT_IO_ERROR;
    }

    ok = read_all(fd, buffer, (size_t)st.st_size);
    if (close(fd) != 0) {
        ok = false;
    }
    if (!ok) {
        return AURORA_IDENTITY_PERSISTENT_IO_ERROR;
    }

    *out_size = (size_t)st.st_size;
    return AURORA_IDENTITY_PERSISTENT_IO_OK;
}

static bool posix_write_slot_atomic(
    void *context,
    uint32_t slot,
    const uint8_t *buffer,
    size_t size) {
    struct aurora_identity_posix_store *store =
        (struct aurora_identity_posix_store *)context;
    char final_path[AURORA_IDENTITY_POSIX_BASE_PATH_MAX + 32u];
    char temp_path[AURORA_IDENTITY_POSIX_BASE_PATH_MAX + 32u];
    int fd = -1;
    bool ok = false;

    if (store == NULL || buffer == NULL || size == 0u ||
        !build_slot_path(store, slot, final_path, sizeof(final_path)) ||
        !build_temp_path(store, slot, temp_path, sizeof(temp_path))) {
        return false;
    }

    fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return false;
    }

    if (fchmod(fd, S_IRUSR | S_IWUSR) != 0 ||
        !write_all(fd, buffer, size) ||
        fsync(fd) != 0) {
        goto cleanup;
    }

    if (close(fd) != 0) {
        fd = -1;
        goto cleanup;
    }
    fd = -1;

    if (rename(temp_path, final_path) != 0) {
        goto cleanup;
    }

    if (!fsync_parent_directory(final_path)) {
        return false;
    }

    ok = true;

cleanup:
    if (fd >= 0) {
        (void)close(fd);
    }
    if (!ok) {
        (void)unlink(temp_path);
    }
    return ok;
}

bool aurora_identity_posix_store_init(
    struct aurora_identity_posix_store *store,
    const char *base_path) {
    size_t length;

    if (store == NULL || base_path == NULL) {
        return false;
    }

    length = strlen(base_path);
    if (length == 0u || length >= sizeof(store->base_path)) {
        return false;
    }

    memset(store, 0, sizeof(*store));
    memcpy(store->base_path, base_path, length + 1u);
    return true;
}

struct aurora_identity_persistent_io_ops aurora_identity_posix_store_io(
    struct aurora_identity_posix_store *store) {
    struct aurora_identity_persistent_io_ops ops;

    memset(&ops, 0, sizeof(ops));
    ops.context = store;
    ops.read_slot = posix_read_slot;
    ops.write_slot_atomic = posix_write_slot_atomic;
    return ops;
}
