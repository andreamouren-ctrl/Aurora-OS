#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/profile_session.h>
#include <aurora/spinlock.h>
#include <aurora/vfs.h>

#define PROFILE_ROOT_PATH "/system/users"
#define PROFILE_DIRECTORY_MODE 0700u
#define PROFILE_ROOT_MODE 0700u

static struct aurora_profile_root_authority root_authority = {
    .marker = UINT64_C(0x50524F46494C4552)
};

static struct aurora_profile_object profiles[AURORA_PROFILE_MAX];
static aurora_spinlock profile_lock;
static bool profile_initialized;

static void clear_bytes(void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    if (buffer == NULL) return;
    for (size_t i = 0u; i < size; ++i) bytes[i] = 0u;
}

static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t size) {
    if (a == NULL || b == NULL) return false;
    uint8_t diff = 0u;
    for (size_t i = 0u; i < size; ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0u;
}

static bool user_id_valid(const uint8_t user_id[AURORA_PROFILE_USER_ID_SIZE]) {
    if (user_id == NULL) return false;
    uint8_t combined = 0u;
    for (size_t i = 0u; i < AURORA_PROFILE_USER_ID_SIZE; ++i) {
        combined |= user_id[i];
    }
    return combined != 0u;
}

static char hex_digit(uint8_t nibble) {
    nibble &= 0x0Fu;
    return nibble < 10u
        ? (char)('0' + nibble)
        : (char)('a' + (nibble - 10u));
}

static bool build_profile_path(
    const uint8_t user_id[AURORA_PROFILE_USER_ID_SIZE],
    char out[AURORA_PROFILE_PATH_MAX]
) {
    static const char prefix[] = PROFILE_ROOT_PATH "/";
    if (user_id == NULL || out == NULL) return false;

    size_t offset = 0u;
    for (size_t i = 0u; i < sizeof(prefix) - 1u; ++i) {
        if (offset + 1u >= AURORA_PROFILE_PATH_MAX) return false;
        out[offset++] = prefix[i];
    }

    for (size_t i = 0u; i < AURORA_PROFILE_USER_ID_SIZE; ++i) {
        if (offset + 2u >= AURORA_PROFILE_PATH_MAX) return false;
        out[offset++] = hex_digit((uint8_t)(user_id[i] >> 4));
        out[offset++] = hex_digit(user_id[i]);
    }

    out[offset] = '\0';
    return true;
}

static bool ensure_directory(const char *path, uint32_t mode) {
    struct aurora_vfs_stat stat;
    enum aurora_vfs_lookup_result result = vfs_stat_result(path, &stat);
    if (result == AURORA_VFS_LOOKUP_FOUND) {
        return stat.type == AURORA_VFS_NODE_DIRECTORY &&
            vfs_chmod(path, mode);
    }
    if (result != AURORA_VFS_LOOKUP_NOT_FOUND ||
        !vfs_create_directory(path)) {
        return false;
    }
    return vfs_chown(path, 0u, 0u) &&
        vfs_chmod(path, mode) &&
        vfs_sync(path);
}

static bool profile_store_ready(void) {
    struct aurora_vfs_stat system_stat;
    if (!vfs_stat("/system", &system_stat) ||
        system_stat.type != AURORA_VFS_NODE_DIRECTORY) {
        return false;
    }
    return ensure_directory(PROFILE_ROOT_PATH, PROFILE_ROOT_MODE);
}

static void ensure_initialized(void) {
    if (profile_initialized) return;
    spinlock_init(&profile_lock);
    for (size_t i = 0u; i < AURORA_PROFILE_MAX; ++i) {
        clear_bytes(&profiles[i], sizeof(profiles[i]));
    }
    profile_initialized = true;
}

struct aurora_profile_root_authority *profile_root_authority(void) {
    ensure_initialized();
    return &root_authority;
}

aurora_cap_handle profile_open_or_create(
    struct aurora_process *process,
    aurora_cap_handle root_handle,
    const uint8_t user_id[AURORA_PROFILE_USER_ID_SIZE]
) {
    struct aurora_capability_view root_view;
    if (process == NULL || !user_id_valid(user_id) ||
        !cap_lookup(
            &process->capabilities,
            root_handle,
            AURORA_CAP_PROFILE_ROOT,
            AURORA_RIGHT_CONTROL,
            &root_view) ||
        root_view.object != &root_authority ||
        !profile_store_ready()) {
        return AURORA_CAP_INVALID;
    }

    ensure_initialized();
    char path[AURORA_PROFILE_PATH_MAX];
    clear_bytes(path, sizeof(path));
    if (!build_profile_path(user_id, path)) return AURORA_CAP_INVALID;

    spinlock_lock(&profile_lock);

    struct aurora_profile_object *slot = NULL;
    struct aurora_profile_object *free_slot = NULL;
    for (size_t i = 0u; i < AURORA_PROFILE_MAX; ++i) {
        if (profiles[i].occupied) {
            if (bytes_equal(
                    profiles[i].user_id,
                    user_id,
                    AURORA_PROFILE_USER_ID_SIZE)) {
                slot = &profiles[i];
                break;
            }
        } else if (free_slot == NULL) {
            free_slot = &profiles[i];
        }
    }

    if (slot == NULL) {
        if (free_slot == NULL) {
            spinlock_unlock(&profile_lock);
            return AURORA_CAP_INVALID;
        }

        if (!ensure_directory(path, PROFILE_DIRECTORY_MODE)) {
            spinlock_unlock(&profile_lock);
            return AURORA_CAP_INVALID;
        }

        free_slot->occupied = true;
        for (size_t i = 0u; i < AURORA_PROFILE_USER_ID_SIZE; ++i) {
            free_slot->user_id[i] = user_id[i];
        }
        for (size_t i = 0u; i < AURORA_PROFILE_PATH_MAX; ++i) {
            free_slot->path[i] = path[i];
            if (path[i] == '\0') break;
        }
        slot = free_slot;
    } else {
        struct aurora_vfs_stat stat;
        if (!vfs_stat(slot->path, &stat) ||
            stat.type != AURORA_VFS_NODE_DIRECTORY) {
            spinlock_unlock(&profile_lock);
            return AURORA_CAP_INVALID;
        }
    }

    aurora_cap_handle handle = cap_grant(
        &process->capabilities,
        slot,
        AURORA_CAP_FILE,
        AURORA_RIGHT_READ |
        AURORA_RIGHT_WRITE |
        AURORA_RIGHT_ENUMERATE |
        AURORA_RIGHT_CONTROL |
        AURORA_RIGHT_TRANSFER
    );

    spinlock_unlock(&profile_lock);
    return handle;
}

bool profile_capability_matches_user(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    const uint8_t user_id[AURORA_PROFILE_USER_ID_SIZE]
) {
    struct aurora_capability_view view;
    if (table == NULL || !user_id_valid(user_id) ||
        !cap_lookup(
            table,
            handle,
            AURORA_CAP_FILE,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
            &view)) {
        return false;
    }

    const struct aurora_profile_object *profile =
        (const struct aurora_profile_object *)view.object;
    return profile != NULL &&
        profile->occupied &&
        bytes_equal(
            profile->user_id,
            user_id,
            AURORA_PROFILE_USER_ID_SIZE);
}
