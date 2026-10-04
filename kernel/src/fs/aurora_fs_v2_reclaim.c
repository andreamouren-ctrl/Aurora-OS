#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2_reclaim.h>
#include <aurora/block_device.h>

#define V2R_MAGIC_0 'A'
#define V2R_MAGIC_1 'U'
#define V2R_MAGIC_2 'R'
#define V2R_MAGIC_3 'E'
#define V2R_MAGIC_4 'X'
#define V2R_MAGIC_5 'T'
#define V2R_MAGIC_6 '2'
#define V2R_MAGIC_7 '\0'
#define V2R_VERSION 1u
#define V2R_HEADER_SIZE 64u
#define V2R_ENTRY_SIZE 32u
#define V2R_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - V2R_HEADER_SIZE) / V2R_ENTRY_SIZE)
#define V2R_MAX_LEVEL 3u
#define V2R_MAX_NEW_PATH 4u

struct v2r_header {
    uint8_t magic[8];
    uint32_t version;
    uint16_t level;
    uint16_t entry_count;
    uint64_t generation;
    uint64_t first_logical;
    uint64_t last_logical_exclusive;
    uint32_t checksum;
    uint8_t reserved[20];
} __attribute__((packed));

struct v2r_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct v2r_node {
    struct v2r_header header;
    struct v2r_entry entries[V2R_CAPACITY];
} __attribute__((packed));

struct v2r_build_context {
    struct aurora_fs_v2_allocator *allocator;
    uint64_t new_metadata[V2R_MAX_NEW_PATH];
    uint32_t new_metadata_count;
};

static struct v2r_node v2r_node_io;
static struct v2r_node v2r_child_io;

_Static_assert(sizeof(struct v2r_header) == V2R_HEADER_SIZE,
               "AuroraFS v2 reclaim node header drifted");
_Static_assert(sizeof(struct v2r_entry) == V2R_ENTRY_SIZE,
               "AuroraFS v2 reclaim node entry drifted");
_Static_assert(sizeof(struct v2r_node) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 reclaim node must fill one fs block");
_Static_assert(V2R_CAPACITY == 126u,
               "AuroraFS v2 reclaim assumes 126 entries per node");

static bool add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || b > UINT64_MAX - a) return false;
    *out = a + b;
    return true;
}

static uint32_t crc32_ieee(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint32_t node_checksum(struct v2r_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return checksum;
}

static bool magic_valid(const struct v2r_node *node) {
    static const uint8_t magic[8] = {
        V2R_MAGIC_0, V2R_MAGIC_1, V2R_MAGIC_2, V2R_MAGIC_3,
        V2R_MAGIC_4, V2R_MAGIC_5, V2R_MAGIC_6, V2R_MAGIC_7
    };
    if (node == NULL) return false;
    for (size_t i = 0u; i < sizeof(magic); ++i)
        if (node->header.magic[i] != magic[i]) return false;
    return true;
}

static bool fs_block_geometry(
    const struct aurora_fs_v2_allocator *allocator,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_count
) {
    if (allocator == NULL || allocator->device == NULL || out_lba == NULL ||
        out_count == NULL || fs_block >= allocator->total_fs_blocks ||
        allocator->device->block_size == 0u ||
        allocator->device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        (AURORA_FS_V2_FS_BLOCK_SIZE % allocator->device->block_size) != 0u ||
        (allocator->base_bytes % allocator->device->block_size) != 0u) return false;

    uint64_t fs_offset;
    uint64_t byte_offset;
    if (fs_block > UINT64_MAX / AURORA_FS_V2_FS_BLOCK_SIZE) return false;
    fs_offset = fs_block * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (!add_u64(allocator->base_bytes, fs_offset, &byte_offset)) return false;

    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / allocator->device->block_size;
    uint64_t lba = byte_offset / allocator->device->block_size;
    if (count == 0u || count > UINT32_MAX || lba >= allocator->device->block_count ||
        count > allocator->device->block_count - lba) return false;
    *out_lba = lba;
    *out_count = (uint32_t)count;
    return true;
}

static bool read_node(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t block,
    struct v2r_node *out
) {
    uint64_t lba;
    uint32_t count;
    if (out == NULL || block < allocator->data_start ||
        !fs_block_geometry(allocator, block, &lba, &count) ||
        !block_device_read(allocator->device, lba, count, out) ||
        !magic_valid(out) || out->header.version != V2R_VERSION ||
        out->header.level > V2R_MAX_LEVEL || out->header.entry_count == 0u ||
        out->header.entry_count > V2R_CAPACITY ||
        out->header.last_logical_exclusive <= out->header.first_logical)
        return false;
    return out->header.checksum == node_checksum(out);
}

static bool write_new_node(
    struct v2r_build_context *context,
    struct v2r_node *node,
    uint64_t *out_block
) {
    if (context == NULL || context->allocator == NULL || node == NULL || out_block == NULL ||
        context->new_metadata_count >= V2R_MAX_NEW_PATH) return false;

    uint64_t block;
    if (!aurora_fs_v2_allocator_allocate_range(context->allocator, 1u, &block)) return false;
    uint64_t lba;
    uint32_t count;
    node->header.checksum = node_checksum(node);
    if (!fs_block_geometry(context->allocator, block, &lba, &count) ||
        !block_device_write(context->allocator->device, lba, count, node) ||
        !block_device_flush(context->allocator->device)) {
        (void)aurora_fs_v2_allocator_free_range(context->allocator, block, 1u);
        return false;
    }
    context->new_metadata[context->new_metadata_count++] = block;
    *out_block = block;
    return true;
}

static void rollback_new_metadata(struct v2r_build_context *context) {
    if (context == NULL || context->allocator == NULL) return;
    while (context->new_metadata_count != 0u) {
        uint64_t block = context->new_metadata[--context->new_metadata_count];
        (void)aurora_fs_v2_allocator_free_range(context->allocator, block, 1u);
    }
}

static bool count_subtree_data(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t block,
    uint64_t *out_blocks
) {
    struct v2r_node node;
    if (out_blocks == NULL || !read_node(allocator, block, &node)) return false;
    uint64_t total = 0u;
    if (node.header.level == 0u) {
        for (uint16_t i = 0u; i < node.header.entry_count; ++i) {
            if (!add_u64(total, node.entries[i].block_count_or_span, &total)) return false;
        }
    } else {
        for (uint16_t i = 0u; i < node.header.entry_count; ++i) {
            uint64_t child = 0u;
            if (!count_subtree_data(allocator, node.entries[i].physical_or_child, &child) ||
                !add_u64(total, child, &total)) return false;
        }
    }
    *out_blocks = total;
    return true;
}

static bool trim_node(
    struct v2r_build_context *context,
    uint64_t old_block,
    uint64_t keep,
    uint64_t *out_new_block,
    uint64_t *out_removed,
    bool *out_changed
) {
    struct v2r_node node;
    if (context == NULL || out_new_block == NULL || out_removed == NULL ||
        out_changed == NULL || !read_node(context->allocator, old_block, &node)) return false;

    if (keep >= node.header.last_logical_exclusive) {
        *out_new_block = old_block;
        *out_removed = 0u;
        *out_changed = false;
        return true;
    }
    if (keep <= node.header.first_logical) {
        if (!count_subtree_data(context->allocator, old_block, out_removed)) return false;
        *out_new_block = 0u;
        *out_changed = true;
        return true;
    }

    struct v2r_node replacement = node;
    uint16_t kept = 0u;
    uint64_t removed = 0u;

    for (uint16_t i = 0u; i < node.header.entry_count; ++i) {
        const struct v2r_entry *entry = &node.entries[i];
        uint64_t end;
        if (entry->block_count_or_span == 0u ||
            !add_u64(entry->logical_block, entry->block_count_or_span, &end)) return false;

        if (end <= keep) {
            replacement.entries[kept++] = *entry;
            continue;
        }

        if (entry->logical_block >= keep) {
            uint64_t add = 0u;
            if (node.header.level == 0u) add = entry->block_count_or_span;
            else if (!count_subtree_data(context->allocator, entry->physical_or_child, &add))
                return false;
            if (!add_u64(removed, add, &removed)) return false;
            continue;
        }

        if (node.header.level == 0u) {
            uint64_t retained = keep - entry->logical_block;
            uint64_t dropped = entry->block_count_or_span - retained;
            struct v2r_entry partial = *entry;
            partial.block_count_or_span = retained;
            replacement.entries[kept++] = partial;
            if (!add_u64(removed, dropped, &removed)) return false;
        } else {
            uint64_t new_child = 0u;
            uint64_t child_removed = 0u;
            bool child_changed = false;
            if (!trim_node(context, entry->physical_or_child, keep,
                           &new_child, &child_removed, &child_changed)) return false;
            if (!add_u64(removed, child_removed, &removed)) return false;
            if (new_child != 0u) {
                struct v2r_entry partial = *entry;
                partial.physical_or_child = new_child;
                partial.block_count_or_span = keep - entry->logical_block;
                replacement.entries[kept++] = partial;
            }
        }
    }

    if (kept == 0u) {
        *out_new_block = 0u;
        *out_removed = removed;
        *out_changed = true;
        return true;
    }

    for (uint16_t i = kept; i < V2R_CAPACITY; ++i)
        replacement.entries[i] = (struct v2r_entry){0};
    replacement.header.entry_count = kept;
    replacement.header.last_logical_exclusive = keep;
    replacement.header.generation++;
    replacement.header.checksum = 0u;

    uint64_t new_block;
    if (!write_new_node(context, &replacement, &new_block)) return false;
    *out_new_block = new_block;
    *out_removed = removed;
    *out_changed = true;
    return true;
}

bool aurora_fs_v2_extent_tree_trim_tail_cow(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root,
    uint64_t keep_logical_blocks,
    struct aurora_fs_v2_trim_result *out_result
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        old_root < allocator->data_start || out_result == NULL) return false;

    struct v2r_build_context context = {
        .allocator = allocator,
        .new_metadata_count = 0u
    };
    uint64_t new_root = 0u;
    uint64_t removed = 0u;
    bool changed = false;
    if (!trim_node(&context, old_root, keep_logical_blocks,
                   &new_root, &removed, &changed)) {
        rollback_new_metadata(&context);
        return false;
    }

    out_result->new_root = new_root;
    out_result->removed_data_blocks = removed;
    out_result->changed = changed;
    return true;
}

static bool free_whole_subtree(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t block
) {
    struct v2r_node node;
    if (!read_node(allocator, block, &node)) return false;
    if (node.header.level == 0u) {
        for (uint16_t i = 0u; i < node.header.entry_count; ++i) {
            if (!aurora_fs_v2_allocator_free_range(
                    allocator, node.entries[i].physical_or_child,
                    node.entries[i].block_count_or_span)) return false;
        }
    } else {
        for (uint16_t i = 0u; i < node.header.entry_count; ++i)
            if (!free_whole_subtree(allocator, node.entries[i].physical_or_child)) return false;
    }
    return aurora_fs_v2_allocator_free_range(allocator, block, 1u);
}

static bool reclaim_old_tail_node(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t block,
    uint64_t keep
) {
    struct v2r_node node;
    if (!read_node(allocator, block, &node)) return false;
    if (keep >= node.header.last_logical_exclusive) return true;
    if (keep <= node.header.first_logical) return free_whole_subtree(allocator, block);

    if (node.header.level == 0u) {
        for (uint16_t i = 0u; i < node.header.entry_count; ++i) {
            const struct v2r_entry *entry = &node.entries[i];
            uint64_t end;
            if (!add_u64(entry->logical_block, entry->block_count_or_span, &end)) return false;
            if (end <= keep) continue;
            if (entry->logical_block >= keep) {
                if (!aurora_fs_v2_allocator_free_range(
                        allocator, entry->physical_or_child,
                        entry->block_count_or_span)) return false;
            } else {
                uint64_t retained = keep - entry->logical_block;
                uint64_t dropped = entry->block_count_or_span - retained;
                if (dropped != 0u && !aurora_fs_v2_allocator_free_range(
                        allocator, entry->physical_or_child + retained, dropped)) return false;
            }
        }
    } else {
        for (uint16_t i = 0u; i < node.header.entry_count; ++i) {
            const struct v2r_entry *entry = &node.entries[i];
            uint64_t end;
            if (!add_u64(entry->logical_block, entry->block_count_or_span, &end)) return false;
            if (end <= keep) continue;
            if (entry->logical_block >= keep) {
                if (!free_whole_subtree(allocator, entry->physical_or_child)) return false;
            } else if (!reclaim_old_tail_node(allocator, entry->physical_or_child, keep)) {
                return false;
            }
        }
    }

    return aurora_fs_v2_allocator_free_range(allocator, block, 1u);
}

bool aurora_fs_v2_extent_tree_reclaim_old_tail(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root,
    uint64_t keep_logical_blocks
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        old_root < allocator->data_start) return false;
    return reclaim_old_tail_node(allocator, old_root, keep_logical_blocks);
}
