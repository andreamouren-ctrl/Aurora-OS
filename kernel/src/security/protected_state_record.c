#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/protected_state.h>
#include <aurora/spinlock.h>
#include <aurora/vfs.h>

#define PROTECTED_STATE_RECORD_FILE_MODE 0600u
#define PROTECTED_STATE_STAGING_PREFIX ".aurora-publish-"

static aurora_spinlock record_publish_lock = AURORA_SPINLOCK_INIT;

static size_t bounded_length(const char *text, size_t limit) {
    size_t length = 0u;
    if (text == NULL) return limit;
    while (length < limit && text[length] != '\0') ++length;
    return length;
}

static bool record_name_valid(const char *name) {
    size_t length = bounded_length(name, AURORA_VFS_PATH_MAX);
    if (length == 0u || length >= AURORA_VFS_PATH_MAX) return false;
    if ((length == 1u && name[0] == '.') ||
        (length == 2u && name[0] == '.' && name[1] == '.')) return false;

    for (size_t i = 0u; i < length; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch == (unsigned char)'/' || ch == (unsigned char)'\\' ||
            ch == (unsigned char)'\n' || ch == (unsigned char)'\r' ||
            ch == (unsigned char)'\t' || ch < 0x20u || ch == 0x7Fu) {
            return false;
        }
    }
    return true;
}

static bool build_record_path(
    const struct aurora_protected_state_namespace *state,
    const char *relative_path,
    char out[AURORA_VFS_PATH_MAX]
) {
    if (state == NULL || !state->initialized || out == NULL ||
        !record_name_valid(relative_path)) return false;

    size_t root_length = bounded_length(state->root, sizeof(state->root));
    size_t name_length = bounded_length(relative_path, AURORA_VFS_PATH_MAX);
    if (root_length == 0u || root_length >= sizeof(state->root) ||
        root_length + 1u + name_length >= AURORA_VFS_PATH_MAX) return false;

    size_t offset = 0u;
    for (size_t i = 0u; i < root_length; ++i) out[offset++] = state->root[i];
    out[offset++] = '/';
    for (size_t i = 0u; i < name_length; ++i) out[offset++] = relative_path[i];
    out[offset] = '\0';
    return true;
}

static bool build_staging_name(
    const char *relative_path,
    char out[AURORA_VFS_PATH_MAX]
) {
    static const char prefix[] = PROTECTED_STATE_STAGING_PREFIX;
    size_t prefix_length = sizeof(prefix) - 1u;
    size_t name_length = bounded_length(relative_path, AURORA_VFS_PATH_MAX);
    if (!record_name_valid(relative_path) ||
        prefix_length + name_length >= AURORA_VFS_PATH_MAX) return false;

    size_t offset = 0u;
    for (size_t i = 0u; i < prefix_length; ++i) out[offset++] = prefix[i];
    for (size_t i = 0u; i < name_length; ++i) out[offset++] = relative_path[i];
    out[offset] = '\0';
    return true;
}

static bool authorize_record(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    uint64_t rights
) {
    struct aurora_capability_view view;
    if (table == NULL || state == NULL || !state->initialized ||
        !cap_lookup(
            table,
            handle,
            AURORA_CAP_PROTECTED_STATE,
            rights,
            &view)) {
        return false;
    }
    return view.object == state;
}

static bool cleanup_staging(const char *path) {
    struct aurora_vfs_stat stat;
    if (path == NULL) return false;

    enum aurora_vfs_lookup_result lookup = vfs_stat_result(path, &stat);
    if (lookup == AURORA_VFS_LOOKUP_NOT_FOUND) return true;
    if (lookup != AURORA_VFS_LOOKUP_FOUND) return false;
    return vfs_remove(path);
}

static bool write_staging_record(
    const char *staging,
    const void *data,
    size_t length
) {
    if (staging == NULL || data == NULL || length == 0u ||
        !vfs_create_file(staging)) {
        return false;
    }

    if (!vfs_chown(staging, 0u, 0u) ||
        !vfs_chmod(staging, PROTECTED_STATE_RECORD_FILE_MODE) ||
        !vfs_write_file(staging, data, length) ||
        !vfs_fdatasync(staging) ||
        !vfs_fsync(staging)) {
        (void)cleanup_staging(staging);
        return false;
    }

    return true;
}

enum aurora_protected_state_read_result protected_state_read_record(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path,
    void *buffer,
    size_t capacity,
    size_t *out_length
) {
    char path[AURORA_VFS_PATH_MAX];
    struct aurora_vfs_stat stat;

    if (!authorize_record(table, handle, state, AURORA_RIGHT_READ) ||
        buffer == NULL || out_length == NULL || capacity == 0u ||
        capacity > AURORA_PROTECTED_STATE_RECORD_MAX ||
        !build_record_path(state, relative_path, path)) {
        return AURORA_PROTECTED_STATE_READ_ERROR;
    }

    *out_length = 0u;
    enum aurora_vfs_lookup_result lookup = vfs_stat_result(path, &stat);
    if (lookup == AURORA_VFS_LOOKUP_NOT_FOUND)
        return AURORA_PROTECTED_STATE_READ_NOT_FOUND;
    if (lookup != AURORA_VFS_LOOKUP_FOUND ||
        stat.type != AURORA_VFS_NODE_FILE ||
        stat.size == 0u || stat.size > (uint64_t)capacity ||
        stat.size > AURORA_PROTECTED_STATE_RECORD_MAX) {
        return AURORA_PROTECTED_STATE_READ_ERROR;
    }

    size_t length = 0u;
    if (!vfs_read_file(path, buffer, capacity, &length) ||
        (uint64_t)length != stat.size) {
        return AURORA_PROTECTED_STATE_READ_ERROR;
    }

    *out_length = length;
    return AURORA_PROTECTED_STATE_READ_OK;
}

enum aurora_protected_state_create_once_result
protected_state_create_record_once_durable(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path,
    const void *data,
    size_t length
) {
    char target[AURORA_VFS_PATH_MAX];
    char staging_name[AURORA_VFS_PATH_MAX];
    char staging[AURORA_VFS_PATH_MAX];
    struct aurora_vfs_stat stat;
    enum aurora_protected_state_create_once_result result =
        AURORA_PROTECTED_STATE_CREATE_ONCE_ERROR;

    if (!authorize_record(table, handle, state, AURORA_RIGHT_WRITE) ||
        data == NULL || length == 0u ||
        length > AURORA_PROTECTED_STATE_RECORD_MAX ||
        !build_record_path(state, relative_path, target) ||
        !build_staging_name(relative_path, staging_name) ||
        !build_record_path(state, staging_name, staging)) {
        return AURORA_PROTECTED_STATE_CREATE_ONCE_ERROR;
    }

    if (!spinlock_try_lock(&record_publish_lock)) {
        return AURORA_PROTECTED_STATE_CREATE_ONCE_ERROR;
    }

    enum aurora_vfs_lookup_result target_lookup = vfs_stat_result(target, &stat);
    if (target_lookup == AURORA_VFS_LOOKUP_FOUND) {
        result = AURORA_PROTECTED_STATE_CREATE_ONCE_EXISTS;
        goto out;
    }
    if (target_lookup != AURORA_VFS_LOOKUP_NOT_FOUND) goto out;

    enum aurora_vfs_lookup_result staging_lookup = vfs_stat_result(staging, &stat);
    if (staging_lookup == AURORA_VFS_LOOKUP_FOUND) {
        if (!vfs_remove(staging)) goto out;
    } else if (staging_lookup != AURORA_VFS_LOOKUP_NOT_FOUND) {
        goto out;
    }

    if (!write_staging_record(staging, data, length)) goto out;

    target_lookup = vfs_stat_result(target, &stat);
    if (target_lookup == AURORA_VFS_LOOKUP_FOUND) {
        (void)cleanup_staging(staging);
        result = AURORA_PROTECTED_STATE_CREATE_ONCE_EXISTS;
        goto out;
    }
    if (target_lookup != AURORA_VFS_LOOKUP_NOT_FOUND) {
        (void)cleanup_staging(staging);
        goto out;
    }

    if (!vfs_rename(staging, target)) {
        enum aurora_vfs_lookup_result after = vfs_stat_result(target, &stat);
        (void)cleanup_staging(staging);
        result = after == AURORA_VFS_LOOKUP_FOUND
            ? AURORA_PROTECTED_STATE_CREATE_ONCE_EXISTS
            : AURORA_PROTECTED_STATE_CREATE_ONCE_ERROR;
        goto out;
    }

    if (!vfs_fsync(target) || !vfs_sync(state->root)) goto out;

    result = AURORA_PROTECTED_STATE_CREATE_ONCE_OK;

out:
    spinlock_unlock(&record_publish_lock);
    return result;
}

bool protected_state_replace_record_durable(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path,
    const void *data,
    size_t length
) {
    char target[AURORA_VFS_PATH_MAX];
    char staging_name[AURORA_VFS_PATH_MAX];
    char staging[AURORA_VFS_PATH_MAX];
    struct aurora_vfs_stat stat;
    bool result = false;

    if (!authorize_record(table, handle, state, AURORA_RIGHT_WRITE) ||
        data == NULL || length == 0u ||
        length > AURORA_PROTECTED_STATE_RECORD_MAX ||
        !build_record_path(state, relative_path, target) ||
        !build_staging_name(relative_path, staging_name) ||
        !build_record_path(state, staging_name, staging)) {
        return false;
    }

    if (!spinlock_try_lock(&record_publish_lock)) return false;

    enum aurora_vfs_lookup_result staging_lookup = vfs_stat_result(staging, &stat);
    if (staging_lookup == AURORA_VFS_LOOKUP_FOUND) {
        if (!vfs_remove(staging) || !vfs_sync(state->root)) goto out;
    } else if (staging_lookup != AURORA_VFS_LOOKUP_NOT_FOUND) {
        goto out;
    }

    if (!write_staging_record(staging, data, length)) goto out;

    enum aurora_vfs_lookup_result target_lookup = vfs_stat_result(target, &stat);
    if (target_lookup == AURORA_VFS_LOOKUP_FOUND) {
        if (stat.type != AURORA_VFS_NODE_FILE ||
            !vfs_remove(target) ||
            !vfs_sync(state->root)) {
            (void)cleanup_staging(staging);
            goto out;
        }
    } else if (target_lookup != AURORA_VFS_LOOKUP_NOT_FOUND) {
        (void)cleanup_staging(staging);
        goto out;
    }

    if (!vfs_rename(staging, target)) {
        (void)cleanup_staging(staging);
        goto out;
    }

    if (!vfs_fsync(target) || !vfs_sync(state->root)) goto out;
    result = true;

out:
    spinlock_unlock(&record_publish_lock);
    return result;
}
