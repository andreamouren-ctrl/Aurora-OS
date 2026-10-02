#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define U_MAGIC_0 'A'
#define U_MAGIC_1 'U'
#define U_MAGIC_2 'R'
#define U_MAGIC_3 'E'
#define U_MAGIC_4 'X'
#define U_MAGIC_5 'T'
#define U_MAGIC_6 '2'
#define U_MAGIC_7 '\0'
#define U_VERSION 1u
#define U_HEADER_SIZE 64u
#define U_ENTRY_SIZE 32u
#define U_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - U_HEADER_SIZE) / U_ENTRY_SIZE)
#define U_LEVEL_LEAF 0u
#define U_LEVEL_ONE 1u
#define U_LEVEL_TWO 2u
#define U_LEVEL_THREE 3u
#define U_MAX_SUPPORTED_LEVEL U_LEVEL_THREE
#define U_INODE_SIZE 256u
#define U_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / U_INODE_SIZE)
#define U_INODE_FILE 1u
#define U_TOTAL_FS_BLOCKS 64u
#define U_INODE_BLOCK 2u
#define U_LEAF_BLOCK 10u
#define U_LEVEL1_BLOCK 11u
#define U_LEVEL2_BLOCK 12u
#define U_LEVEL3_BLOCK 13u
#define U_DATA_BLOCK 20u
#define U_LOGICAL_BLOCK 7u
#define U_INODE_INDEX 0u
#define U_STORAGE_BYTES ((U_TOTAL_FS_BLOCKS + 1u) * AURORA_FS_V2_FS_BLOCK_SIZE)

struct u_header {
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

struct u_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct u_node {
    struct u_header header;
    struct u_entry entries[U_CAPACITY];
} __attribute__((packed));

struct u_inode_extent {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct u_inode {
    uint64_t object_id;
    uint64_t parent_object_id;
    uint64_t size;
    uint64_t allocated_bytes;
    uint64_t generation;
    uint64_t extent_tree_root;
    uint32_t type;
    uint32_t flags;
    uint32_t extent_count;
    uint32_t reserved0;
    struct u_inode_extent extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

static uint8_t u_storage[U_STORAGE_BYTES];
static struct u_node u_node_io;
static uint8_t u_inode_io[AURORA_FS_V2_FS_BLOCK_SIZE];

_Static_assert(sizeof(struct u_header) == U_HEADER_SIZE,
               "AuroraFS v2 unified lookup header must remain 64 bytes");
_Static_assert(sizeof(struct u_entry) == U_ENTRY_SIZE,
               "AuroraFS v2 unified lookup entry must remain 32 bytes");
_Static_assert(sizeof(struct u_node) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 unified lookup node must fill one filesystem block");
_Static_assert(sizeof(struct u_inode) == U_INODE_SIZE,
               "AuroraFS v2 unified lookup inode must remain 256 bytes");
_Static_assert(U_CAPACITY == 126u,
               "AuroraFS v2 unified lookup assumes 126 entries per node");

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

static uint32_t node_checksum(struct u_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return checksum;
}

static bool magic_valid(const struct u_node *node) {
    static const uint8_t magic[8] = {
        U_MAGIC_0, U_MAGIC_1, U_MAGIC_2, U_MAGIC_3,
        U_MAGIC_4, U_MAGIC_5, U_MAGIC_6, U_MAGIC_7
    };
    if (node == NULL) {
        return false;
    }
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        if (node->header.magic[i] != magic[i]) {
            return false;
        }
    }
    return true;
}

static void init_node(struct u_node *node, uint16_t level,
                      uint64_t first, uint64_t last) {
    zero_bytes(node, sizeof(*node));
    static const uint8_t magic[8] = {
        U_MAGIC_0, U_MAGIC_1, U_MAGIC_2, U_MAGIC_3,
        U_MAGIC_4, U_MAGIC_5, U_MAGIC_6, U_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        node->header.magic[i] = magic[i];
    }
    node->header.version = U_VERSION;
    node->header.level = level;
    node->header.entry_count = 1u;
    node->header.generation = 1u;
    node->header.first_logical = first;
    node->header.last_logical_exclusive = last;
}

static bool fs_block_geometry(struct aurora_block_device *device,
                              uint64_t base_bytes,
                              uint64_t fs_block,
                              uint64_t total_fs_blocks,
                              uint64_t *out_lba,
                              uint32_t *out_count) {
    if (device == NULL || out_lba == NULL || out_count == NULL ||
        device->block_size == 0u || device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) != 0u ||
        (base_bytes % device->block_size) != 0u || fs_block >= total_fs_blocks) {
        return false;
    }
    uint64_t fs_offset;
    uint64_t byte_offset;
    if (!mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_offset) ||
        !add_u64(base_bytes, fs_offset, &byte_offset)) {
        return false;
    }
    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    uint64_t lba = byte_offset / device->block_size;
    if (count == 0u || count > UINT32_MAX || lba >= device->block_count ||
        count > device->block_count - lba) {
        return false;
    }
    *out_lba = lba;
    *out_count = (uint32_t)count;
    return true;
}

static bool read_tree_node(struct aurora_fs_v2_allocator *allocator,
                           uint64_t fs_block,
                           struct u_node *node) {
    uint64_t lba;
    uint32_t count;
    if (allocator == NULL || allocator->device == NULL || node == NULL ||
        fs_block < allocator->data_start ||
        !fs_block_geometry(allocator->device, allocator->base_bytes, fs_block,
                           allocator->total_fs_blocks, &lba, &count) ||
        !block_device_read(allocator->device, lba, count, node) ||
        !magic_valid(node) || node->header.version != U_VERSION ||
        node->header.level > U_MAX_SUPPORTED_LEVEL ||
        node->header.entry_count == 0u || node->header.entry_count > U_CAPACITY ||
        node->header.last_logical_exclusive <= node->header.first_logical) {
        return false;
    }
    return node->header.checksum == node_checksum(node);
}

static bool entry_contains(const struct u_entry *entry, uint64_t logical_block,
                           uint64_t *out_end) {
    uint64_t end;
    if (entry == NULL || entry->block_count_or_span == 0u ||
        !add_u64(entry->logical_block, entry->block_count_or_span, &end) ||
        logical_block < entry->logical_block || logical_block >= end) {
        return false;
    }
    if (out_end != NULL) {
        *out_end = end;
    }
    return true;
}

static bool node_entries_ordered(const struct u_node *node) {
    if (node == NULL || node->header.entry_count == 0u ||
        node->header.entry_count > U_CAPACITY) {
        return false;
    }
    uint64_t previous_end = node->header.first_logical;
    for (uint16_t i = 0u; i < node->header.entry_count; ++i) {
        uint64_t end;
        const struct u_entry *entry = &node->entries[i];
        if (entry->block_count_or_span == 0u ||
            !add_u64(entry->logical_block, entry->block_count_or_span, &end) ||
            entry->logical_block < node->header.first_logical ||
            end > node->header.last_logical_exclusive ||
            (i != 0u && entry->logical_block < previous_end)) {
            return false;
        }
        previous_end = end;
    }
    return true;
}

static bool lookup_leaf(const struct u_node *leaf, uint64_t logical_block,
                        uint64_t *out_physical_block,
                        uint64_t *out_contiguous_blocks) {
    if (leaf == NULL || leaf->header.level != U_LEVEL_LEAF ||
        out_physical_block == NULL || out_contiguous_blocks == NULL ||
        !node_entries_ordered(leaf)) {
        return false;
    }
    for (uint16_t i = 0u; i < leaf->header.entry_count; ++i) {
        const struct u_entry *entry = &leaf->entries[i];
        if (!entry_contains(entry, logical_block, NULL)) {
            continue;
        }
        uint64_t within = logical_block - entry->logical_block;
        uint64_t physical;
        if (!add_u64(entry->physical_or_child, within, &physical)) {
            return false;
        }
        *out_physical_block = physical;
        *out_contiguous_blocks = entry->block_count_or_span - within;
        return true;
    }
    return false;
}

bool aurora_fs_v2_extent_tree_lookup_unified(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t root_block,
    uint64_t logical_block,
    uint64_t *out_physical_block,
    uint64_t *out_contiguous_blocks
) {
    if (allocator == NULL || allocator->device == NULL ||
        out_physical_block == NULL || out_contiguous_blocks == NULL ||
        !read_tree_node(allocator, root_block, &u_node_io) ||
        logical_block < u_node_io.header.first_logical ||
        logical_block >= u_node_io.header.last_logical_exclusive ||
        !node_entries_ordered(&u_node_io)) {
        return false;
    }

    uint16_t level = u_node_io.header.level;
    for (uint16_t depth = 0u; depth <= U_MAX_SUPPORTED_LEVEL; ++depth) {
        if (level == U_LEVEL_LEAF) {
            return lookup_leaf(&u_node_io, logical_block,
                               out_physical_block, out_contiguous_blocks);
        }

        bool found = false;
        struct u_entry selected = {0};
        for (uint16_t i = 0u; i < u_node_io.header.entry_count; ++i) {
            if (entry_contains(&u_node_io.entries[i], logical_block, NULL)) {
                selected = u_node_io.entries[i];
                found = true;
                break;
            }
        }
        if (!found || selected.physical_or_child < allocator->data_start ||
            selected.physical_or_child >= allocator->total_fs_blocks) {
            return false;
        }

        uint16_t expected_child_level = (uint16_t)(level - 1u);
        uint64_t parent_entry_end;
        if (!entry_contains(&selected, logical_block, &parent_entry_end) ||
            !read_tree_node(allocator, selected.physical_or_child, &u_node_io) ||
            u_node_io.header.level != expected_child_level ||
            u_node_io.header.first_logical < selected.logical_block ||
            u_node_io.header.last_logical_exclusive > parent_entry_end ||
            logical_block < u_node_io.header.first_logical ||
            logical_block >= u_node_io.header.last_logical_exclusive ||
            !node_entries_ordered(&u_node_io)) {
            return false;
        }
        level = u_node_io.header.level;
    }
    return false;
}

static bool geometry_valid(struct aurora_block_device *device,
                           const struct aurora_fs_v2_format_geometry *geometry) {
    return device != NULL && geometry != NULL && geometry->total_fs_blocks != 0u &&
        geometry->inode_blocks != 0u && geometry->inode_start < geometry->total_fs_blocks &&
        geometry->inode_blocks <= geometry->total_fs_blocks - geometry->inode_start &&
        geometry->data_start < geometry->total_fs_blocks &&
        geometry->base_bytes % device->block_size == 0u;
}

static bool read_inode_unified(struct aurora_block_device *device,
                               const struct aurora_fs_v2_format_geometry *geometry,
                               uint64_t inode_index,
                               struct u_inode *out_inode) {
    if (!geometry_valid(device, geometry) || out_inode == NULL) {
        return false;
    }
    uint64_t inode_block_offset = inode_index / U_INODES_PER_BLOCK;
    uint32_t inode_slot = (uint32_t)(inode_index % U_INODES_PER_BLOCK);
    if (inode_block_offset >= geometry->inode_blocks) {
        return false;
    }
    uint64_t fs_block;
    if (!add_u64(geometry->inode_start, inode_block_offset, &fs_block)) {
        return false;
    }
    uint64_t lba;
    uint32_t count;
    if (!fs_block_geometry(device, geometry->base_bytes, fs_block,
                           geometry->total_fs_blocks, &lba, &count) ||
        !block_device_read(device, lba, count, u_inode_io)) {
        return false;
    }
    *out_inode = ((const struct u_inode *)u_inode_io)[inode_slot];
    return true;
}

bool aurora_fs_v2_inode_extent_lookup_unified(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t logical_block,
    uint64_t *out_physical_block,
    uint64_t *out_contiguous_blocks
) {
    struct u_inode inode;
    return allocator != NULL && allocator->device != NULL &&
        read_inode_unified(allocator->device, geometry, inode_index, &inode) &&
        inode.object_id != 0u && inode.type == U_INODE_FILE &&
        inode.extent_tree_root != 0u &&
        aurora_fs_v2_extent_tree_lookup_unified(
            allocator, inode.extent_tree_root, logical_block,
            out_physical_block, out_contiguous_blocks);
}

static bool test_read(struct aurora_block_device *device, uint64_t lba,
                      uint32_t block_count, void *buffer) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }
    uint64_t offset;
    uint64_t length;
    if (!mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > sizeof(u_storage) || length > sizeof(u_storage) - offset) {
        return false;
    }
    uint8_t *out = buffer;
    for (uint64_t i = 0u; i < length; ++i) {
        out[i] = u_storage[offset + i];
    }
    return true;
}

static bool test_write(struct aurora_block_device *device, uint64_t lba,
                       uint32_t block_count, const void *buffer) {
    if (device == NULL || buffer == NULL || device->read_only || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }
    uint64_t offset;
    uint64_t length;
    if (!mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > sizeof(u_storage) || length > sizeof(u_storage) - offset) {
        return false;
    }
    const uint8_t *in = buffer;
    for (uint64_t i = 0u; i < length; ++i) {
        u_storage[offset + i] = in[i];
    }
    return true;
}

static bool test_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static uint8_t *fs_block_ptr(uint64_t fs_block) {
    uint64_t offset = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        fs_block * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset > sizeof(u_storage) ||
        AURORA_FS_V2_FS_BLOCK_SIZE > sizeof(u_storage) - offset) {
        return NULL;
    }
    return &u_storage[offset];
}

static bool store_node(uint64_t block, struct u_node *node) {
    uint8_t *dst = fs_block_ptr(block);
    if (dst == NULL || node == NULL) {
        return false;
    }
    node->header.checksum = node_checksum(node);
    const uint8_t *src = (const uint8_t *)node;
    for (size_t i = 0u; i < sizeof(*node); ++i) {
        dst[i] = src[i];
    }
    return true;
}

static bool seed_tree_and_inode(bool level3_root) {
    struct u_node leaf;
    struct u_node level1;
    struct u_node level2;
    struct u_node level3;
    init_node(&leaf, U_LEVEL_LEAF, U_LOGICAL_BLOCK, U_LOGICAL_BLOCK + 1u);
    leaf.entries[0] = (struct u_entry){U_LOGICAL_BLOCK, U_DATA_BLOCK, 1u, 0u};

    init_node(&level1, U_LEVEL_ONE, U_LOGICAL_BLOCK, U_LOGICAL_BLOCK + 1u);
    level1.entries[0] = (struct u_entry){U_LOGICAL_BLOCK, U_LEAF_BLOCK, 1u, 0u};

    init_node(&level2, U_LEVEL_TWO, U_LOGICAL_BLOCK, U_LOGICAL_BLOCK + 1u);
    level2.entries[0] = (struct u_entry){U_LOGICAL_BLOCK, U_LEVEL1_BLOCK, 1u, 0u};

    init_node(&level3, U_LEVEL_THREE, U_LOGICAL_BLOCK, U_LOGICAL_BLOCK + 1u);
    level3.entries[0] = (struct u_entry){U_LOGICAL_BLOCK, U_LEVEL2_BLOCK, 1u, 0u};

    if (!store_node(U_LEAF_BLOCK, &leaf) ||
        !store_node(U_LEVEL1_BLOCK, &level1) ||
        !store_node(U_LEVEL2_BLOCK, &level2) ||
        (level3_root && !store_node(U_LEVEL3_BLOCK, &level3))) {
        return false;
    }

    uint8_t *inode_dst = fs_block_ptr(U_INODE_BLOCK);
    if (inode_dst == NULL) {
        return false;
    }
    zero_bytes(inode_dst, AURORA_FS_V2_FS_BLOCK_SIZE);
    struct u_inode *inode = (struct u_inode *)inode_dst;
    inode[U_INODE_INDEX].object_id = 2u;
    inode[U_INODE_INDEX].parent_object_id = 1u;
    inode[U_INODE_INDEX].size = AURORA_FS_V2_FS_BLOCK_SIZE;
    inode[U_INODE_INDEX].allocated_bytes = AURORA_FS_V2_FS_BLOCK_SIZE;
    inode[U_INODE_INDEX].generation = 1u;
    inode[U_INODE_INDEX].extent_tree_root = level3_root ? U_LEVEL3_BLOCK : U_LEVEL2_BLOCK;
    inode[U_INODE_INDEX].type = U_INODE_FILE;
    inode[U_INODE_INDEX].extent_count = 1u;
    return true;
}

static bool run_geometry(uint32_t block_size, bool level3_root) {
    zero_bytes(u_storage, sizeof(u_storage));
    if (!seed_tree_and_inode(level3_root)) {
        return false;
    }

    struct aurora_block_device device = {
        .name = level3_root ? "aurorafs-v2-level3-lookup-test" :
                             "aurorafs-v2-unified-lookup-test",
        .block_size = block_size,
        .block_count = sizeof(u_storage) / block_size,
        .read_only = false,
        .context = NULL,
        .read_blocks = test_read,
        .write_blocks = test_write,
        .flush = test_flush
    };
    struct aurora_fs_v2_format_geometry geometry = {
        .base_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES,
        .total_fs_blocks = U_TOTAL_FS_BLOCKS,
        .bitmap_start = 1u,
        .bitmap_blocks = 1u,
        .inode_start = U_INODE_BLOCK,
        .inode_blocks = 1u,
        .data_start = 4u
    };
    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) {
        return false;
    }

    uint64_t physical = 0u;
    uint64_t contiguous = 0u;
    uint64_t root = level3_root ? U_LEVEL3_BLOCK : U_LEVEL2_BLOCK;
    if (!aurora_fs_v2_extent_tree_lookup_unified(
            &allocator, root, U_LOGICAL_BLOCK, &physical, &contiguous) ||
        physical != U_DATA_BLOCK || contiguous != 1u) {
        return false;
    }

    physical = 0u;
    contiguous = 0u;
    return aurora_fs_v2_inode_extent_lookup_unified(
               &allocator, &geometry, U_INODE_INDEX, U_LOGICAL_BLOCK,
               &physical, &contiguous) &&
        physical == U_DATA_BLOCK && contiguous == 1u;
}

bool aurora_fs_v2_unified_lookup_self_test(void) {
    return run_geometry(512u, false) && run_geometry(4096u, false);
}

bool aurora_fs_v2_level3_lookup_self_test(void) {
    return run_geometry(512u, true) && run_geometry(4096u, true);
}
