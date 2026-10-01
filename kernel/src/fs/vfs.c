#include <stddef.h>
#include <stdint.h>

#include <aurora/fs_mount.h>
#include <aurora/vfs.h>

struct bootstrap_vfs_file {
    bool used;
    char path[AURORA_VFS_PATH_MAX];
    uint8_t data[AURORA_VFS_BOOTSTRAP_DATA_MAX];
    size_t size;
};

static struct bootstrap_vfs_file bootstrap_files[
    AURORA_VFS_BOOTSTRAP_FILE_MAX
];

static bool vfs_initialized;

static size_t vfs_string_length(const char *text) {
    size_t length = 0u;

    if (text == NULL) {
        return 0u;
    }

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

static bool vfs_paths_equal(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }

    size_t index = 0u;

    while (a[index] != '\0' && b[index] != '\0') {
        if (a[index] != b[index]) {
            return false;
        }

        ++index;
    }

    return a[index] == '\0' && b[index] == '\0';
}

static bool vfs_path_valid(const char *path) {
    size_t length = vfs_string_length(path);

    if (length < 2u || length >= AURORA_VFS_PATH_MAX) {
        return false;
    }

    if (path[0] != '/') {
        return false;
    }

    if (path[length - 1u] == '/') {
        return false;
    }

    for (size_t i = 0u; i < length; ++i) {
        char ch = path[i];

        if (ch == '\\' || ch == '\n' || ch == '\r' || ch == '\t') {
            return false;
        }

        if (i + 1u < length && path[i] == '/' && path[i + 1u] == '/') {
            return false;
        }
    }

    return true;
}

static struct bootstrap_vfs_file *vfs_find_file(const char *path) {
    for (size_t i = 0u; i < AURORA_VFS_BOOTSTRAP_FILE_MAX; ++i) {
        if (!bootstrap_files[i].used) {
            continue;
        }

        if (vfs_paths_equal(bootstrap_files[i].path, path)) {
            return &bootstrap_files[i];
        }
    }

    return NULL;
}

static struct bootstrap_vfs_file *vfs_find_free_slot(void) {
    for (size_t i = 0u; i < AURORA_VFS_BOOTSTRAP_FILE_MAX; ++i) {
        if (!bootstrap_files[i].used) {
            return &bootstrap_files[i];
        }
    }

    return NULL;
}

static enum aurora_vfs_node_type vfs_node_type_from_fs(
    enum aurora_fs_entry_type type
) {
    switch (type) {
        case AURORA_FS_ENTRY_FILE:
            return AURORA_VFS_NODE_FILE;
        case AURORA_FS_ENTRY_DIRECTORY:
            return AURORA_VFS_NODE_DIRECTORY;
        default:
            return AURORA_VFS_NODE_NONE;
    }
}

static const struct aurora_fs_mount *vfs_resolve_mount(
    const char *path,
    const char **out_relative_path
) {
    if (path == NULL || path[0] != '/') {
        return NULL;
    }

    return fs_mount_resolve(path, out_relative_path);
}

bool vfs_init(void) {
    for (size_t i = 0u; i < AURORA_VFS_BOOTSTRAP_FILE_MAX; ++i) {
        bootstrap_files[i].used = false;
        bootstrap_files[i].path[0] = '\0';
        bootstrap_files[i].size = 0u;
    }

    vfs_initialized = true;
    return true;
}

bool vfs_create_file(const char *path) {
    if (!vfs_initialized || !vfs_path_valid(path)) {
        return false;
    }

    if (vfs_resolve_mount(path, NULL) != NULL) {
        return false;
    }

    if (vfs_find_file(path) != NULL) {
        return false;
    }

    struct bootstrap_vfs_file *slot = vfs_find_free_slot();

    if (slot == NULL) {
        return false;
    }

    size_t length = vfs_string_length(path);

    for (size_t i = 0u; i <= length; ++i) {
        slot->path[i] = path[i];
    }

    slot->size = 0u;
    slot->used = true;
    return true;
}

bool vfs_remove(const char *path) {
    if (!vfs_initialized || path == NULL) {
        return false;
    }

    if (vfs_resolve_mount(path, NULL) != NULL) {
        return false;
    }

    struct bootstrap_vfs_file *file = vfs_find_file(path);

    if (file == NULL) {
        return false;
    }

    for (size_t i = 0u; i < file->size; ++i) {
        file->data[i] = 0u;
    }

    for (size_t i = 0u; i < AURORA_VFS_PATH_MAX; ++i) {
        file->path[i] = '\0';
    }

    file->size = 0u;
    file->used = false;
    return true;
}

bool vfs_stat(const char *path, struct aurora_vfs_stat *out_stat) {
    if (!vfs_initialized || path == NULL || out_stat == NULL) {
        return false;
    }

    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(
        path,
        &relative_path
    );

    if (mount != NULL) {
        if (mount->driver == NULL || mount->driver->stat == NULL) {
            return false;
        }

        struct aurora_fs_stat mounted_stat;
        if (!mount->driver->stat(
                mount->context,
                relative_path,
                &mounted_stat)) {
            return false;
        }

        enum aurora_vfs_node_type node_type = vfs_node_type_from_fs(
            mounted_stat.type
        );

        if (node_type == AURORA_VFS_NODE_NONE) {
            return false;
        }

        out_stat->type = node_type;
        out_stat->size = mounted_stat.size;
        return true;
    }

    if (vfs_paths_equal(path, "/")) {
        out_stat->type = AURORA_VFS_NODE_DIRECTORY;
        out_stat->size = 0u;
        return true;
    }

    struct bootstrap_vfs_file *file = vfs_find_file(path);

    if (file == NULL) {
        return false;
    }

    out_stat->type = AURORA_VFS_NODE_FILE;
    out_stat->size = (uint64_t)file->size;
    return true;
}

bool vfs_write_file(
    const char *path,
    const void *data,
    size_t length
) {
    if (!vfs_initialized || path == NULL || data == NULL) {
        return false;
    }

    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(
        path,
        &relative_path
    );

    if (mount != NULL) {
        if (mount->driver == NULL || mount->driver->write == NULL ||
            mount->access != AURORA_FS_PROBE_MATCH_READ_WRITE) {
            return false;
        }

        size_t written = 0u;
        return mount->driver->write(
                   mount->context,
                   relative_path,
                   0u,
                   data,
                   length,
                   &written) &&
               written == length;
    }

    if (length > AURORA_VFS_BOOTSTRAP_DATA_MAX) {
        return false;
    }

    struct bootstrap_vfs_file *file = vfs_find_file(path);

    if (file == NULL) {
        return false;
    }

    const uint8_t *source = (const uint8_t *)data;

    for (size_t i = 0u; i < length; ++i) {
        file->data[i] = source[i];
    }

    for (size_t i = length; i < file->size; ++i) {
        file->data[i] = 0u;
    }

    file->size = length;
    return true;
}

bool vfs_read_file(
    const char *path,
    void *buffer,
    size_t capacity,
    size_t *out_length
) {
    if (!vfs_initialized || path == NULL || buffer == NULL) {
        return false;
    }

    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(
        path,
        &relative_path
    );

    if (mount != NULL) {
        if (mount->driver == NULL || mount->driver->read == NULL) {
            return false;
        }

        return mount->driver->read(
            mount->context,
            relative_path,
            0u,
            buffer,
            capacity,
            out_length
        );
    }

    struct bootstrap_vfs_file *file = vfs_find_file(path);

    if (file == NULL || capacity < file->size) {
        return false;
    }

    uint8_t *destination = (uint8_t *)buffer;

    for (size_t i = 0u; i < file->size; ++i) {
        destination[i] = file->data[i];
    }

    if (out_length != NULL) {
        *out_length = file->size;
    }

    return true;
}

bool vfs_self_test(void) {
    static const char probe_path[] = "/.aurora-vfs-probe";
    static const uint8_t probe_data[] = {
        0x41u, 0x55u, 0x52u, 0x4Fu, 0x52u, 0x41u
    };

    uint8_t readback[sizeof(probe_data)];
    size_t read_length = 0u;
    struct aurora_vfs_stat stat;

    if (!vfs_create_file(probe_path)) {
        return false;
    }

    if (!vfs_write_file(probe_path, probe_data, sizeof(probe_data))) {
        vfs_remove(probe_path);
        return false;
    }

    if (!vfs_stat(probe_path, &stat) ||
        stat.type != AURORA_VFS_NODE_FILE ||
        stat.size != sizeof(probe_data)) {
        vfs_remove(probe_path);
        return false;
    }

    if (!vfs_read_file(
            probe_path,
            readback,
            sizeof(readback),
            &read_length) ||
        read_length != sizeof(probe_data)) {
        vfs_remove(probe_path);
        return false;
    }

    for (size_t i = 0u; i < sizeof(probe_data); ++i) {
        if (readback[i] != probe_data[i]) {
            vfs_remove(probe_path);
            return false;
        }
    }

    return vfs_remove(probe_path);
}
