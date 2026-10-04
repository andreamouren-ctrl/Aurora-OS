#ifndef AURORA_AURORA_FS_V2_ACL_H
#define AURORA_AURORA_FS_V2_ACL_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>

#define AURORA_FS_V2_ACL_MAX_ENTRIES 2u

#define AURORA_FS_V2_ACL_PERM_EXECUTE 0x1u
#define AURORA_FS_V2_ACL_PERM_WRITE   0x2u
#define AURORA_FS_V2_ACL_PERM_READ    0x4u
#define AURORA_FS_V2_ACL_PERM_MASK    0x7u

enum aurora_fs_v2_acl_subject_type {
    AURORA_FS_V2_ACL_SUBJECT_USER = 1,
    AURORA_FS_V2_ACL_SUBJECT_GROUP = 2
};

struct aurora_fs_v2_acl_entry {
    enum aurora_fs_v2_acl_subject_type subject_type;
    uint32_t subject_id;
    uint8_t permissions;
};

struct aurora_fs_v2_acl {
    uint8_t entry_count;
    struct aurora_fs_v2_acl_entry entries[AURORA_FS_V2_ACL_MAX_ENTRIES];
};

bool aurora_fs_v2_acl_read(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct aurora_fs_v2_acl *out_acl
);

bool aurora_fs_v2_acl_write(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct aurora_fs_v2_acl *acl
);

bool aurora_fs_v2_acl_clear(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index
);

bool aurora_fs_v2_acl_check_access(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint32_t effective_uid,
    uint32_t effective_gid,
    uint8_t requested_permissions
);

bool aurora_fs_v2_acl_self_test(void);

#endif
