#include <stddef.h>
#include <stdint.h>

#include <aurora/block_device.h>
#include <aurora/fs_mount.h>
#include <aurora/vfs.h>

struct bootstrap_vfs_file {
    bool used;
    char path[AURORA_VFS_PATH_MAX];
    uint8_t data[AURORA_VFS_BOOTSTRAP_DATA_MAX];
    size_t size;
};

static struct bootstrap_vfs_file bootstrap_files[AURORA_VFS_BOOTSTRAP_FILE_MAX];
static bool vfs_initialized;

static void vfs_zero(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static size_t vfs_string_length(const char *text) {
    size_t length = 0u;
    if (text == NULL) return 0u;
    while (text[length] != '\0') ++length;
    return length;
}

static bool vfs_paths_equal(const char *a, const char *b) {
    if (a == NULL || b == NULL) return false;
    size_t index = 0u;
    while (a[index] != '\0' && b[index] != '\0') {
        if (a[index] != b[index]) return false;
        ++index;
    }
    return a[index] == '\0' && b[index] == '\0';
}

static bool vfs_path_valid(const char *path) {
    size_t length = vfs_string_length(path);
    if (length < 2u || length >= AURORA_VFS_PATH_MAX || path[0] != '/' ||
        path[length - 1u] == '/') return false;
    for (size_t i = 0u; i < length; ++i) {
        char ch = path[i];
        if (ch == '\\' || ch == '\n' || ch == '\r' || ch == '\t') return false;
        if (i + 1u < length && path[i] == '/' && path[i + 1u] == '/') return false;
    }
    return true;
}

static struct bootstrap_vfs_file *vfs_find_file(const char *path) {
    for (size_t i = 0u; i < AURORA_VFS_BOOTSTRAP_FILE_MAX; ++i) {
        if (bootstrap_files[i].used && vfs_paths_equal(bootstrap_files[i].path, path))
            return &bootstrap_files[i];
    }
    return NULL;
}

static struct bootstrap_vfs_file *vfs_find_free_slot(void) {
    for (size_t i = 0u; i < AURORA_VFS_BOOTSTRAP_FILE_MAX; ++i)
        if (!bootstrap_files[i].used) return &bootstrap_files[i];
    return NULL;
}

static bool vfs_copy_path(char destination[AURORA_VFS_PATH_MAX], const char *path) {
    if (!vfs_path_valid(path)) return false;
    size_t length = vfs_string_length(path);
    for (size_t i = 0u; i <= length; ++i) destination[i] = path[i];
    return true;
}

static enum aurora_vfs_node_type vfs_node_type_from_fs(enum aurora_fs_entry_type type) {
    if (type == AURORA_FS_ENTRY_FILE) return AURORA_VFS_NODE_FILE;
    if (type == AURORA_FS_ENTRY_DIRECTORY) return AURORA_VFS_NODE_DIRECTORY;
    return AURORA_VFS_NODE_NONE;
}

static const struct aurora_fs_mount *vfs_resolve_mount(
    const char *path,
    const char **out_relative_path
) {
    if (path == NULL || path[0] != '/') return NULL;
    return fs_mount_resolve(path, out_relative_path);
}

static bool vfs_mount_is_writable(const struct aurora_fs_mount *mount) {
    return mount != NULL && mount->driver != NULL &&
        mount->access == AURORA_FS_PROBE_MATCH_READ_WRITE;
}

static bool vfs_flush_mount_device(const struct aurora_fs_mount *mount) {
    return mount != NULL && mount->partition.device != NULL &&
        block_device_flush(mount->partition.device);
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
    if (!vfs_initialized || !vfs_path_valid(path)) return false;
    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(path, &relative_path);
    if (mount != NULL)
        return vfs_mount_is_writable(mount) && mount->driver->create != NULL &&
            mount->driver->create(mount->context, relative_path, AURORA_FS_ENTRY_FILE);
    if (vfs_find_file(path) != NULL) return false;
    struct bootstrap_vfs_file *slot = vfs_find_free_slot();
    if (slot == NULL || !vfs_copy_path(slot->path, path)) return false;
    slot->size = 0u;
    slot->used = true;
    return true;
}

bool vfs_create_directory(const char *path) {
    if (!vfs_initialized || !vfs_path_valid(path)) return false;
    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(path, &relative_path);
    return vfs_mount_is_writable(mount) && mount->driver->create != NULL &&
        mount->driver->create(mount->context, relative_path, AURORA_FS_ENTRY_DIRECTORY);
}

bool vfs_remove(const char *path) {
    if (!vfs_initialized || !vfs_path_valid(path)) return false;
    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(path, &relative_path);
    if (mount != NULL)
        return vfs_mount_is_writable(mount) && mount->driver->remove != NULL &&
            mount->driver->remove(mount->context, relative_path);
    struct bootstrap_vfs_file *file = vfs_find_file(path);
    if (file == NULL) return false;
    vfs_zero(file->data, sizeof(file->data));
    vfs_zero(file->path, sizeof(file->path));
    file->size = 0u;
    file->used = false;
    return true;
}

bool vfs_rename(const char *old_path, const char *new_path) {
    if (!vfs_initialized || !vfs_path_valid(old_path) || !vfs_path_valid(new_path) ||
        vfs_paths_equal(old_path, new_path)) return false;
    const char *old_relative = NULL;
    const char *new_relative = NULL;
    const struct aurora_fs_mount *old_mount = vfs_resolve_mount(old_path, &old_relative);
    const struct aurora_fs_mount *new_mount = vfs_resolve_mount(new_path, &new_relative);
    if (old_mount != NULL || new_mount != NULL) {
        if (old_mount == NULL || new_mount == NULL || old_mount != new_mount ||
            !vfs_mount_is_writable(old_mount) || old_mount->driver->rename == NULL) return false;
        return old_mount->driver->rename(old_mount->context, old_relative, new_relative);
    }
    if (vfs_find_file(new_path) != NULL) return false;
    struct bootstrap_vfs_file *file = vfs_find_file(old_path);
    return file != NULL && vfs_copy_path(file->path, new_path);
}

bool vfs_truncate_file(const char *path, uint64_t size) {
    if (!vfs_initialized || !vfs_path_valid(path)) return false;
    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(path, &relative_path);
    if (mount != NULL)
        return vfs_mount_is_writable(mount) && mount->driver->truncate != NULL &&
            mount->driver->truncate(mount->context, relative_path, size);
    if (size > AURORA_VFS_BOOTSTRAP_DATA_MAX) return false;
    struct bootstrap_vfs_file *file = vfs_find_file(path);
    if (file == NULL) return false;
    size_t new_size = (size_t)size;
    if (new_size > file->size) {
        for (size_t i = file->size; i < new_size; ++i) file->data[i] = 0u;
    } else {
        for (size_t i = new_size; i < file->size; ++i) file->data[i] = 0u;
    }
    file->size = new_size;
    return true;
}

bool vfs_stat(const char *path, struct aurora_vfs_stat *out_stat) {
    if (!vfs_initialized || path == NULL || out_stat == NULL) return false;
    vfs_zero(out_stat, sizeof(*out_stat));
    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(path, &relative_path);
    if (mount != NULL) {
        if (mount->driver == NULL || mount->driver->stat == NULL) return false;
        struct aurora_fs_stat mounted_stat;
        if (!mount->driver->stat(mount->context, relative_path, &mounted_stat)) return false;
        enum aurora_vfs_node_type node_type = vfs_node_type_from_fs(mounted_stat.type);
        if (node_type == AURORA_VFS_NODE_NONE) return false;
        out_stat->type = node_type;
        out_stat->size = mounted_stat.size;
        out_stat->allocated_size = mounted_stat.allocated_size;
        out_stat->created_time_ns = mounted_stat.created_time_ns;
        out_stat->changed_time_ns = mounted_stat.changed_time_ns;
        out_stat->modified_time_ns = mounted_stat.modified_time_ns;
        out_stat->accessed_time_ns = mounted_stat.accessed_time_ns;
        out_stat->filesystem_id = mounted_stat.filesystem_id;
        out_stat->uid = mounted_stat.uid;
        out_stat->gid = mounted_stat.gid;
        out_stat->mode = mounted_stat.mode;
        out_stat->link_count = mounted_stat.link_count;
        return true;
    }
    if (vfs_paths_equal(path, "/")) {
        out_stat->type = AURORA_VFS_NODE_DIRECTORY;
        out_stat->link_count = 1u;
        return true;
    }
    struct bootstrap_vfs_file *file = vfs_find_file(path);
    if (file == NULL) return false;
    out_stat->type = AURORA_VFS_NODE_FILE;
    out_stat->size = (uint64_t)file->size;
    out_stat->allocated_size = (uint64_t)file->size;
    out_stat->link_count = 1u;
    return true;
}

bool vfs_chmod(const char *path, uint32_t mode) {
    if (!vfs_initialized || !vfs_path_valid(path) || (mode & ~07777u) != 0u) return false;
    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(path, &relative_path);
    return vfs_mount_is_writable(mount) && mount->driver->chmod != NULL &&
        mount->driver->chmod(mount->context, relative_path, mode);
}

bool vfs_chown(const char *path, uint32_t uid, uint32_t gid) {
    if (!vfs_initialized || !vfs_path_valid(path)) return false;
    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(path, &relative_path);
    return vfs_mount_is_writable(mount) && mount->driver->chown != NULL &&
        mount->driver->chown(mount->context, relative_path, uid, gid);
}

bool vfs_sync(const char *path) {
    if (!vfs_initialized || path == NULL || path[0] != '/') return false;
    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(path, &relative_path);
    (void)relative_path;
    if (mount == NULL || mount->driver == NULL) return false;
    if (mount->driver->sync != NULL) return mount->driver->sync(mount->context);
    return vfs_flush_mount_device(mount);
}

bool vfs_fsync(const char *path) {
    if (!vfs_initialized || !vfs_path_valid(path)) return false;
    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(path, &relative_path);
    if (mount == NULL || mount->driver == NULL) return false;
    if (mount->driver->fsync != NULL)
        return mount->driver->fsync(mount->context, relative_path);
    if (mount->driver->stat == NULL) return false;
    struct aurora_fs_stat stat;
    if (!mount->driver->stat(mount->context, relative_path, &stat)) return false;
    return vfs_flush_mount_device(mount);
}

bool vfs_fdatasync(const char *path) {
    if (!vfs_initialized || !vfs_path_valid(path)) return false;
    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(path, &relative_path);
    if (mount == NULL || mount->driver == NULL) return false;
    if (mount->driver->fdatasync != NULL)
        return mount->driver->fdatasync(mount->context, relative_path);
    if (mount->driver->stat == NULL) return false;
    struct aurora_fs_stat stat;
    if (!mount->driver->stat(mount->context, relative_path, &stat)) return false;
    return vfs_flush_mount_device(mount);
}

bool vfs_write_file(const char *path, const void *data, size_t length) {
    if (!vfs_initialized || path == NULL || data == NULL) return false;
    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(path, &relative_path);
    if (mount != NULL) {
        if (!vfs_mount_is_writable(mount) || mount->driver->write == NULL) return false;
        size_t written = 0u;
        return mount->driver->write(mount->context, relative_path, 0u, data, length, &written) &&
            written == length;
    }
    if (length > AURORA_VFS_BOOTSTRAP_DATA_MAX) return false;
    struct bootstrap_vfs_file *file = vfs_find_file(path);
    if (file == NULL) return false;
    const uint8_t *source = data;
    for (size_t i = 0u; i < length; ++i) file->data[i] = source[i];
    for (size_t i = length; i < file->size; ++i) file->data[i] = 0u;
    file->size = length;
    return true;
}

bool vfs_read_file(const char *path, void *buffer, size_t capacity, size_t *out_length) {
    if (!vfs_initialized || path == NULL || buffer == NULL) return false;
    const char *relative_path = NULL;
    const struct aurora_fs_mount *mount = vfs_resolve_mount(path, &relative_path);
    if (mount != NULL) {
        if (mount->driver == NULL || mount->driver->read == NULL) return false;
        return mount->driver->read(mount->context, relative_path, 0u, buffer, capacity, out_length);
    }
    struct bootstrap_vfs_file *file = vfs_find_file(path);
    if (file == NULL || capacity < file->size) return false;
    uint8_t *destination = buffer;
    for (size_t i = 0u; i < file->size; ++i) destination[i] = file->data[i];
    if (out_length != NULL) *out_length = file->size;
    return true;
}

bool vfs_self_test(void) {
    static const char probe_path[] = "/.aurora-vfs-probe";
    static const char renamed_path[] = "/.aurora-vfs-probe-renamed";
    static const uint8_t probe_data[] = {0x41u, 0x55u, 0x52u, 0x4Fu, 0x52u, 0x41u};
    uint8_t readback[sizeof(probe_data)];
    size_t read_length = 0u;
    struct aurora_vfs_stat stat;
    if (!vfs_create_file(probe_path)) return false;
    if (!vfs_write_file(probe_path, probe_data, sizeof(probe_data))) {
        vfs_remove(probe_path);
        return false;
    }
    if (!vfs_rename(probe_path, renamed_path) || vfs_stat(probe_path, &stat)) {
        vfs_remove(probe_path);
        vfs_remove(renamed_path);
        return false;
    }
    if (!vfs_truncate_file(renamed_path, 3u)) {
        vfs_remove(renamed_path);
        return false;
    }
    if (!vfs_stat(renamed_path, &stat) || stat.type != AURORA_VFS_NODE_FILE || stat.size != 3u) {
        vfs_remove(renamed_path);
        return false;
    }
    if (!vfs_read_file(renamed_path, readback, sizeof(readback), &read_length) || read_length != 3u) {
        vfs_remove(renamed_path);
        return false;
    }
    for (size_t i = 0u; i < read_length; ++i) {
        if (readback[i] != probe_data[i]) {
            vfs_remove(renamed_path);
            return false;
        }
    }
    if (!vfs_truncate_file(renamed_path, sizeof(probe_data))) {
        vfs_remove(renamed_path);
        return false;
    }
    if (!vfs_read_file(renamed_path, readback, sizeof(readback), &read_length) ||
        read_length != sizeof(probe_data)) {
        vfs_remove(renamed_path);
        return false;
    }
    for (size_t i = 3u; i < sizeof(probe_data); ++i) {
        if (readback[i] != 0u) {
            vfs_remove(renamed_path);
            return false;
        }
    }
    return vfs_remove(renamed_path);
}
