#ifndef AURORA_AURORA_FS_V2_FILE_IO_H
#define AURORA_AURORA_FS_V2_FILE_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>

bool aurora_fs_v2_file_read(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t offset,
    void *buffer,
    size_t length,
    size_t *out_read
);

bool aurora_fs_v2_file_write(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t offset,
    const void *buffer,
    size_t length,
    size_t *out_written
);

bool aurora_fs_v2_file_truncate(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t new_size
);

bool aurora_fs_v2_file_io_self_test(void);

#endif
