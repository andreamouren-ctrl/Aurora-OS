#ifndef AURORA_VFS_H
#define AURORA_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURORA_VFS_PATH_MAX 128u
#define AURORA_VFS_BOOTSTRAP_FILE_MAX 32u
#define AURORA_VFS_BOOTSTRAP_DATA_MAX 512u

enum aurora_vfs_node_type {
    AURORA_VFS_NODE_NONE = 0,
    AURORA_VFS_NODE_DIRECTORY,
    AURORA_VFS_NODE_FILE
};

struct aurora_vfs_stat {
    enum aurora_vfs_node_type type;
    uint64_t size;
    uint64_t allocated_size;
    uint64_t created_time_ns;
    uint64_t changed_time_ns;
    uint64_t modified_time_ns;
    uint64_t accessed_time_ns;
    uint64_t filesystem_id;
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    uint32_t link_count;
};

bool vfs_init(void);
bool vfs_create_file(const char *path);
bool vfs_create_directory(const char *path);
bool vfs_remove(const char *path);
bool vfs_rename(const char *old_path, const char *new_path);
bool vfs_truncate_file(const char *path, uint64_t size);
bool vfs_stat(const char *path, struct aurora_vfs_stat *out_stat);
bool vfs_chmod(const char *path, uint32_t mode);
bool vfs_chown(const char *path, uint32_t uid, uint32_t gid);

bool vfs_write_file(
    const char *path,
    const void *data,
    size_t length
);

bool vfs_read_file(
    const char *path,
    void *buffer,
    size_t capacity,
    size_t *out_length
);

bool vfs_self_test(void);

#endif
