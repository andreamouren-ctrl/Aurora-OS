#include <stddef.h>

#include <aurora/fs_mount.h>

static struct aurora_fs_mount mounts[AURORA_FS_MOUNT_MAX];

static bool string_equal(const char *a, const char *b) {
    size_t i = 0u;
    while (a[i] != '\0' || b[i] != '\0') {
        if (a[i] != b[i]) {
            return false;
        }
        ++i;
    }
    return true;
}

static size_t string_length(const char *text) {
    size_t length = 0u;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool copy_mount_path(char destination[AURORA_FS_MOUNT_PATH_MAX], const char *source) {
    if (source == NULL || source[0] != '/') {
        return false;
    }

    size_t length = string_length(source);
    if (length == 0u || length >= AURORA_FS_MOUNT_PATH_MAX) {
        return false;
    }

    for (size_t i = 0u; i <= length; ++i) {
        destination[i] = source[i];
    }
    return true;
}

static bool path_prefix_match(const char *path, const char *mount_path, size_t mount_length) {
    for (size_t i = 0u; i < mount_length; ++i) {
        if (path[i] != mount_path[i]) {
            return false;
        }
    }

    if (mount_length == 1u && mount_path[0] == '/') {
        return path[0] == '/';
    }

    return path[mount_length] == '\0' || path[mount_length] == '/';
}

void fs_mount_manager_init(void) {
    for (size_t i = 0u; i < AURORA_FS_MOUNT_MAX; ++i) {
        mounts[i].active = false;
        mounts[i].mount_path[0] = '\0';
        mounts[i].driver = NULL;
        mounts[i].context = NULL;
        mounts[i].access = AURORA_FS_PROBE_NO_MATCH;
    }
}

bool fs_mount_partition(
    const char *mount_path,
    const struct aurora_partition *partition,
    struct aurora_fs_mount **out_mount
) {
    if (mount_path == NULL || partition == NULL) {
        return false;
    }

    for (size_t i = 0u; i < AURORA_FS_MOUNT_MAX; ++i) {
        if (mounts[i].active && string_equal(mounts[i].mount_path, mount_path)) {
            return false;
        }
    }

    struct aurora_fs_match match;
    if (!fs_driver_detect(partition, &match) || match.driver == NULL ||
        match.driver->mount == NULL) {
        return false;
    }

    size_t free_index = AURORA_FS_MOUNT_MAX;
    for (size_t i = 0u; i < AURORA_FS_MOUNT_MAX; ++i) {
        if (!mounts[i].active) {
            free_index = i;
            break;
        }
    }

    if (free_index == AURORA_FS_MOUNT_MAX) {
        return false;
    }

    void *context = NULL;
    if (!match.driver->mount(partition, &context)) {
        return false;
    }

    struct aurora_fs_mount *mount = &mounts[free_index];
    if (!copy_mount_path(mount->mount_path, mount_path)) {
        if (match.driver->unmount != NULL) {
            match.driver->unmount(context);
        }
        return false;
    }

    mount->partition = *partition;
    mount->driver = match.driver;
    mount->access = match.access;
    mount->context = context;
    mount->active = true;

    if (out_mount != NULL) {
        *out_mount = mount;
    }
    return true;
}

bool fs_unmount(const char *mount_path) {
    if (mount_path == NULL) {
        return false;
    }

    for (size_t i = 0u; i < AURORA_FS_MOUNT_MAX; ++i) {
        struct aurora_fs_mount *mount = &mounts[i];
        if (!mount->active || !string_equal(mount->mount_path, mount_path)) {
            continue;
        }

        if (mount->driver != NULL && mount->driver->unmount != NULL) {
            mount->driver->unmount(mount->context);
        }

        mount->active = false;
        mount->mount_path[0] = '\0';
        mount->driver = NULL;
        mount->context = NULL;
        mount->access = AURORA_FS_PROBE_NO_MATCH;
        return true;
    }

    return false;
}

size_t fs_mount_count(void) {
    size_t count = 0u;
    for (size_t i = 0u; i < AURORA_FS_MOUNT_MAX; ++i) {
        if (mounts[i].active) {
            ++count;
        }
    }
    return count;
}

const struct aurora_fs_mount *fs_mount_at(size_t index) {
    size_t seen = 0u;
    for (size_t i = 0u; i < AURORA_FS_MOUNT_MAX; ++i) {
        if (!mounts[i].active) {
            continue;
        }
        if (seen == index) {
            return &mounts[i];
        }
        ++seen;
    }
    return NULL;
}

const struct aurora_fs_mount *fs_mount_resolve(
    const char *absolute_path,
    const char **out_relative_path
) {
    if (absolute_path == NULL || absolute_path[0] != '/') {
        return NULL;
    }

    const struct aurora_fs_mount *best = NULL;
    size_t best_length = 0u;

    for (size_t i = 0u; i < AURORA_FS_MOUNT_MAX; ++i) {
        const struct aurora_fs_mount *mount = &mounts[i];
        if (!mount->active) {
            continue;
        }

        size_t length = string_length(mount->mount_path);
        if (length >= best_length &&
            path_prefix_match(absolute_path, mount->mount_path, length)) {
            best = mount;
            best_length = length;
        }
    }

    if (best == NULL) {
        return NULL;
    }

    if (out_relative_path != NULL) {
        const char *relative = absolute_path + best_length;
        if (best_length == 1u && best->mount_path[0] == '/') {
            relative = absolute_path;
        } else if (relative[0] == '\0') {
            relative = "/";
        }
        *out_relative_path = relative;
    }

    return best;
}
