#ifndef AURORA_AURORA_FS_V2_INTEGRITY_H
#define AURORA_AURORA_FS_V2_INTEGRITY_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>

enum aurora_fs_v2_integrity_error {
    AURORA_FS_V2_INTEGRITY_OK = 0,
    AURORA_FS_V2_INTEGRITY_IO_ERROR,
    AURORA_FS_V2_INTEGRITY_BAD_MAGIC,
    AURORA_FS_V2_INTEGRITY_BAD_VERSION,
    AURORA_FS_V2_INTEGRITY_BAD_BLOCK_SIZE,
    AURORA_FS_V2_INTEGRITY_BAD_SUPERBLOCK_CHECKSUM,
    AURORA_FS_V2_INTEGRITY_BAD_GEOMETRY,
    AURORA_FS_V2_INTEGRITY_BAD_TRANSACTION,
    AURORA_FS_V2_INTEGRITY_BAD_INODE,
    AURORA_FS_V2_INTEGRITY_BAD_METADATA,
    AURORA_FS_V2_INTEGRITY_BAD_ACL,
    AURORA_FS_V2_INTEGRITY_BAD_DIRECTORY,
    AURORA_FS_V2_INTEGRITY_BAD_REFERENCE,
    AURORA_FS_V2_INTEGRITY_BAD_BITMAP
};

struct aurora_fs_v2_integrity_report {
    enum aurora_fs_v2_integrity_error error;
    uint64_t total_blocks;
    uint64_t bitmap_start;
    uint64_t bitmap_blocks;
    uint64_t inode_start;
    uint64_t inode_blocks;
    uint64_t data_start;
    uint64_t root_object_id;
    uint64_t active_inodes;
    uint64_t directories_scanned;
    uint64_t directory_records_scanned;
    uint64_t failing_inode_index;
    uint64_t failing_record_index;
    bool transaction_clean;
};

bool aurora_fs_v2_integrity_check_core(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    struct aurora_fs_v2_integrity_report *out_report
);

bool aurora_fs_v2_integrity_check_full(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    struct aurora_fs_v2_integrity_report *out_report
);

bool aurora_fs_v2_integrity_self_test(void);

#endif
