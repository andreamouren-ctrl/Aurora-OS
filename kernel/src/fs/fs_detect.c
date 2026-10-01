#include <stddef.h>
#include <stdint.h>

#include <aurora/fs_detect.h>

#define SECTOR_SIZE 512u

static bool bytes_equal(const uint8_t *data, const char *text, size_t length) {
    for (size_t i = 0u; i < length; ++i) {
        if (data[i] != (uint8_t)text[i]) {
            return false;
        }
    }
    return true;
}

static void set_detection(
    struct aurora_fs_detection *out,
    enum aurora_fs_kind kind,
    const char *name
) {
    out->kind = kind;
    out->name = name;
}

bool fs_detect_kind(
    const struct aurora_partition *partition,
    struct aurora_fs_detection *out_detection
) {
    if (partition == NULL || out_detection == NULL || partition->device == NULL ||
        partition->device->block_size != SECTOR_SIZE || partition->block_count == 0u) {
        return false;
    }

    set_detection(out_detection, AURORA_FS_KIND_UNKNOWN, "unknown");

    uint8_t sector[SECTOR_SIZE];
    if (!partition_read(partition, 0u, 1u, sector)) {
        return false;
    }

    if (bytes_equal(sector, "AURAFS1", 7u)) {
        set_detection(out_detection, AURORA_FS_KIND_AURORA, "AuroraFS");
        return true;
    }

    if (bytes_equal(sector + 3u, "EXFAT   ", 8u)) {
        set_detection(out_detection, AURORA_FS_KIND_EXFAT, "exFAT");
        return true;
    }

    if (bytes_equal(sector + 3u, "NTFS    ", 8u)) {
        set_detection(out_detection, AURORA_FS_KIND_NTFS, "NTFS");
        return true;
    }

    if (bytes_equal(sector, "XFSB", 4u)) {
        set_detection(out_detection, AURORA_FS_KIND_XFS, "XFS");
        return true;
    }

    if (sector[510] == 0x55u && sector[511] == 0xAAu) {
        if (bytes_equal(sector + 82u, "FAT32   ", 8u)) {
            set_detection(out_detection, AURORA_FS_KIND_FAT32, "FAT32");
            return true;
        }
        if (bytes_equal(sector + 54u, "FAT16   ", 8u)) {
            set_detection(out_detection, AURORA_FS_KIND_FAT16, "FAT16");
            return true;
        }
        if (bytes_equal(sector + 54u, "FAT12   ", 8u)) {
            set_detection(out_detection, AURORA_FS_KIND_FAT12, "FAT12");
            return true;
        }
    }

    if (partition->block_count > 2u && partition_read(partition, 2u, 1u, sector)) {
        if (sector[56u] == 0x53u && sector[57u] == 0xEFu) {
            set_detection(out_detection, AURORA_FS_KIND_EXT, "ext2/ext3/ext4");
            return true;
        }

        if ((sector[0] == 'H' && sector[1] == '+') ||
            (sector[0] == 'H' && sector[1] == 'X')) {
            set_detection(out_detection, AURORA_FS_KIND_HFS_PLUS, "HFS+");
            return true;
        }
    }

    if (partition->block_count > 16u && partition_read(partition, 16u, 1u, sector)) {
        if (bytes_equal(sector + 1u, "CD001", 5u)) {
            set_detection(out_detection, AURORA_FS_KIND_ISO9660, "ISO9660");
            return true;
        }
        if (bytes_equal(sector + 1u, "NSR02", 5u) ||
            bytes_equal(sector + 1u, "NSR03", 5u)) {
            set_detection(out_detection, AURORA_FS_KIND_UDF, "UDF");
            return true;
        }
    }

    if (partition->block_count > 128u && partition_read(partition, 128u, 1u, sector)) {
        if (bytes_equal(sector + 64u, "_BHRfS_M", 8u)) {
            set_detection(out_detection, AURORA_FS_KIND_BTRFS, "Btrfs");
            return true;
        }
    }

    if (sector[32u] == 'N' && sector[33u] == 'X' &&
        sector[34u] == 'S' && sector[35u] == 'B') {
        set_detection(out_detection, AURORA_FS_KIND_APFS, "APFS");
        return true;
    }

    return true;
}
