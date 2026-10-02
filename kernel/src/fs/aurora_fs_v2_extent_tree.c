#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define V2_EXTENT_NODE_MAGIC_0 'A'
#define V2_EXTENT_NODE_MAGIC_1 'U'
#define V2_EXTENT_NODE_MAGIC_2 'R'
#define V2_EXTENT_NODE_MAGIC_3 'E'
#define V2_EXTENT_NODE_MAGIC_4 'X'
#define V2_EXTENT_NODE_MAGIC_5 'T'
#define V2_EXTENT_NODE_MAGIC_6 '2'
#define V2_EXTENT_NODE_MAGIC_7 '\0'
#define V2_EXTENT_NODE_VERSION 1u
#define V2_EXTENT_NODE_HEADER_SIZE 64u
#define V2_EXTENT_NODE_ENTRY_SIZE 32u
#define V2_EXTENT_NODE_CAPACITY \
    ((AURORA_FS_V2_FS_BLOCK_SIZE - V2_EXTENT_NODE_HEADER_SIZE) / V2_EXTENT_NODE_ENTRY_SIZE)
#define V2_EXTENT_LEVEL_LEAF 0u
#define V2_EXTENT_LEVEL_ROOT 1u
#define V2_EXTENT_TEST_DEVICE_BYTES (2u * 1024u * 1024u)
#define V2_EXTENT_TEST_COUNT (V2_EXTENT_NODE_CAPACITY + 4u)

struct extent_node_header_disk {
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

struct extent_node_entry_disk {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct extent_node_disk {
    struct extent_node_header_disk header;
    struct extent_node_entry_disk entries[V2_EXTENT_NODE_CAPACITY];
} __attribute__((packed));

struct extent_test_context {
    uint8_t *storage;
    uint64_t storage_bytes;
};

static uint8_t extent_node_scratch[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t extent_child_scratch[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t extent_test_storage[V2_EXTENT_TEST_DEVICE_BYTES];
static struct aurora_fs_v2_extent extent_test_extents[V2_EXTENT_TEST_COUNT];

_Static_assert(sizeof(struct extent_node_header_disk) == V2_EXTENT_NODE_HEADER_SIZE,
               "AuroraFS v2 extent-node header must be 64 bytes");
_Static_assert(sizeof(struct extent_node_entry_disk) == V2_EXTENT_NODE_ENTRY_SIZE,
               "AuroraFS v2 extent-node entry must be 32 bytes");
_Static_assert(sizeof(struct extent_node_disk) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 extent node must fill one filesystem block");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) {
        bytes[i] = 0u;
    }
}

static bool add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || b > UINT64_MAX - a) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || (a != 0u && b > UINT64_MAX / a)) {
        return false;
    }
    *out = a * b;
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

static uint32_t node_checksum(struct extent_node_disk *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return checksum;
}

static void set_magic(struct extent_node_disk *node) {
    static const uint8_t magic[8] = {
        V2_EXTENT_NODE_MAGIC_0, V2_EXTENT_NODE_MAGIC_1,
        V2_EXTENT_NODE_MAGIC_2, V2_EXTENT_NODE_MAGIC_3,
        V2_EXTENT_NODE_MAGIC_4, V2_EXTENT_NODE_MAGIC_5,
        V2_EXTENT_NODE_MAGIC_6, V2_EXTENT_NODE_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        node->header.magic[i] = magic[i];
    }
}

static bool magic_valid(const struct extent_node_disk *node) {
    static const uint8_t magic[8] = {
        V2_EXTENT_NODE_MAGIC_0, V2_EXTENT_NODE_MAGIC_1,
        V2_EXTENT_NODE_MAGIC_2, V2_EXTENT_NODE_MAGIC_3,
        V2_EXTENT_NODE_MAGIC_4, V2_EXTENT_NODE_MAGIC_5,
        V2_EXTENT_NODE_MAGIC_6, V2_EXTENT_NODE_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        if (node->header.magic[i] != magic[i]) {
            return false;
        }
    }
    return true;
}

static bool fs_block_geometry(
    const struct aurora_fs_v2_allocator *allocator,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_device_blocks
) {
    if (allocator == NULL || allocator->device == NULL || out_lba == NULL ||
        out_device_blocks == NULL || fs_block >= allocator->total_fs_blocks ||
        allocator->device->block_size == 0u ||
        allocator->device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        (AURORA_FS_V2_FS_BLOCK_SIZE % allocator->device->block_size) != 0u ||
        (allocator->base_bytes % allocator->device->block_size) != 0u) {
        return false;
    }

    uint64_t fs_offset;
    uint64_t byte_offset;
    if (!mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_offset) ||
        !add_u64(allocator->base_bytes, fs_offset, &byte_offset)) {
        return false;
    }

    uint64_t device_blocks =
        AURORA_FS_V2_FS_BLOCK_SIZE / allocator->device->block_size;
    uint64_t lba = byte_offset / allocator->device->block_size;
    if (device_blocks == 0u || device_blocks > UINT32_MAX ||
        lba >= allocator->device->block_count ||
        device_blocks > allocator->device->block_count - lba) {
        return false;
    }

    *out_lba = lba;
    *out_device_blocks = (uint32_t)device_blocks;
    return true;
}

static bool read_node(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t fs_block,
    struct extent_node_disk *node
) {
    uint64_t lba;
    uint32_t device_blocks;
    if (node == NULL ||
        !fs_block_geometry(allocator, fs_block, &lba, &device_blocks) ||
        !block_device_read(allocator->device, lba, device_blocks, node) ||
        !magic_valid(node) || node->header.version != V2_EXTENT_NODE_VERSION ||
        node->header.entry_count == 0u ||
        node->header.entry_count > V2_EXTENT_NODE_CAPACITY ||
        node->header.last_logical_exclusive <= node->header.first_logical) {
        return false;
    }
    return node->header.checksum == node_checksum(node);
}

static bool write_node(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t fs_block,
    struct extent_node_disk *node
) {
    uint64_t lba;
    uint32_t device_blocks;
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        node == NULL || !fs_block_geometry(allocator, fs_block, &lba, &device_blocks)) {
        return false;
    }
    node->header.checksum = node_checksum(node);
    return block_device_write(allocator->device, lba, device_blocks, node);
}

static bool extent_array_valid(
    const struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_extent *extents,
    uint32_t extent_count
) {
    if (allocator == NULL || extents == NULL || extent_count == 0u ||
        extent_count > V2_EXTENT_NODE_CAPACITY * V2_EXTENT_NODE_CAPACITY) {
        return false;
    }

    uint64_t previous_end = 0u;
    for (uint32_t i = 0u; i < extent_count; ++i) {
        uint64_t logical_end;
        uint64_t physical_end;
        if (extents[i].block_count == 0u ||
            extents[i].physical_block < allocator->data_start ||
            !add_u64(extents[i].logical_block, extents[i].block_count, &logical_end) ||
            !add_u64(extents[i].physical_block, extents[i].block_count, &physical_end) ||
            physical_end > allocator->total_fs_blocks ||
            (i != 0u && extents[i].logical_block < previous_end)) {
            return false;
        }
        previous_end = logical_end;
    }
    return true;
}

static void init_node(
    struct extent_node_disk *node,
    uint16_t level,
    uint16_t entry_count,
    uint64_t first_logical,
    uint64_t last_logical_exclusive
) {
    zero_bytes(node, sizeof(*node));
    set_magic(node);
    node->header.version = V2_EXTENT_NODE_VERSION;
    node->header.level = level;
    node->header.entry_count = entry_count;
    node->header.generation = 1u;
    node->header.first_logical = first_logical;
    node->header.last_logical_exclusive = last_logical_exclusive;
}

static bool write_leaf(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_extent *extents,
    uint32_t extent_count,
    uint64_t leaf_block
) {
    struct extent_node_disk *leaf = (struct extent_node_disk *)extent_child_scratch;
    uint64_t last_exclusive;
    if (extent_count == 0u || extent_count > V2_EXTENT_NODE_CAPACITY ||
        !add_u64(
            extents[extent_count - 1u].logical_block,
            extents[extent_count - 1u].block_count,
            &last_exclusive)) {
        return false;
    }

    init_node(
        leaf,
        V2_EXTENT_LEVEL_LEAF,
        (uint16_t)extent_count,
        extents[0].logical_block,
        last_exclusive);
    for (uint32_t i = 0u; i < extent_count; ++i) {
        leaf->entries[i].logical_block = extents[i].logical_block;
        leaf->entries[i].physical_or_child = extents[i].physical_block;
        leaf->entries[i].block_count_or_span = extents[i].block_count;
    }
    return write_node(allocator, leaf_block, leaf);
}

bool aurora_fs_v2_extent_tree_write(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_extent *extents,
    uint32_t extent_count,
    uint64_t *out_root_block
) {
    if (out_root_block == NULL || !extent_array_valid(allocator, extents, extent_count) ||
        allocator->device->read_only) {
        return false;
    }

    if (extent_count <= V2_EXTENT_NODE_CAPACITY) {
        uint64_t root_block;
        if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &root_block) ||
            !write_leaf(allocator, extents, extent_count, root_block) ||
            !block_device_flush(allocator->device)) {
            return false;
        }
        *out_root_block = root_block;
        return true;
    }

    uint32_t leaf_count =
        (extent_count + V2_EXTENT_NODE_CAPACITY - 1u) / V2_EXTENT_NODE_CAPACITY;
    if (leaf_count > V2_EXTENT_NODE_CAPACITY) {
        return false;
    }

    struct extent_node_disk *root = (struct extent_node_disk *)extent_node_scratch;
    uint64_t tree_last;
    if (!add_u64(
            extents[extent_count - 1u].logical_block,
            extents[extent_count - 1u].block_count,
            &tree_last)) {
        return false;
    }
    init_node(
        root,
        V2_EXTENT_LEVEL_ROOT,
        (uint16_t)leaf_count,
        extents[0].logical_block,
        tree_last);

    uint32_t offset = 0u;
    for (uint32_t leaf_index = 0u; leaf_index < leaf_count; ++leaf_index) {
        uint32_t remaining = extent_count - offset;
        uint32_t take = remaining > V2_EXTENT_NODE_CAPACITY ?
            V2_EXTENT_NODE_CAPACITY : remaining;
        uint64_t leaf_block;
        uint64_t leaf_last;
        if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &leaf_block) ||
            !write_leaf(allocator, &extents[offset], take, leaf_block) ||
            !add_u64(
                extents[offset + take - 1u].logical_block,
                extents[offset + take - 1u].block_count,
                &leaf_last)) {
            return false;
        }
        root->entries[leaf_index].logical_block = extents[offset].logical_block;
        root->entries[leaf_index].physical_or_child = leaf_block;
        root->entries[leaf_index].block_count_or_span =
            leaf_last - extents[offset].logical_block;
        offset += take;
    }

    uint64_t root_block;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &root_block) ||
        !write_node(allocator, root_block, root) ||
        !block_device_flush(allocator->device)) {
        return false;
    }

    *out_root_block = root_block;
    return true;
}

static bool lookup_leaf(
    const struct extent_node_disk *leaf,
    uint64_t logical_block,
    uint64_t *out_physical_block,
    uint64_t *out_contiguous_blocks
) {
    if (leaf == NULL || leaf->header.level != V2_EXTENT_LEVEL_LEAF ||
        out_physical_block == NULL || out_contiguous_blocks == NULL) {
        return false;
    }

    for (uint16_t i = 0u; i < leaf->header.entry_count; ++i) {
        const struct extent_node_entry_disk *entry = &leaf->entries[i];
        uint64_t end;
        if (entry->block_count_or_span == 0u ||
            !add_u64(entry->logical_block, entry->block_count_or_span, &end)) {
            return false;
        }
        if (logical_block >= entry->logical_block && logical_block < end) {
            uint64_t within = logical_block - entry->logical_block;
            *out_physical_block = entry->physical_or_child + within;
            *out_contiguous_blocks = entry->block_count_or_span - within;
            return true;
        }
    }
    return false;
}

bool aurora_fs_v2_extent_tree_lookup(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t root_block,
    uint64_t logical_block,
    uint64_t *out_physical_block,
    uint64_t *out_contiguous_blocks
) {
    if (allocator == NULL || out_physical_block == NULL ||
        out_contiguous_blocks == NULL || root_block < allocator->data_start ||
        root_block >= allocator->total_fs_blocks) {
        return false;
    }

    struct extent_node_disk *root = (struct extent_node_disk *)extent_node_scratch;
    if (!read_node(allocator, root_block, root) ||
        logical_block < root->header.first_logical ||
        logical_block >= root->header.last_logical_exclusive) {
        return false;
    }

    if (root->header.level == V2_EXTENT_LEVEL_LEAF) {
        return lookup_leaf(root, logical_block, out_physical_block, out_contiguous_blocks);
    }
    if (root->header.level != V2_EXTENT_LEVEL_ROOT) {
        return false;
    }

    for (uint16_t i = 0u; i < root->header.entry_count; ++i) {
        const struct extent_node_entry_disk *entry = &root->entries[i];
        uint64_t end;
        if (entry->block_count_or_span == 0u ||
            !add_u64(entry->logical_block, entry->block_count_or_span, &end)) {
            return false;
        }
        if (logical_block < entry->logical_block || logical_block >= end) {
            continue;
        }

        struct extent_node_disk *leaf = (struct extent_node_disk *)extent_child_scratch;
        if (!read_node(allocator, entry->physical_or_child, leaf) ||
            leaf->header.level != V2_EXTENT_LEVEL_LEAF) {
            return false;
        }
        return lookup_leaf(leaf, logical_block, out_physical_block, out_contiguous_blocks);
    }
    return false;
}

static bool test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }
    struct extent_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset) {
        return false;
    }
    uint8_t *out = buffer;
    for (uint64_t i = 0u; i < length; ++i) {
        out[i] = context->storage[offset + i];
    }
    return true;
}

static bool test_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    if (device == NULL || buffer == NULL || device->read_only || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }
    struct extent_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset) {
        return false;
    }
    const uint8_t *source = buffer;
    for (uint64_t i = 0u; i < length; ++i) {
        context->storage[offset + i] = source[i];
    }
    return true;
}

static bool test_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool run_extent_geometry(uint32_t device_block_size) {
    zero_bytes(extent_test_storage, sizeof(extent_test_storage));
    struct extent_test_context context = {
        .storage = extent_test_storage,
        .storage_bytes = sizeof(extent_test_storage)
    };
    struct aurora_block_device device = {
        .name = "aurorafs-v2-extent-tree-test",
        .block_size = device_block_size,
        .block_count = sizeof(extent_test_storage) / device_block_size,
        .read_only = false,
        .context = &context,
        .read_blocks = test_read,
        .write_blocks = test_write,
        .flush = test_flush
    };

    struct aurora_fs_v2_format_geometry geometry;
    if (!aurora_fs_v2_format_device(
            &device,
            AURORA_FS_V2_DEFAULT_BASE_BYTES,
            0u,
            &geometry)) {
        return false;
    }

    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator,
            &device,
            geometry.base_bytes,
            geometry.total_fs_blocks,
            geometry.bitmap_start,
            geometry.bitmap_blocks,
            geometry.data_start)) {
        return false;
    }

    for (uint32_t i = 0u; i < V2_EXTENT_TEST_COUNT; ++i) {
        uint64_t data_block;
        uint64_t separator_block;
        if (!aurora_fs_v2_allocator_allocate_range(&allocator, 1u, &data_block) ||
            !aurora_fs_v2_allocator_allocate_range(&allocator, 1u, &separator_block)) {
            return false;
        }
        extent_test_extents[i].logical_block = i;
        extent_test_extents[i].physical_block = data_block;
        extent_test_extents[i].block_count = 1u;
        (void)separator_block;
    }

    uint64_t root_block;
    if (!aurora_fs_v2_extent_tree_write(
            &allocator,
            extent_test_extents,
            V2_EXTENT_TEST_COUNT,
            &root_block)) {
        return false;
    }

    bool root_allocated = false;
    if (!aurora_fs_v2_allocator_is_allocated(&allocator, root_block, &root_allocated) ||
        !root_allocated) {
        return false;
    }

    for (uint32_t i = 0u; i < V2_EXTENT_TEST_COUNT; ++i) {
        uint64_t physical;
        uint64_t contiguous;
        if (!aurora_fs_v2_extent_tree_lookup(
                &allocator, root_block, i, &physical, &contiguous) ||
            physical != extent_test_extents[i].physical_block || contiguous != 1u) {
            return false;
        }
    }

    uint64_t physical;
    uint64_t contiguous;
    if (aurora_fs_v2_extent_tree_lookup(
            &allocator,
            root_block,
            V2_EXTENT_TEST_COUNT,
            &physical,
            &contiguous)) {
        return false;
    }

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened,
            &device,
            geometry.base_bytes,
            geometry.total_fs_blocks,
            geometry.bitmap_start,
            geometry.bitmap_blocks,
            geometry.data_start)) {
        return false;
    }

    static const uint32_t probes[] = { 0u, 1u, 125u, 126u, V2_EXTENT_TEST_COUNT - 1u };
    for (size_t i = 0u; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        uint32_t logical = probes[i];
        if (!aurora_fs_v2_extent_tree_lookup(
                &reopened, root_block, logical, &physical, &contiguous) ||
            physical != extent_test_extents[logical].physical_block || contiguous != 1u) {
            return false;
        }
    }
    return true;
}

bool aurora_fs_v2_extent_tree_self_test(void) {
    return run_extent_geometry(512u) && run_extent_geometry(4096u);
}
