#ifndef AURORA_AURORA_FS_V2_RECOVERY_H
#define AURORA_AURORA_FS_V2_RECOVERY_H

#include <stdbool.h>

#include <aurora/aurora_fs_v2.h>

bool aurora_fs_v2_recover_pending_transaction(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry
);

bool aurora_fs_v2_recovery_self_test(void);

#endif
