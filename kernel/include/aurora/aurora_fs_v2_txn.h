#ifndef AURORA_AURORA_FS_V2_TXN_H
#define AURORA_AURORA_FS_V2_TXN_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>

enum aurora_fs_v2_txn_state {
    AURORA_FS_V2_TXN_CLEAN = 0,
    AURORA_FS_V2_TXN_PREPARED = 1,
    AURORA_FS_V2_TXN_COMMITTED = 2
};

enum aurora_fs_v2_txn_operation {
    AURORA_FS_V2_TXN_OP_NONE = 0,
    AURORA_FS_V2_TXN_OP_FILE_GROW = 1,
    AURORA_FS_V2_TXN_OP_FILE_SHRINK = 2,
    AURORA_FS_V2_TXN_OP_CREATE = 3,
    AURORA_FS_V2_TXN_OP_REMOVE = 4,
    AURORA_FS_V2_TXN_OP_RENAME = 5
};

#define AURORA_FS_V2_TXN_ROLLBACK_RANGE_CAPACITY 4u
#define AURORA_FS_V2_TXN_CLEANUP_RANGE_CAPACITY 4u

struct aurora_fs_v2_txn_range {
    uint64_t first_block;
    uint64_t block_count;
};

struct aurora_fs_v2_txn_record {
    enum aurora_fs_v2_txn_state state;
    enum aurora_fs_v2_txn_operation operation;
    uint64_t sequence;
    uint64_t inode_index;
    uint64_t old_root;
    uint64_t new_root;
    uint64_t old_size;
    uint64_t new_size;
    uint32_t rollback_range_count;
    uint32_t cleanup_range_count;
    struct aurora_fs_v2_txn_range rollback_ranges[AURORA_FS_V2_TXN_ROLLBACK_RANGE_CAPACITY];
    struct aurora_fs_v2_txn_range cleanup_ranges[AURORA_FS_V2_TXN_CLEANUP_RANGE_CAPACITY];
};

bool aurora_fs_v2_txn_load(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    struct aurora_fs_v2_txn_record *out_record
);

bool aurora_fs_v2_txn_prepare(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    const struct aurora_fs_v2_txn_record *record
);

bool aurora_fs_v2_txn_mark_committed(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t expected_sequence
);

bool aurora_fs_v2_txn_clear(
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t expected_sequence
);

#endif
