#include <stddef.h>
#include <stdint.h>

#include <aurora/exfat.h>
#include <aurora/heap.h>
#include <aurora/partition.h>

#define EXFAT_ENTRY_SIZE 32u
#define EXFAT_ENTRY_END 0x00u
#define EXFAT_ENTRY_FILE 0x85u
#define EXFAT_ENTRY_STREAM 0xC0u
#define EXFAT_ENTRY_FILENAME 0xC1u
#define EXFAT_ATTR_DIRECTORY 0x0010u
#define EXFAT_STREAM_NO_FAT_CHAIN 0x02u
#define EXFAT_CLUSTER_END 0xFFFFFFF8u
#define EXFAT_NAME_UNITS 255u

struct exfat_context {
    struct aurora_partition partition;
    uint32_t sector_size;
    uint32_t device_block_size;
    uint32_t sectors_per_cluster;
    uint64_t cluster_size;
    uint32_t fat_offset;
    uint32_t fat_length;
    uint32_t cluster_heap_offset;
    uint32_t cluster_count;
    uint32_t root_cluster;
};

struct exfat_node {
    char name[AURORA_FS_NAME_MAX];
    uint16_t attributes;
    uint32_t first_cluster;
    uint64_t data_length;
    uint64_t valid_data_length;
    bool no_fat_chain;
};

struct exfat_dir_ref {
    uint32_t first_cluster;
    uint64_t data_length;
    bool no_fat_chain;
    bool root;
};

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4u) << 32);
}

static bool bytes_equal(const uint8_t *data, const char *text, size_t length) {
    for (size_t i = 0u; i < length; ++i) {
        if (data[i] != (uint8_t)text[i]) {
            return false;
        }
    }
    return true;
}

static char upper_ascii(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }
    return ch;
}

static bool string_equal_ci(const char *a, const char *b) {
    size_t i = 0u;
    for (;;) {
        char ca = upper_ascii(a[i]);
        char cb = upper_ascii(b[i]);
        if (ca != cb) {
            return false;
        }
        if (ca == '\0') {
            return true;
        }
        ++i;
    }
}

static bool cluster_valid(const struct exfat_context *ctx, uint32_t cluster) {
    return cluster >= 2u && cluster < ctx->cluster_count + 2u;
}

static uint64_t cluster_byte_offset(
    const struct exfat_context *ctx,
    uint32_t cluster
) {
    return (uint64_t)ctx->cluster_heap_offset * ctx->sector_size
        + (uint64_t)(cluster - 2u) * ctx->cluster_size;
}

static bool read_partition_bytes(
    const struct exfat_context *ctx,
    uint64_t byte_offset,
    void *buffer,
    size_t length
) {
    uint8_t *out = (uint8_t *)buffer;
    size_t copied = 0u;

    while (copied < length) {
        uint64_t absolute = byte_offset + copied;
        uint64_t block = absolute / ctx->device_block_size;
        uint32_t within = (uint32_t)(absolute % ctx->device_block_size);
        uint8_t sector[4096];

        if (ctx->device_block_size > sizeof(sector) ||
            !partition_read(&ctx->partition, block, 1u, sector)) {
            return false;
        }

        size_t available = ctx->device_block_size - within;
        size_t remaining = length - copied;
        size_t take = available < remaining ? available : remaining;

        for (size_t i = 0u; i < take; ++i) {
            out[copied + i] = sector[within + i];
        }
        copied += take;
    }

    return true;
}

static bool next_cluster(
    const struct exfat_context *ctx,
    uint32_t cluster,
    uint32_t *out_next
) {
    uint64_t fat_byte = (uint64_t)ctx->fat_offset * ctx->sector_size
        + (uint64_t)cluster * 4u;
    uint8_t raw[4];

    if (!read_partition_bytes(ctx, fat_byte, raw, sizeof(raw))) {
        return false;
    }

    uint32_t next = le32(raw);
    if (next < 2u) {
        return false;
    }

    *out_next = next;
    return true;
}

static bool cluster_at_index(
    const struct exfat_context *ctx,
    const struct exfat_dir_ref *dir,
    uint64_t index,
    uint32_t *out_cluster
) {
    uint32_t cluster = dir->first_cluster;
    if (!cluster_valid(ctx, cluster)) {
        return false;
    }

    if (dir->no_fat_chain) {
        uint64_t value = (uint64_t)cluster + index;
        if (value > UINT32_MAX || !cluster_valid(ctx, (uint32_t)value)) {
            return false;
        }
        *out_cluster = (uint32_t)value;
        return true;
    }

    for (uint64_t i = 0u; i < index; ++i) {
        uint32_t next;
        if (!next_cluster(ctx, cluster, &next) || next >= EXFAT_CLUSTER_END ||
            !cluster_valid(ctx, next)) {
            return false;
        }
        cluster = next;
    }

    *out_cluster = cluster;
    return true;
}

static bool read_dir_raw(
    const struct exfat_context *ctx,
    const struct exfat_dir_ref *dir,
    uint64_t raw_index,
    uint8_t out[EXFAT_ENTRY_SIZE]
) {
    uint64_t byte_index = raw_index * EXFAT_ENTRY_SIZE;
    if (!dir->root && dir->data_length != 0u && byte_index >= dir->data_length) {
        return false;
    }

    uint64_t cluster_index = byte_index / ctx->cluster_size;
    uint64_t offset_in_cluster = byte_index % ctx->cluster_size;
    uint32_t cluster;

    if (!cluster_at_index(ctx, dir, cluster_index, &cluster)) {
        return false;
    }

    return read_partition_bytes(
        ctx,
        cluster_byte_offset(ctx, cluster) + offset_in_cluster,
        out,
        EXFAT_ENTRY_SIZE
    );
}

static bool utf8_append(
    char *out,
    size_t capacity,
    size_t *position,
    uint32_t codepoint
) {
    size_t needed = codepoint <= 0x7Fu ? 1u
        : codepoint <= 0x7FFu ? 2u
        : codepoint <= 0xFFFFu ? 3u : 4u;

    if (codepoint > 0x10FFFFu) {
        codepoint = 0xFFFDu;
        needed = 3u;
    }

    if (*position + needed >= capacity) {
        return false;
    }

    if (needed == 1u) {
        out[(*position)++] = (char)codepoint;
    } else if (needed == 2u) {
        out[(*position)++] = (char)(0xC0u | (codepoint >> 6));
        out[(*position)++] = (char)(0x80u | (codepoint & 0x3Fu));
    } else if (needed == 3u) {
        out[(*position)++] = (char)(0xE0u | (codepoint >> 12));
        out[(*position)++] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[(*position)++] = (char)(0x80u | (codepoint & 0x3Fu));
    } else {
        out[(*position)++] = (char)(0xF0u | (codepoint >> 18));
        out[(*position)++] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
        out[(*position)++] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[(*position)++] = (char)(0x80u | (codepoint & 0x3Fu));
    }

    return true;
}

static bool name_to_utf8(
    const uint16_t units[EXFAT_NAME_UNITS],
    uint8_t unit_count,
    char out[AURORA_FS_NAME_MAX]
) {
    size_t position = 0u;

    for (uint16_t i = 0u; i < unit_count; ++i) {
        uint16_t unit = units[i];
        uint32_t codepoint = unit;

        if (unit >= 0xD800u && unit <= 0xDBFFu) {
            if ((uint16_t)(i + 1u) < unit_count) {
                uint16_t low = units[i + 1u];
                if (low >= 0xDC00u && low <= 0xDFFFu) {
                    codepoint = 0x10000u
                        + (((uint32_t)unit - 0xD800u) << 10)
                        + ((uint32_t)low - 0xDC00u);
                    ++i;
                } else {
                    codepoint = 0xFFFDu;
                }
            } else {
                codepoint = 0xFFFDu;
            }
        } else if (unit >= 0xDC00u && unit <= 0xDFFFu) {
            codepoint = 0xFFFDu;
        }

        if (!utf8_append(out, AURORA_FS_NAME_MAX, &position, codepoint)) {
            return false;
        }
    }

    out[position] = '\0';
    return true;
}

static uint16_t checksum_step(uint16_t checksum, uint8_t value) {
    return (uint16_t)(((checksum & 1u) != 0u ? 0x8000u : 0u)
        + (checksum >> 1) + value);
}

static bool parse_file_set(
    const struct exfat_context *ctx,
    const struct exfat_dir_ref *dir,
    uint64_t primary_index,
    const uint8_t primary[EXFAT_ENTRY_SIZE],
    struct exfat_node *out_node,
    uint64_t *out_next_index
) {
    uint8_t secondary_count = primary[1u];
    if (secondary_count < 2u || secondary_count > 20u) {
        return false;
    }

    uint16_t stored_checksum = le16(primary + 2u);
    uint16_t checksum = 0u;
    for (size_t i = 0u; i < EXFAT_ENTRY_SIZE; ++i) {
        if (i == 2u || i == 3u) {
            continue;
        }
        checksum = checksum_step(checksum, primary[i]);
    }

    bool have_stream = false;
    uint8_t name_length = 0u;
    uint16_t name_units[EXFAT_NAME_UNITS];
    for (size_t i = 0u; i < EXFAT_NAME_UNITS; ++i) {
        name_units[i] = 0u;
    }
    size_t name_written = 0u;

    struct exfat_node node = { 0 };
    node.attributes = le16(primary + 4u);

    for (uint8_t secondary = 1u; secondary <= secondary_count; ++secondary) {
        uint8_t raw[EXFAT_ENTRY_SIZE];
        if (!read_dir_raw(ctx, dir, primary_index + secondary, raw)) {
            return false;
        }

        for (size_t i = 0u; i < EXFAT_ENTRY_SIZE; ++i) {
            checksum = checksum_step(checksum, raw[i]);
        }

        if (raw[0] == EXFAT_ENTRY_STREAM) {
            have_stream = true;
            node.no_fat_chain = (raw[1u] & EXFAT_STREAM_NO_FAT_CHAIN) != 0u;
            name_length = raw[3u];
            node.valid_data_length = le64(raw + 8u);
            node.first_cluster = le32(raw + 20u);
            node.data_length = le64(raw + 24u);
        } else if (raw[0] == EXFAT_ENTRY_FILENAME) {
            for (size_t i = 0u; i < 15u && name_written < EXFAT_NAME_UNITS; ++i) {
                name_units[name_written++] = le16(raw + 2u + i * 2u);
            }
        }
    }

    if (!have_stream || checksum != stored_checksum ||
        name_length == 0u || name_length > EXFAT_NAME_UNITS ||
        name_written < name_length || !cluster_valid(ctx, node.first_cluster) ||
        !name_to_utf8(name_units, name_length, node.name)) {
        return false;
    }

    *out_node = node;
    *out_next_index = primary_index + (uint64_t)secondary_count + 1u;
    return true;
}

static bool directory_find_or_index(
    const struct exfat_context *ctx,
    const struct exfat_dir_ref *dir,
    const char *name,
    uint64_t target_visible,
    bool by_name,
    struct exfat_node *out_node
) {
    uint64_t raw_index = 0u;
    uint64_t visible = 0u;
    uint64_t guard_limit = dir->root
        ? (uint64_t)ctx->cluster_count * (ctx->cluster_size / EXFAT_ENTRY_SIZE)
        : (dir->data_length / EXFAT_ENTRY_SIZE) + 1u;

    while (raw_index < guard_limit) {
        uint8_t raw[EXFAT_ENTRY_SIZE];
        if (!read_dir_raw(ctx, dir, raw_index, raw)) {
            return false;
        }

        if (raw[0] == EXFAT_ENTRY_END) {
            return false;
        }

        if (raw[0] != EXFAT_ENTRY_FILE) {
            ++raw_index;
            continue;
        }

        struct exfat_node node;
        uint64_t next_index;
        if (!parse_file_set(ctx, dir, raw_index, raw, &node, &next_index)) {
            return false;
        }

        if ((by_name && string_equal_ci(node.name, name)) ||
            (!by_name && visible == target_visible)) {
            *out_node = node;
            return true;
        }

        ++visible;
        raw_index = next_index;
    }

    return false;
}

static bool resolve_path(
    const struct exfat_context *ctx,
    const char *path,
    struct exfat_node *out_node,
    bool *out_is_root
) {
    if (path == NULL || out_node == NULL || out_is_root == NULL || path[0] != '/') {
        return false;
    }

    if (path[1] == '\0') {
        *out_is_root = true;
        return true;
    }

    *out_is_root = false;
    struct exfat_dir_ref dir = {
        .first_cluster = ctx->root_cluster,
        .data_length = 0u,
        .no_fat_chain = false,
        .root = true
    };

    size_t position = 1u;
    for (;;) {
        char component[AURORA_FS_NAME_MAX];
        size_t length = 0u;

        while (path[position] != '\0' && path[position] != '/') {
            if (length + 1u >= sizeof(component)) {
                return false;
            }
            component[length++] = path[position++];
        }
        component[length] = '\0';

        if (length == 0u) {
            return false;
        }

        struct exfat_node node;
        if (!directory_find_or_index(ctx, &dir, component, 0u, true, &node)) {
            return false;
        }

        while (path[position] == '/') {
            ++position;
        }

        if (path[position] == '\0') {
            *out_node = node;
            return true;
        }

        if ((node.attributes & EXFAT_ATTR_DIRECTORY) == 0u) {
            return false;
        }

        dir.first_cluster = node.first_cluster;
        dir.data_length = node.data_length;
        dir.no_fat_chain = node.no_fat_chain;
        dir.root = false;
    }
}

static enum aurora_fs_probe_result exfat_probe(
    const struct aurora_partition *partition
) {
    if (partition == NULL || partition->device == NULL ||
        partition->device->block_size > 4096u ||
        partition->device->block_size == 0u) {
        return AURORA_FS_PROBE_NO_MATCH;
    }

    uint8_t boot[512];
    if (partition->device->block_size != 512u ||
        !partition_read(partition, 0u, 1u, boot)) {
        return AURORA_FS_PROBE_NO_MATCH;
    }

    if (!bytes_equal(boot + 3u, "EXFAT   ", 8u)) {
        return AURORA_FS_PROBE_NO_MATCH;
    }

    uint8_t sector_shift = boot[108u];
    uint8_t cluster_shift = boot[109u];
    if (sector_shift < 9u || sector_shift > 12u ||
        cluster_shift > 25u || sector_shift + cluster_shift > 25u ||
        boot[110u] == 0u || le32(boot + 92u) == 0u) {
        return AURORA_FS_PROBE_NO_MATCH;
    }

    return AURORA_FS_PROBE_MATCH_READ_ONLY;
}

static bool exfat_mount(
    const struct aurora_partition *partition,
    void **out_context
) {
    if (out_context == NULL || exfat_probe(partition) == AURORA_FS_PROBE_NO_MATCH) {
        return false;
    }

    uint8_t boot[512];
    if (!partition_read(partition, 0u, 1u, boot)) {
        return false;
    }

    struct exfat_context *ctx = kheap_alloc(sizeof(*ctx), 16u);
    if (ctx == NULL) {
        return false;
    }

    ctx->partition = *partition;
    ctx->device_block_size = partition->device->block_size;
    ctx->sector_size = 1u << boot[108u];
    ctx->sectors_per_cluster = 1u << boot[109u];
    ctx->cluster_size = (uint64_t)ctx->sector_size * ctx->sectors_per_cluster;
    ctx->fat_offset = le32(boot + 80u);
    ctx->fat_length = le32(boot + 84u);
    ctx->cluster_heap_offset = le32(boot + 88u);
    ctx->cluster_count = le32(boot + 92u);
    ctx->root_cluster = le32(boot + 96u);

    uint64_t volume_length = le64(boot + 72u);
    uint64_t volume_bytes = volume_length * ctx->sector_size;
    uint64_t partition_bytes = partition->block_count * ctx->device_block_size;

    if (ctx->sector_size < ctx->device_block_size ||
        (ctx->sector_size % ctx->device_block_size) != 0u ||
        ctx->fat_length == 0u || ctx->cluster_count == 0u ||
        !cluster_valid(ctx, ctx->root_cluster) || volume_bytes > partition_bytes) {
        return false;
    }

    *out_context = ctx;
    return true;
}

static void exfat_unmount(void *context) {
    (void)context;
}

static bool exfat_stat(
    void *context,
    const char *path,
    struct aurora_fs_stat *out_stat
) {
    if (context == NULL || out_stat == NULL) {
        return false;
    }

    struct exfat_context *ctx = (struct exfat_context *)context;
    struct exfat_node node;
    bool is_root;
    if (!resolve_path(ctx, path, &node, &is_root)) {
        return false;
    }

    out_stat->type = is_root || (node.attributes & EXFAT_ATTR_DIRECTORY) != 0u
        ? AURORA_FS_ENTRY_DIRECTORY : AURORA_FS_ENTRY_FILE;
    out_stat->size = is_root ? 0u : node.data_length;
    out_stat->allocated_size = 0u;
    out_stat->created_time_ns = 0u;
    out_stat->modified_time_ns = 0u;
    out_stat->accessed_time_ns = 0u;
    out_stat->filesystem_id = is_root ? ctx->root_cluster : node.first_cluster;
    return true;
}

static bool exfat_readdir(
    void *context,
    const char *path,
    uint64_t index,
    struct aurora_fs_dirent *out_entry
) {
    if (context == NULL || path == NULL || out_entry == NULL) {
        return false;
    }

    struct exfat_context *ctx = (struct exfat_context *)context;
    struct exfat_dir_ref dir = {
        .first_cluster = ctx->root_cluster,
        .data_length = 0u,
        .no_fat_chain = false,
        .root = true
    };

    if (!(path[0] == '/' && path[1] == '\0')) {
        struct exfat_node node;
        bool is_root;
        if (!resolve_path(ctx, path, &node, &is_root) || is_root ||
            (node.attributes & EXFAT_ATTR_DIRECTORY) == 0u) {
            return false;
        }

        dir.first_cluster = node.first_cluster;
        dir.data_length = node.data_length;
        dir.no_fat_chain = node.no_fat_chain;
        dir.root = false;
    }

    struct exfat_node node;
    if (!directory_find_or_index(ctx, &dir, NULL, index, false, &node)) {
        return false;
    }

    size_t i = 0u;
    for (; i + 1u < AURORA_FS_NAME_MAX && node.name[i] != '\0'; ++i) {
        out_entry->name[i] = node.name[i];
    }
    out_entry->name[i] = '\0';
    out_entry->type = (node.attributes & EXFAT_ATTR_DIRECTORY) != 0u
        ? AURORA_FS_ENTRY_DIRECTORY : AURORA_FS_ENTRY_FILE;
    out_entry->size = node.data_length;
    out_entry->filesystem_id = node.first_cluster;
    return true;
}

static bool exfat_read(
    void *context,
    const char *path,
    uint64_t offset,
    void *buffer,
    size_t length,
    size_t *out_read
) {
    if (context == NULL || path == NULL || out_read == NULL ||
        (buffer == NULL && length != 0u)) {
        return false;
    }

    *out_read = 0u;
    struct exfat_context *ctx = (struct exfat_context *)context;
    struct exfat_node node;
    bool is_root;
    if (!resolve_path(ctx, path, &node, &is_root) || is_root ||
        (node.attributes & EXFAT_ATTR_DIRECTORY) != 0u || offset > node.data_length) {
        return false;
    }

    if (length == 0u || offset == node.data_length) {
        return true;
    }

    uint64_t remaining = node.data_length - offset;
    size_t wanted = length;
    if ((uint64_t)wanted > remaining) {
        wanted = (size_t)remaining;
    }

    uint64_t cluster_index = offset / ctx->cluster_size;
    uint64_t offset_in_cluster = offset % ctx->cluster_size;
    struct exfat_dir_ref data_ref = {
        .first_cluster = node.first_cluster,
        .data_length = node.data_length,
        .no_fat_chain = node.no_fat_chain,
        .root = false
    };

    uint8_t *out = (uint8_t *)buffer;
    size_t copied = 0u;

    while (copied < wanted) {
        uint32_t cluster;
        if (!cluster_at_index(ctx, &data_ref, cluster_index, &cluster)) {
            return false;
        }

        uint64_t in_cluster = cluster_index == offset / ctx->cluster_size
            ? offset_in_cluster : 0u;
        size_t take = (size_t)(ctx->cluster_size - in_cluster);
        if (take > wanted - copied) {
            take = wanted - copied;
        }

        if (!read_partition_bytes(
                ctx,
                cluster_byte_offset(ctx, cluster) + in_cluster,
                out + copied,
                take)) {
            return false;
        }

        copied += take;
        ++cluster_index;
    }

    *out_read = copied;
    return true;
}

static const struct aurora_fs_driver exfat = {
    .name = "exFAT",
    .probe = exfat_probe,
    .mount = exfat_mount,
    .unmount = exfat_unmount,
    .stat = exfat_stat,
    .readdir = exfat_readdir,
    .read = exfat_read,
    .write = NULL
};

const struct aurora_fs_driver *exfat_driver(void) {
    return &exfat;
}
