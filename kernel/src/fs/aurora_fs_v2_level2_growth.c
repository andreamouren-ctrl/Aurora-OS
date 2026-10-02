#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define L2_HEADER_SIZE 64u
#define L2_ENTRY_SIZE 32u
#define L2_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - L2_HEADER_SIZE) / L2_ENTRY_SIZE)
#define L2_LEVEL_LEAF 0u
#define L2_LEVEL_ONE 1u
#define L2_LEVEL_TWO 2u
#define L2_INODE_SIZE 256u
#define L2_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / L2_INODE_SIZE)
#define L2_INODE_FILE 1u
#define L2_TEST_TOTAL_BLOCKS 32768u
#define L2_TEST_BITMAP_START 1u
#define L2_TEST_BITMAP_BLOCKS 1u
#define L2_TEST_INODE_START 2u
#define L2_TEST_INODE_BLOCKS 1u
#define L2_TEST_DATA_START 16u
#define L2_TEST_ROOT 16u
#define L2_TEST_FIRST_LEAF 17u
#define L2_TEST_FIRST_DATA 1024u
#define L2_TEST_SLOT_COUNT 140u
#define L2_FULL_EXTENT_COUNT (L2_CAPACITY * L2_CAPACITY)

struct l2_header {
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

struct l2_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct l2_node {
    struct l2_header header;
    struct l2_entry entries[L2_CAPACITY];
} __attribute__((packed));

struct l2_inode_extent {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct l2_inode {
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
    struct l2_inode_extent extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct l2_test_context {
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint64_t slot_block[L2_TEST_SLOT_COUNT];
    uint8_t slot_data[L2_TEST_SLOT_COUNT][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static struct l2_node root_io;
static struct l2_node child_io;
static struct l2_node leaf_io;
static uint8_t inode_io[AURORA_FS_V2_FS_BLOCK_SIZE];
static struct l2_test_context test_ctx;

_Static_assert(sizeof(struct l2_header) == L2_HEADER_SIZE, "AuroraFS v2 level-2 header size");
_Static_assert(sizeof(struct l2_entry) == L2_ENTRY_SIZE, "AuroraFS v2 level-2 entry size");
_Static_assert(sizeof(struct l2_node) == AURORA_FS_V2_FS_BLOCK_SIZE, "AuroraFS v2 level-2 node size");
_Static_assert(sizeof(struct l2_inode) == L2_INODE_SIZE, "AuroraFS v2 inode size");
_Static_assert(L2_CAPACITY == 126u, "AuroraFS v2 level-2 gate assumes 126 entries");

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

static uint32_t node_checksum(struct l2_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t result = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return result;
}

static void set_magic(struct l2_node *node) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        node->header.magic[i] = magic[i];
    }
}

static bool magic_valid(const struct l2_node *node) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        if (node->header.magic[i] != magic[i]) {
            return false;
        }
    }
    return true;
}

static void init_node(
    struct l2_node *node,
    uint16_t level,
    uint16_t count,
    uint64_t generation,
    uint64_t first,
    uint64_t last
) {
    zero_bytes(node, sizeof(*node));
    set_magic(node);
    node->header.version = 1u;
    node->header.level = level;
    node->header.entry_count = count;
    node->header.generation = generation;
    node->header.first_logical = first;
    node->header.last_logical_exclusive = last;
}

static bool fs_geometry(
    const struct aurora_fs_v2_allocator *allocator,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_count
) {
    if (allocator == NULL || allocator->device == NULL || out_lba == NULL || out_count == NULL ||
        fs_block >= allocator->total_fs_blocks || allocator->device->block_size == 0u ||
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
    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / allocator->device->block_size;
    uint64_t lba = byte_offset / allocator->device->block_size;
    if (count == 0u || count > UINT32_MAX || lba >= allocator->device->block_count ||
        count > allocator->device->block_count - lba) {
        return false;
    }
    *out_lba = lba;
    *out_count = (uint32_t)count;
    return true;
}

static bool read_node(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t block,
    struct l2_node *node
) {
    uint64_t lba;
    uint32_t count;
    if (node == NULL || !fs_geometry(allocator, block, &lba, &count) ||
        !block_device_read(allocator->device, lba, count, node) || !magic_valid(node) ||
        node->header.version != 1u || node->header.entry_count == 0u ||
        node->header.entry_count > L2_CAPACITY ||
        node->header.last_logical_exclusive <= node->header.first_logical) {
        return false;
    }
    return node->header.checksum == node_checksum(node);
}

static bool write_node(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t block,
    struct l2_node *node
) {
    uint64_t lba;
    uint32_t count;
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        node == NULL || !fs_geometry(allocator, block, &lba, &count)) {
        return false;
    }
    node->header.checksum = node_checksum(node);
    return block_device_write(allocator->device, lba, count, node);
}

static bool entry_contains(const struct l2_entry *entry, uint64_t logical) {
    uint64_t end;
    return entry != NULL && entry->block_count_or_span != 0u &&
        add_u64(entry->logical_block, entry->block_count_or_span, &end) &&
        logical >= entry->logical_block && logical < end;
}

static bool lookup_leaf(
    const struct l2_node *leaf,
    uint64_t logical,
    uint64_t *out_physical,
    uint64_t *out_contiguous
) {
    if (leaf == NULL || leaf->header.level != L2_LEVEL_LEAF ||
        out_physical == NULL || out_contiguous == NULL) {
        return false;
    }
    for (uint16_t i = 0u; i < leaf->header.entry_count; ++i) {
        const struct l2_entry *entry = &leaf->entries[i];
        if (!entry_contains(entry, logical)) {
            continue;
        }
        uint64_t within = logical - entry->logical_block;
        *out_physical = entry->physical_or_child + within;
        *out_contiguous = entry->block_count_or_span - within;
        return true;
    }
    return false;
}

bool aurora_fs_v2_extent_tree_lookup_level2(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t root_block,
    uint64_t logical_block,
    uint64_t *out_physical_block,
    uint64_t *out_contiguous_blocks
) {
    if (allocator == NULL || out_physical_block == NULL || out_contiguous_blocks == NULL ||
        root_block < allocator->data_start || root_block >= allocator->total_fs_blocks ||
        !read_node(allocator, root_block, &root_io) ||
        logical_block < root_io.header.first_logical ||
        logical_block >= root_io.header.last_logical_exclusive) {
        return false;
    }
    if (root_io.header.level == L2_LEVEL_LEAF) {
        return lookup_leaf(&root_io, logical_block, out_physical_block, out_contiguous_blocks);
    }
    if (root_io.header.level == L2_LEVEL_ONE) {
        for (uint16_t i = 0u; i < root_io.header.entry_count; ++i) {
            if (!entry_contains(&root_io.entries[i], logical_block)) {
                continue;
            }
            if (!read_node(allocator, root_io.entries[i].physical_or_child, &leaf_io) ||
                leaf_io.header.level != L2_LEVEL_LEAF) {
                return false;
            }
            return lookup_leaf(&leaf_io, logical_block, out_physical_block, out_contiguous_blocks);
        }
        return false;
    }
    if (root_io.header.level != L2_LEVEL_TWO) {
        return false;
    }
    for (uint16_t i = 0u; i < root_io.header.entry_count; ++i) {
        if (!entry_contains(&root_io.entries[i], logical_block)) {
            continue;
        }
        if (!read_node(allocator, root_io.entries[i].physical_or_child, &child_io) ||
            child_io.header.level != L2_LEVEL_ONE) {
            return false;
        }
        for (uint16_t j = 0u; j < child_io.header.entry_count; ++j) {
            if (!entry_contains(&child_io.entries[j], logical_block)) {
                continue;
            }
            if (!read_node(allocator, child_io.entries[j].physical_or_child, &leaf_io) ||
                leaf_io.header.level != L2_LEVEL_LEAF) {
                return false;
            }
            return lookup_leaf(&leaf_io, logical_block, out_physical_block, out_contiguous_blocks);
        }
        return false;
    }
    return false;
}

static void release_block(struct aurora_fs_v2_allocator *allocator, uint64_t block) {
    if (block != 0u) {
        (void)aurora_fs_v2_allocator_free_range(allocator, block, 1u);
    }
}

bool aurora_fs_v2_extent_tree_grow_level1_full_root_cow(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root_block,
    const struct aurora_fs_v2_extent *extent,
    uint64_t *out_new_root_block
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        extent == NULL || out_new_root_block == NULL || extent->block_count == 0u ||
        extent->physical_block < allocator->data_start ||
        !read_node(allocator, old_root_block, &root_io) ||
        root_io.header.level != L2_LEVEL_ONE || root_io.header.entry_count != L2_CAPACITY) {
        return false;
    }
    uint64_t physical_end;
    uint64_t logical_end;
    if (!add_u64(extent->physical_block, extent->block_count, &physical_end) ||
        physical_end > allocator->total_fs_blocks ||
        !add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        extent->logical_block < root_io.header.last_logical_exclusive) {
        return false;
    }
    uint16_t last_index = root_io.header.entry_count - 1u;
    if (!read_node(allocator, root_io.entries[last_index].physical_or_child, &leaf_io) ||
        leaf_io.header.level != L2_LEVEL_LEAF || leaf_io.header.entry_count != L2_CAPACITY ||
        leaf_io.header.last_logical_exclusive != root_io.header.last_logical_exclusive) {
        return false;
    }

    struct l2_node new_leaf;
    struct l2_node new_level1;
    struct l2_node new_level2;
    uint64_t generation = root_io.header.generation + 1u;

    init_node(&new_leaf, L2_LEVEL_LEAF, 1u, generation,
              extent->logical_block, logical_end);
    new_leaf.entries[0] = (struct l2_entry){
        extent->logical_block, extent->physical_block, extent->block_count, 0u
    };

    uint64_t new_leaf_block = 0u;
    uint64_t new_level1_block = 0u;
    uint64_t new_level2_block = 0u;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_leaf_block)) {
        return false;
    }
    if (!write_node(allocator, new_leaf_block, &new_leaf) ||
        !block_device_flush(allocator->device)) {
        release_block(allocator, new_leaf_block);
        return false;
    }

    init_node(&new_level1, L2_LEVEL_ONE, 1u, generation,
              extent->logical_block, logical_end);
    new_level1.entries[0] = (struct l2_entry){
        extent->logical_block, new_leaf_block, extent->block_count, 0u
    };
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_level1_block)) {
        release_block(allocator, new_leaf_block);
        return false;
    }
    if (!write_node(allocator, new_level1_block, &new_level1) ||
        !block_device_flush(allocator->device)) {
        release_block(allocator, new_level1_block);
        release_block(allocator, new_leaf_block);
        return false;
    }

    init_node(&new_level2, L2_LEVEL_TWO, 2u, generation,
              root_io.header.first_logical, logical_end);
    new_level2.entries[0] = (struct l2_entry){
        root_io.header.first_logical,
        old_root_block,
        root_io.header.last_logical_exclusive - root_io.header.first_logical,
        0u
    };
    new_level2.entries[1] = (struct l2_entry){
        extent->logical_block,
        new_level1_block,
        logical_end - extent->logical_block,
        0u
    };
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_level2_block)) {
        release_block(allocator, new_level1_block);
        release_block(allocator, new_leaf_block);
        return false;
    }
    if (!write_node(allocator, new_level2_block, &new_level2) ||
        !block_device_flush(allocator->device)) {
        release_block(allocator, new_level2_block);
        release_block(allocator, new_level1_block);
        release_block(allocator, new_leaf_block);
        return false;
    }

    *out_new_root_block = new_level2_block;
    return true;
}

static bool geometry_valid(
    const struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry
) {
    return device != NULL && geometry != NULL && device->block_size != 0u &&
        device->block_size <= AURORA_FS_V2_FS_BLOCK_SIZE &&
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) == 0u &&
        (geometry->base_bytes % device->block_size) == 0u &&
        geometry->inode_blocks != 0u && geometry->inode_start < geometry->data_start &&
        geometry->data_start < geometry->total_fs_blocks;
}

static bool geometry_lba(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_count
) {
    if (!geometry_valid(device, geometry) || out_lba == NULL || out_count == NULL ||
        fs_block >= geometry->total_fs_blocks) {
        return false;
    }
    uint64_t fs_offset;
    uint64_t byte_offset;
    if (!mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_offset) ||
        !add_u64(geometry->base_bytes, fs_offset, &byte_offset)) {
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

static bool read_fs_block(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t block,
    void *buffer
) {
    uint64_t lba;
    uint32_t count;
    return buffer != NULL && geometry_lba(device, geometry, block, &lba, &count) &&
        block_device_read(device, lba, count, buffer);
}

static bool write_fs_block(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t block,
    const void *buffer
) {
    uint64_t lba;
    uint32_t count;
    return buffer != NULL && !device->read_only &&
        geometry_lba(device, geometry, block, &lba, &count) &&
        block_device_write(device, lba, count, buffer);
}

static bool inode_position(
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t index,
    uint64_t *out_block,
    uint32_t *out_slot
) {
    uint64_t capacity;
    if (geometry == NULL || out_block == NULL || out_slot == NULL ||
        !mul_u64(geometry->inode_blocks, L2_INODES_PER_BLOCK, &capacity) ||
        index >= capacity) {
        return false;
    }
    *out_block = geometry->inode_start + index / L2_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(index % L2_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t index,
    struct l2_inode *out_inode
) {
    uint64_t block;
    uint32_t slot;
    if (out_inode == NULL || !inode_position(geometry, index, &block, &slot) ||
        !read_fs_block(device, geometry, block, inode_io)) {
        return false;
    }
    *out_inode = ((const struct l2_inode *)inode_io)[slot];
    return true;
}

static bool write_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t index,
    const struct l2_inode *inode
) {
    uint64_t block;
    uint32_t slot;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_position(geometry, index, &block, &slot) ||
        !read_fs_block(device, geometry, block, inode_io)) {
        return false;
    }
    ((struct l2_inode *)inode_io)[slot] = *inode;
    return write_fs_block(device, geometry, block, inode_io) && block_device_flush(device);
}

bool aurora_fs_v2_inode_extent_append_level2_grow_cow(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct aurora_fs_v2_extent *extent
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        !geometry_valid(allocator->device, geometry) || extent == NULL || extent->block_count == 0u) {
        return false;
    }
    uint64_t physical_end;
    uint64_t logical_end;
    uint64_t logical_bytes;
    uint64_t allocated_add;
    if (!add_u64(extent->physical_block, extent->block_count, &physical_end) ||
        physical_end > allocator->total_fs_blocks ||
        !add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        !mul_u64(logical_end, AURORA_FS_V2_FS_BLOCK_SIZE, &logical_bytes) ||
        !mul_u64(extent->block_count, AURORA_FS_V2_FS_BLOCK_SIZE, &allocated_add)) {
        return false;
    }
    struct l2_inode inode;
    if (!read_inode(allocator->device, geometry, inode_index, &inode) ||
        inode.object_id == 0u || inode.type != L2_INODE_FILE || inode.extent_tree_root == 0u ||
        inode.extent_count != L2_FULL_EXTENT_COUNT || inode.extent_count == UINT32_MAX ||
        allocated_add > UINT64_MAX - inode.allocated_bytes) {
        return false;
    }
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_grow_level1_full_root_cow(
            allocator, inode.extent_tree_root, extent, &new_root)) {
        return false;
    }
    inode.extent_tree_root = new_root;
    inode.extent_count++;
    inode.allocated_bytes += allocated_add;
    if (logical_bytes > inode.size) {
        inode.size = logical_bytes;
    }
    inode.generation++;
    return write_inode(allocator->device, geometry, inode_index, &inode);
}

bool aurora_fs_v2_inode_extent_lookup_level2(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t logical_block,
    uint64_t *out_physical_block,
    uint64_t *out_contiguous_blocks
) {
    struct l2_inode inode;
    return allocator != NULL && allocator->device != NULL &&
        geometry_valid(allocator->device, geometry) &&
        read_inode(allocator->device, geometry, inode_index, &inode) &&
        inode.object_id != 0u && inode.type == L2_INODE_FILE && inode.extent_tree_root != 0u &&
        aurora_fs_v2_extent_tree_lookup_level2(
            allocator, inode.extent_tree_root, logical_block,
            out_physical_block, out_contiguous_blocks);
}

static void bitmap_set(uint64_t block) {
    test_ctx.bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
}

static int slot_for(uint64_t block, bool create) {
    for (uint32_t i = 0u; i < L2_TEST_SLOT_COUNT; ++i) {
        if (test_ctx.slot_block[i] == block) {
            return (int)i;
        }
    }
    if (!create) {
        return -1;
    }
    for (uint32_t i = 0u; i < L2_TEST_SLOT_COUNT; ++i) {
        if (test_ctx.slot_block[i] == UINT64_MAX) {
            test_ctx.slot_block[i] = block;
            zero_bytes(test_ctx.slot_data[i], AURORA_FS_V2_FS_BLOCK_SIZE);
            return (int)i;
        }
    }
    return -1;
}

static bool transfer_geometry(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t count,
    uint64_t *out_offset,
    uint64_t *out_length
) {
    return device != NULL && out_offset != NULL && out_length != NULL && count != 0u &&
        lba < device->block_count && (uint64_t)count <= device->block_count - lba &&
        mul_u64(lba, device->block_size, out_offset) &&
        mul_u64(count, device->block_size, out_length);
}

static bool sparse_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t count,
    void *buffer
) {
    uint64_t offset;
    uint64_t length;
    if (buffer == NULL || !transfer_geometry(device, lba, count, &offset, &length) ||
        length != AURORA_FS_V2_FS_BLOCK_SIZE) {
        return false;
    }
    uint8_t *out = buffer;
    zero_bytes(out, (size_t)length);
    uint64_t bitmap_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        L2_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            out[i] = test_ctx.bitmap[i];
        }
        return true;
    }
    if (offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u) {
        return false;
    }
    uint64_t block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    int slot = slot_for(block, false);
    if (slot < 0) {
        return true;
    }
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
        out[i] = test_ctx.slot_data[(uint32_t)slot][i];
    }
    return true;
}

static bool sparse_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t count,
    const void *buffer
) {
    uint64_t offset;
    uint64_t length;
    if (buffer == NULL || device == NULL || device->read_only ||
        !transfer_geometry(device, lba, count, &offset, &length) ||
        length != AURORA_FS_V2_FS_BLOCK_SIZE) {
        return false;
    }
    const uint8_t *src = buffer;
    uint64_t bitmap_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        L2_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            test_ctx.bitmap[i] = src[i];
        }
        return true;
    }
    if (offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u) {
        return false;
    }
    uint64_t block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    int slot = slot_for(block, true);
    if (slot < 0) {
        return false;
    }
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
        test_ctx.slot_data[(uint32_t)slot][i] = src[i];
    }
    return true;
}

static bool sparse_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool store_seed(uint64_t block, struct l2_node *node) {
    int slot = slot_for(block, true);
    if (slot < 0) {
        return false;
    }
    node->header.checksum = node_checksum(node);
    const uint8_t *src = (const uint8_t *)node;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
        test_ctx.slot_data[(uint32_t)slot][i] = src[i];
    }
    return true;
}

static bool seed_full_level1_tree(void) {
    struct l2_node root;
    init_node(&root, L2_LEVEL_ONE, L2_CAPACITY, 70u, 0u, L2_FULL_EXTENT_COUNT);
    for (uint32_t leaf_index = 0u; leaf_index < L2_CAPACITY; ++leaf_index) {
        uint64_t first = (uint64_t)leaf_index * L2_CAPACITY;
        uint64_t leaf_block = L2_TEST_FIRST_LEAF + leaf_index;
        struct l2_node leaf;
        init_node(&leaf, L2_LEVEL_LEAF, L2_CAPACITY, 70u,
                  first, first + L2_CAPACITY);
        for (uint32_t i = 0u; i < L2_CAPACITY; ++i) {
            uint64_t logical = first + i;
            leaf.entries[i] = (struct l2_entry){
                logical, L2_TEST_FIRST_DATA + logical, 1u, 0u
            };
        }
        root.entries[leaf_index] = (struct l2_entry){
            first, leaf_block, L2_CAPACITY, 0u
        };
        if (!store_seed(leaf_block, &leaf)) {
            return false;
        }
    }
    return store_seed(L2_TEST_ROOT, &root);
}

static bool setup_test(
    uint32_t block_size,
    struct aurora_block_device *device,
    struct aurora_fs_v2_format_geometry *geometry,
    struct aurora_fs_v2_allocator *allocator
) {
    zero_bytes(&test_ctx, sizeof(test_ctx));
    for (uint32_t i = 0u; i < L2_TEST_SLOT_COUNT; ++i) {
        test_ctx.slot_block[i] = UINT64_MAX;
    }
    for (uint64_t block = 0u; block < L2_TEST_DATA_START; ++block) {
        bitmap_set(block);
    }
    bitmap_set(L2_TEST_ROOT);
    for (uint64_t block = L2_TEST_FIRST_LEAF;
         block < L2_TEST_FIRST_LEAF + L2_CAPACITY;
         ++block) {
        bitmap_set(block);
    }
    for (uint64_t block = L2_TEST_FIRST_DATA;
         block <= L2_TEST_FIRST_DATA + L2_FULL_EXTENT_COUNT;
         ++block) {
        bitmap_set(block);
    }
    uint64_t bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        (uint64_t)L2_TEST_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    *device = (struct aurora_block_device){
        .name = "aurorafs-v2-level2-growth-test",
        .block_size = block_size,
        .block_count = bytes / block_size,
        .read_only = false,
        .context = &test_ctx,
        .read_blocks = sparse_read,
        .write_blocks = sparse_write,
        .flush = sparse_flush
    };
    *geometry = (struct aurora_fs_v2_format_geometry){
        .base_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES,
        .total_fs_blocks = L2_TEST_TOTAL_BLOCKS,
        .bitmap_start = L2_TEST_BITMAP_START,
        .bitmap_blocks = L2_TEST_BITMAP_BLOCKS,
        .inode_start = L2_TEST_INODE_START,
        .inode_blocks = L2_TEST_INODE_BLOCKS,
        .data_start = L2_TEST_DATA_START
    };
    return aurora_fs_v2_allocator_init(
               allocator, device, geometry->base_bytes, geometry->total_fs_blocks,
               geometry->bitmap_start, geometry->bitmap_blocks, geometry->data_start) &&
        seed_full_level1_tree();
}

static bool verify_probes(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t root_block
) {
    static const uint64_t fixed_probes[] = {
        0u, 1u, 125u, 126u, 251u, 252u,
        L2_FULL_EXTENT_COUNT - 2u,
        L2_FULL_EXTENT_COUNT - 1u,
        L2_FULL_EXTENT_COUNT
    };
    for (size_t i = 0u; i < sizeof(fixed_probes) / sizeof(fixed_probes[0]); ++i) {
        uint64_t logical = fixed_probes[i];
        uint64_t physical = 0u;
        uint64_t contiguous = 0u;
        if (!aurora_fs_v2_extent_tree_lookup_level2(
                allocator, root_block, logical, &physical, &contiguous) ||
            physical != L2_TEST_FIRST_DATA + logical || contiguous != 1u) {
            return false;
        }
    }
    for (uint64_t leaf = 0u; leaf < L2_CAPACITY; ++leaf) {
        uint64_t first = leaf * L2_CAPACITY;
        uint64_t last = first + L2_CAPACITY - 1u;
        uint64_t physical = 0u;
        uint64_t contiguous = 0u;
        if (!aurora_fs_v2_extent_tree_lookup_level2(
                allocator, root_block, first, &physical, &contiguous) ||
            physical != L2_TEST_FIRST_DATA + first || contiguous != 1u ||
            !aurora_fs_v2_extent_tree_lookup_level2(
                allocator, root_block, last, &physical, &contiguous) ||
            physical != L2_TEST_FIRST_DATA + last || contiguous != 1u) {
            return false;
        }
    }
    return true;
}

static bool run_tree(uint32_t block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!setup_test(block_size, &device, &geometry, &allocator)) {
        return false;
    }
    struct l2_node old_root;
    struct l2_node old_last_leaf;
    uint64_t last_leaf_block = L2_TEST_FIRST_LEAF + L2_CAPACITY - 1u;
    if (!read_node(&allocator, L2_TEST_ROOT, &old_root) ||
        !read_node(&allocator, last_leaf_block, &old_last_leaf)) {
        return false;
    }
    struct aurora_fs_v2_extent next = {
        .logical_block = L2_FULL_EXTENT_COUNT,
        .physical_block = L2_TEST_FIRST_DATA + L2_FULL_EXTENT_COUNT,
        .block_count = 1u
    };
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_grow_level1_full_root_cow(
            &allocator, L2_TEST_ROOT, &next, &new_root) ||
        new_root == L2_TEST_ROOT || !verify_probes(&allocator, new_root)) {
        return false;
    }
    struct l2_node persisted_root;
    struct l2_node root_after;
    struct l2_node leaf_after;
    if (!read_node(&allocator, new_root, &persisted_root) ||
        persisted_root.header.level != L2_LEVEL_TWO ||
        persisted_root.header.entry_count != 2u ||
        !read_node(&allocator, L2_TEST_ROOT, &root_after) ||
        !read_node(&allocator, last_leaf_block, &leaf_after) ||
        root_after.header.generation != old_root.header.generation ||
        root_after.header.entry_count != old_root.header.entry_count ||
        leaf_after.header.generation != old_last_leaf.header.generation ||
        leaf_after.header.entry_count != old_last_leaf.header.entry_count) {
        return false;
    }
    return true;
}

static bool run_inode(uint32_t block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!setup_test(block_size, &device, &geometry, &allocator)) {
        return false;
    }
    struct l2_inode inode;
    zero_bytes(&inode, sizeof(inode));
    inode.object_id = 2u;
    inode.parent_object_id = 1u;
    inode.size = (uint64_t)L2_FULL_EXTENT_COUNT * AURORA_FS_V2_FS_BLOCK_SIZE;
    inode.allocated_bytes = inode.size;
    inode.generation = 80u;
    inode.extent_tree_root = L2_TEST_ROOT;
    inode.type = L2_INODE_FILE;
    inode.extent_count = L2_FULL_EXTENT_COUNT;
    if (!write_inode(&device, &geometry, 1u, &inode)) {
        return false;
    }
    struct aurora_fs_v2_extent next = {
        .logical_block = L2_FULL_EXTENT_COUNT,
        .physical_block = L2_TEST_FIRST_DATA + L2_FULL_EXTENT_COUNT,
        .block_count = 1u
    };
    if (!aurora_fs_v2_inode_extent_append_level2_grow_cow(
            &allocator, &geometry, 1u, &next)) {
        return false;
    }
    struct l2_inode persisted;
    if (!read_inode(&device, &geometry, 1u, &persisted) ||
        persisted.extent_count != L2_FULL_EXTENT_COUNT + 1u ||
        persisted.extent_tree_root == L2_TEST_ROOT || persisted.generation != 81u ||
        persisted.size != (uint64_t)(L2_FULL_EXTENT_COUNT + 1u) * AURORA_FS_V2_FS_BLOCK_SIZE ||
        persisted.allocated_bytes != persisted.size) {
        return false;
    }
    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) {
        return false;
    }
    static const uint64_t probes[] = {
        0u, 125u, 126u, 251u, 252u,
        L2_FULL_EXTENT_COUNT - 1u, L2_FULL_EXTENT_COUNT
    };
    for (size_t i = 0u; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        uint64_t logical = probes[i];
        uint64_t physical = 0u;
        uint64_t contiguous = 0u;
        if (!aurora_fs_v2_inode_extent_lookup_level2(
                &reopened, &geometry, 1u, logical, &physical, &contiguous) ||
            physical != L2_TEST_FIRST_DATA + logical || contiguous != 1u) {
            return false;
        }
    }
    return true;
}

bool aurora_fs_v2_extent_tree_level2_growth_self_test(void) {
    return run_tree(512u) && run_tree(4096u);
}

bool aurora_fs_v2_inode_level2_growth_self_test(void) {
    return run_inode(512u) && run_inode(4096u);
}
