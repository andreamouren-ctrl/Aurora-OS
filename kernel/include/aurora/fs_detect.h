#ifndef AURORA_FS_DETECT_H
#define AURORA_FS_DETECT_H

#include <stdbool.h>

#include <aurora/partition.h>

enum aurora_fs_kind {
    AURORA_FS_KIND_UNKNOWN = 0,
    AURORA_FS_KIND_AURORA,
    AURORA_FS_KIND_FAT12,
    AURORA_FS_KIND_FAT16,
    AURORA_FS_KIND_FAT32,
    AURORA_FS_KIND_EXFAT,
    AURORA_FS_KIND_NTFS,
    AURORA_FS_KIND_EXT,
    AURORA_FS_KIND_XFS,
    AURORA_FS_KIND_BTRFS,
    AURORA_FS_KIND_HFS_PLUS,
    AURORA_FS_KIND_APFS,
    AURORA_FS_KIND_ISO9660,
    AURORA_FS_KIND_UDF
};

struct aurora_fs_detection {
    enum aurora_fs_kind kind;
    const char *name;
};

bool fs_detect_kind(
    const struct aurora_partition *partition,
    struct aurora_fs_detection *out_detection
);

#endif
