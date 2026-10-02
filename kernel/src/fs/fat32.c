#include <stddef.h>
#include <stdint.h>

#include <aurora/fat32.h>
#include <aurora/heap.h>
#include <aurora/partition.h>

#define FAT32_ENTRY_SIZE 32u
#define FAT32_ATTR_DIRECTORY 0x10u
#define FAT32_ATTR_VOLUME_ID 0x08u
#define FAT32_ATTR_LFN 0x0Fu
#define FAT32_CLUSTER_END 0x0FFFFFF8u
#define FAT32_CLUSTER_BAD 0x0FFFFFF7u
#define FAT32_LFN_UNITS 260u
#define FAT32_LFN_MAX_ENTRIES 20u
#define FAT32_MAX_BLOCK_SIZE 4096u

struct fat32_context {
    struct aurora_partition partition;
    uint32_t bytes_per_sector;
    uint32_t device_block_size;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint32_t first_data_sector;
    uint32_t total_clusters;
};

struct fat32_dir_entry {
    char name[AURORA_FS_NAME_MAX];
    uint8_t attributes;
    uint32_t first_cluster;
    uint32_t size;
};

struct fat32_lfn_state {
    bool active;
    uint8_t checksum;
    uint8_t expected_order;
    uint16_t units[FAT32_LFN_UNITS];
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

static bool valid_sector_size(uint32_t size) {
    return size >= 512u && size <= 4096u &&
        (size & (size - 1u)) == 0u;
}

static bool read_partition_bytes(
    const struct fat32_context *ctx,
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
        uint8_t raw[FAT32_MAX_BLOCK_SIZE];

        if (ctx->device_block_size > sizeof(raw) ||
            !partition_read(&ctx->partition, block, 1u, raw)) {
            return false;
        }

        size_t available = ctx->device_block_size - within;
        size_t remaining = length - copied;
        size_t take = available < remaining ? available : remaining;
        for (size_t i = 0u; i < take; ++i) {
            out[copied + i] = raw[within + i];
        }
        copied += take;
    }

    return true;
}

static bool read_boot_block(
    const struct aurora_partition *partition,
    uint8_t boot[FAT32_MAX_BLOCK_SIZE]
) {
    if (partition == NULL || partition->device == NULL ||
        partition->device->block_size < 512u ||
        partition->device->block_size > FAT32_MAX_BLOCK_SIZE) {
        return false;
    }
    return partition_read(partition, 0u, 1u, boot);
}

static bool cluster_valid(const struct fat32_context *ctx, uint32_t cluster) {
    return cluster >= 2u && cluster < ctx->total_clusters + 2u;
}

static uint64_t cluster_byte_offset(
    const struct fat32_context *ctx,
    uint32_t cluster
) {
    return (uint64_t)ctx->first_data_sector * ctx->bytes_per_sector
        + (uint64_t)(cluster - 2u) * ctx->sectors_per_cluster
            * ctx->bytes_per_sector;
}

static uint64_t cluster_size_bytes(const struct fat32_context *ctx) {
    return (uint64_t)ctx->sectors_per_cluster * ctx->bytes_per_sector;
}

static bool fat32_next_cluster(
    const struct fat32_context *ctx,
    uint32_t cluster,
    uint32_t *out_next
) {
    uint64_t fat_byte = (uint64_t)ctx->reserved_sectors * ctx->bytes_per_sector
        + (uint64_t)cluster * 4u;
    uint8_t raw[4];
    if (!read_partition_bytes(ctx, fat_byte, raw, sizeof(raw))) {
        return false;
    }

    uint32_t next = le32(raw) & 0x0FFFFFFFu;
    if (next == FAT32_CLUSTER_BAD || next < 2u) {
        return false;
    }

    *out_next = next;
    return true;
}

static void decode_short_name(const uint8_t raw[11], char out[AURORA_FS_NAME_MAX]) {
    size_t pos = 0u;
    for (size_t i = 0u; i < 8u && raw[i] != ' '; ++i) {
        out[pos++] = (char)raw[i];
    }

    bool has_extension = false;
    for (size_t i = 8u; i < 11u; ++i) {
        if (raw[i] != ' ') {
            has_extension = true;
            break;
        }
    }

    if (has_extension) {
        out[pos++] = '.';
        for (size_t i = 8u; i < 11u && raw[i] != ' '; ++i) {
            out[pos++] = (char)raw[i];
        }
    }
    out[pos] = '\0';
}

static uint8_t short_name_checksum(const uint8_t raw[11]) {
    uint8_t checksum = 0u;
    for (size_t i = 0u; i < 11u; ++i) {
        checksum = (uint8_t)(((checksum & 1u) != 0u ? 0x80u : 0u)
            + (checksum >> 1) + raw[i]);
    }
    return checksum;
}

static void lfn_clear(struct fat32_lfn_state *state) {
    state->active = false;
    state->checksum = 0u;
    state->expected_order = 0u;
    for (size_t i = 0u; i < FAT32_LFN_UNITS; ++i) {
        state->units[i] = 0xFFFFu;
    }
}

static void lfn_copy_fragment(
    struct fat32_lfn_state *state,
    const uint8_t *raw,
    uint8_t order
) {
    static const uint8_t offsets[13] = {
        1u, 3u, 5u, 7u, 9u,
        14u, 16u, 18u, 20u, 22u, 24u,
        28u, 30u
    };
    size_t base = ((size_t)order - 1u) * 13u;
    for (size_t i = 0u; i < 13u && base + i < FAT32_LFN_UNITS; ++i) {
        state->units[base + i] = le16(raw + offsets[i]);
    }
}

static void lfn_accept(struct fat32_lfn_state *state, const uint8_t *raw) {
    uint8_t order_byte = raw[0];
    uint8_t order = order_byte & 0x1Fu;
    bool is_last = (order_byte & 0x40u) != 0u;

    if (order == 0u || order > FAT32_LFN_MAX_ENTRIES) {
        lfn_clear(state);
        return;
    }

    if (is_last) {
        lfn_clear(state);
        state->active = true;
        state->checksum = raw[13u];
        state->expected_order = order;
    }

    if (!state->active || state->checksum != raw[13u] ||
        state->expected_order != order) {
        lfn_clear(state);
        return;
    }

    lfn_copy_fragment(state, raw, order);
    state->expected_order = (uint8_t)(order - 1u);
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

static bool lfn_to_utf8(
    const struct fat32_lfn_state *state,
    char out[AURORA_FS_NAME_MAX]
) {
    if (!state->active || state->expected_order != 0u) {
        return false;
    }

    size_t position = 0u;
    for (size_t i = 0u; i < FAT32_LFN_UNITS; ++i) {
        uint16_t unit = state->units[i];
        if (unit == 0x0000u || unit == 0xFFFFu) {
            break;
        }

        uint32_t codepoint = unit;
        if (unit >= 0xD800u && unit <= 0xDBFFu) {
            if (i + 1u < FAT32_LFN_UNITS) {
                uint16_t low = state->units[i + 1u];
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
    return position != 0u;
}

static void decode_entry(
    const uint8_t *raw,
    const struct fat32_lfn_state *lfn,
    struct fat32_dir_entry *out
) {
    bool used_lfn = false;
    if (lfn != NULL && lfn->active && lfn->expected_order == 0u &&
        short_name_checksum(raw) == lfn->checksum) {
        used_lfn = lfn_to_utf8(lfn, out->name);
    }
    if (!used_lfn) {
        decode_short_name(raw, out->name);
    }

    out->attributes = raw[11u];
    out->first_cluster = ((uint32_t)le16(raw + 20u) << 16)
        | le16(raw + 26u);
    out->size = le32(raw + 28u);
}

static bool read_directory_entry(
    const struct fat32_context *ctx,
    uint32_t directory_cluster,
    uint64_t target_index,
    struct fat32_dir_entry *out_entry
) {
    uint32_t cluster = directory_cluster;
    uint64_t visible_index = 0u;
    uint32_t guard = 0u;
    struct fat32_lfn_state lfn;
    lfn_clear(&lfn);

    while (cluster_valid(ctx, cluster) && guard++ <= ctx->total_clusters) {
        uint64_t cluster_bytes = cluster_size_bytes(ctx);
        uint64_t entry_count = cluster_bytes / FAT32_ENTRY_SIZE;
        uint64_t base = cluster_byte_offset(ctx, cluster);

        for (uint64_t index = 0u; index < entry_count; ++index) {
            uint8_t raw[FAT32_ENTRY_SIZE];
            if (!read_partition_bytes(
                    ctx,
                    base + index * FAT32_ENTRY_SIZE,
                    raw,
                    sizeof(raw))) {
                return false;
            }

            if (raw[0] == 0x00u) {
                return false;
            }
            if (raw[0] == 0xE5u) {
                lfn_clear(&lfn);
                continue;
            }
            if (raw[11u] == FAT32_ATTR_LFN) {
                lfn_accept(&lfn, raw);
                continue;
            }
            if ((raw[11u] & FAT32_ATTR_VOLUME_ID) != 0u) {
                lfn_clear(&lfn);
                continue;
            }

            if (visible_index == target_index) {
                decode_entry(raw, &lfn, out_entry);
                return true;
            }
            ++visible_index;
            lfn_clear(&lfn);
        }

        uint32_t next;
        if (!fat32_next_cluster(ctx, cluster, &next) || next >= FAT32_CLUSTER_END) {
            break;
        }
        cluster = next;
    }
    return false;
}

static bool find_in_directory(
    const struct fat32_context *ctx,
    uint32_t directory_cluster,
    const char *name,
    struct fat32_dir_entry *out_entry
) {
    for (uint64_t index = 0u;; ++index) {
        struct fat32_dir_entry entry;
        if (!read_directory_entry(ctx, directory_cluster, index, &entry)) {
            return false;
        }
        if (string_equal_ci(entry.name, name)) {
            *out_entry = entry;
            return true;
        }
    }
}

static bool resolve_path(
    const struct fat32_context *ctx,
    const char *path,
    struct fat32_dir_entry *out_entry,
    bool *out_is_root
) {
    if (path == NULL || out_entry == NULL || out_is_root == NULL || path[0] != '/') {
        return false;
    }
    if (path[1] == '\0') {
        *out_is_root = true;
        return true;
    }

    *out_is_root = false;
    uint32_t directory_cluster = ctx->root_cluster;
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

        struct fat32_dir_entry entry;
        if (!find_in_directory(ctx, directory_cluster, component, &entry)) {
            return false;
        }
        while (path[position] == '/') {
            ++position;
        }
        if (path[position] == '\0') {
            *out_entry = entry;
            return true;
        }
        if ((entry.attributes & FAT32_ATTR_DIRECTORY) == 0u ||
            !cluster_valid(ctx, entry.first_cluster)) {
            return false;
        }
        directory_cluster = entry.first_cluster;
    }
}

static enum aurora_fs_probe_result fat32_probe(
    const struct aurora_partition *partition
) {
    uint8_t boot[FAT32_MAX_BLOCK_SIZE];
    if (!read_boot_block(partition, boot)) {
        return AURORA_FS_PROBE_NO_MATCH;
    }

    uint32_t bytes_per_sector = le16(boot + 11u);
    uint32_t device_block_size = partition->device->block_size;
    if (boot[510u] != 0x55u || boot[511u] != 0xAAu ||
        !valid_sector_size(bytes_per_sector) ||
        bytes_per_sector < device_block_size ||
        (bytes_per_sector % device_block_size) != 0u ||
        le16(boot + 17u) != 0u || le16(boot + 22u) != 0u ||
        le32(boot + 36u) == 0u) {
        return AURORA_FS_PROBE_NO_MATCH;
    }

    uint8_t sectors_per_cluster = boot[13u];
    if (sectors_per_cluster == 0u ||
        (sectors_per_cluster & (sectors_per_cluster - 1u)) != 0u) {
        return AURORA_FS_PROBE_NO_MATCH;
    }

    return AURORA_FS_PROBE_MATCH_READ_ONLY;
}

static bool fat32_mount(
    const struct aurora_partition *partition,
    void **out_context
) {
    if (out_context == NULL || fat32_probe(partition) == AURORA_FS_PROBE_NO_MATCH) {
        return false;
    }

    uint8_t boot[FAT32_MAX_BLOCK_SIZE];
    if (!read_boot_block(partition, boot)) {
        return false;
    }

    struct fat32_context *ctx = kheap_alloc(sizeof(*ctx), 16u);
    if (ctx == NULL) {
        return false;
    }

    ctx->partition = *partition;
    ctx->bytes_per_sector = le16(boot + 11u);
    ctx->device_block_size = partition->device->block_size;
    ctx->sectors_per_cluster = boot[13u];
    ctx->reserved_sectors = le16(boot + 14u);
    ctx->fat_count = boot[16u];
    ctx->sectors_per_fat = le32(boot + 36u);
    ctx->root_cluster = le32(boot + 44u) & 0x0FFFFFFFu;

    uint64_t total_sectors = le16(boot + 19u);
    if (total_sectors == 0u) {
        total_sectors = le32(boot + 32u);
    }

    uint64_t first_data = (uint64_t)ctx->reserved_sectors
        + (uint64_t)ctx->fat_count * ctx->sectors_per_fat;
    uint64_t partition_bytes = partition->block_count * ctx->device_block_size;

    if (ctx->reserved_sectors == 0u || ctx->fat_count == 0u ||
        ctx->sectors_per_fat == 0u || total_sectors == 0u ||
        total_sectors > UINT64_MAX / ctx->bytes_per_sector ||
        total_sectors * ctx->bytes_per_sector > partition_bytes ||
        first_data >= total_sectors || first_data > UINT32_MAX) {
        return false;
    }

    ctx->first_data_sector = (uint32_t)first_data;
    ctx->total_clusters = (uint32_t)((total_sectors - first_data)
        / ctx->sectors_per_cluster);

    if (ctx->total_clusters < 65525u || !cluster_valid(ctx, ctx->root_cluster)) {
        return false;
    }

    *out_context = ctx;
    return true;
}

static void fat32_unmount(void *context) {
    (void)context;
}

static bool fat32_stat(
    void *context,
    const char *path,
    struct aurora_fs_stat *out_stat
) {
    if (context == NULL || out_stat == NULL) {
        return false;
    }

    struct fat32_context *ctx = (struct fat32_context *)context;
    struct fat32_dir_entry entry;
    bool is_root;
    if (!resolve_path(ctx, path, &entry, &is_root)) {
        return false;
    }

    out_stat->type = is_root || (entry.attributes & FAT32_ATTR_DIRECTORY) != 0u
        ? AURORA_FS_ENTRY_DIRECTORY : AURORA_FS_ENTRY_FILE;
    out_stat->size = is_root ? 0u : entry.size;
    out_stat->allocated_size = 0u;
    out_stat->created_time_ns = 0u;
    out_stat->modified_time_ns = 0u;
    out_stat->accessed_time_ns = 0u;
    out_stat->filesystem_id = is_root ? ctx->root_cluster : entry.first_cluster;
    return true;
}

static bool fat32_readdir(
    void *context,
    const char *path,
    uint64_t index,
    struct aurora_fs_dirent *out_entry
) {
    if (context == NULL || path == NULL || out_entry == NULL) {
        return false;
    }

    struct fat32_context *ctx = (struct fat32_context *)context;
    uint32_t directory_cluster = ctx->root_cluster;

    if (!(path[0] == '/' && path[1] == '\0')) {
        struct fat32_dir_entry directory;
        bool is_root;
        if (!resolve_path(ctx, path, &directory, &is_root) || is_root ||
            (directory.attributes & FAT32_ATTR_DIRECTORY) == 0u ||
            !cluster_valid(ctx, directory.first_cluster)) {
            return false;
        }
        directory_cluster = directory.first_cluster;
    }

    struct fat32_dir_entry entry;
    if (!read_directory_entry(ctx, directory_cluster, index, &entry)) {
        return false;
    }

    size_t i = 0u;
    for (; i + 1u < AURORA_FS_NAME_MAX && entry.name[i] != '\0'; ++i) {
        out_entry->name[i] = entry.name[i];
    }
    out_entry->name[i] = '\0';
    out_entry->type = (entry.attributes & FAT32_ATTR_DIRECTORY) != 0u
        ? AURORA_FS_ENTRY_DIRECTORY : AURORA_FS_ENTRY_FILE;
    out_entry->size = entry.size;
    out_entry->filesystem_id = entry.first_cluster;
    return true;
}

static bool fat32_read(
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
    struct fat32_context *ctx = (struct fat32_context *)context;
    struct fat32_dir_entry entry;
    bool is_root;
    if (!resolve_path(ctx, path, &entry, &is_root) || is_root ||
        (entry.attributes & FAT32_ATTR_DIRECTORY) != 0u || offset > entry.size) {
        return false;
    }
    if (length == 0u || offset == entry.size) {
        return true;
    }

    uint64_t remaining_file = (uint64_t)entry.size - offset;
    size_t wanted = length;
    if ((uint64_t)wanted > remaining_file) {
        wanted = (size_t)remaining_file;
    }

    uint64_t cluster_bytes = cluster_size_bytes(ctx);
    uint64_t skip_clusters = offset / cluster_bytes;
    uint64_t offset_in_cluster = offset % cluster_bytes;
    uint32_t cluster = entry.first_cluster;
    if (!cluster_valid(ctx, cluster)) {
        return false;
    }

    for (uint64_t i = 0u; i < skip_clusters; ++i) {
        uint32_t next;
        if (!fat32_next_cluster(ctx, cluster, &next) ||
            next >= FAT32_CLUSTER_END || !cluster_valid(ctx, next)) {
            return false;
        }
        cluster = next;
    }

    uint8_t *out = (uint8_t *)buffer;
    size_t copied = 0u;
    uint32_t guard = 0u;

    while (copied < wanted && guard++ <= ctx->total_clusters) {
        if (!cluster_valid(ctx, cluster)) {
            return false;
        }

        size_t remaining = wanted - copied;
        uint64_t available64 = cluster_bytes - offset_in_cluster;
        size_t available = available64 > SIZE_MAX ? SIZE_MAX : (size_t)available64;
        size_t take = available < remaining ? available : remaining;
        if (!read_partition_bytes(
                ctx,
                cluster_byte_offset(ctx, cluster) + offset_in_cluster,
                out + copied,
                take)) {
            return false;
        }

        copied += take;
        offset_in_cluster = 0u;
        if (copied >= wanted) {
            break;
        }

        uint32_t next;
        if (!fat32_next_cluster(ctx, cluster, &next) || next >= FAT32_CLUSTER_END) {
            return false;
        }
        cluster = next;
    }

    *out_read = copied;
    return copied == wanted;
}

static const struct aurora_fs_driver fat32 = {
    .name = "FAT32",
    .probe = fat32_probe,
    .mount = fat32_mount,
    .unmount = fat32_unmount,
    .stat = fat32_stat,
    .readdir = fat32_readdir,
    .read = fat32_read,
    .write = NULL
};

const struct aurora_fs_driver *fat32_driver(void) {
    return &fat32;
}
