#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define L1_MAGIC_0 'A'
#define L1_MAGIC_1 'U'
#define L1_MAGIC_2 'R'
#define L1_MAGIC_3 'E'
#define L1_MAGIC_4 'X'
#define L1_MAGIC_5 'T'
#define L1_MAGIC_6 '2'
#define L1_MAGIC_7 '\0'
#define L1_VERSION 1u
#define L1_HEADER_SIZE 64u
#define L1_ENTRY_SIZE 32u
#define L1_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - L1_HEADER_SIZE) / L1_ENTRY_SIZE)
#define L1_LEVEL_LEAF 0u
#define L1_LEVEL_ROOT 1u
#define L1_INODE_SIZE 256u
#define L1_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / L1_INODE_SIZE)
#define L1_INODE_FILE 1u
#define L1_TEST_TOTAL_BLOCKS 1024u
#define L1_TEST_BITMAP_START 1u
#define L1_TEST_BITMAP_BLOCKS 1u
#define L1_TEST_INODE_START 2u
#define L1_TEST_INODE_BLOCKS 1u
#define L1_TEST_DATA_START 16u
#define L1_TEST_ROOT 16u
#define L1_TEST_LEAF_A 17u
#define L1_TEST_LEAF_B 18u
#define L1_TEST_FIRST_DATA 100u
#define L1_TEST_SLOT_COUNT 16u

struct l1_node_header_disk {
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

struct l1_node_entry_disk {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct l1_node_disk {
    struct l1_node_header_disk header;
    struct l1_node_entry_disk entries[L1_CAPACITY];
} __attribute__((packed));

struct l1_inode_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct l1_inode_disk {
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
    struct l1_inode_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct l1_test_context {
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint64_t slot_block[L1_TEST_SLOT_COUNT];
    uint8_t slot_data[L1_TEST_SLOT_COUNT][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static struct l1_node_disk l1_root_scratch;
static struct l1_node_disk l1_leaf_scratch;
static uint8_t l1_inode_io[AURORA_FS_V2_FS_BLOCK_SIZE];
static struct l1_test_context l1_test;

_Static_assert(sizeof(struct l1_node_header_disk) == L1_HEADER_SIZE,
               "AuroraFS v2 level-1 header must remain 64 bytes");
_Static_assert(sizeof(struct l1_node_entry_disk) == L1_ENTRY_SIZE,
               "AuroraFS v2 level-1 entry must remain 32 bytes");
_Static_assert(sizeof(struct l1_node_disk) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 level-1 node must fill one filesystem block");
_Static_assert(sizeof(struct l1_inode_disk) == L1_INODE_SIZE,
               "AuroraFS v2 inode must remain 256 bytes");
_Static_assert(L1_CAPACITY == 126u,
               "AuroraFS v2 level-1 gate assumes 126 entries per node");

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

static uint32_t node_checksum(struct l1_node_disk *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return checksum;
}

static void set_magic(struct l1_node_disk *node) {
    static const uint8_t magic[8] = {
        L1_MAGIC_0, L1_MAGIC_1, L1_MAGIC_2, L1_MAGIC_3,
        L1_MAGIC_4, L1_MAGIC_5, L1_MAGIC_6, L1_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        node->header.magic[i] = magic[i];
    }
}

static bool magic_valid(const struct l1_node_disk *node) {
    static const uint8_t magic[8] = {
        L1_MAGIC_0, L1_MAGIC_1, L1_MAGIC_2, L1_MAGIC_3,
        L1_MAGIC_4, L1_MAGIC_5, L1_MAGIC_6, L1_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        if (node->header.magic[i] != magic[i]) {
            return false;
        }
    }
    return true;
}

static void init_node(
    struct l1_node_disk *node,
    uint16_t level,
    uint16_t entry_count,
    uint64_t generation,
    uint64_t first_logical,
    uint64_t last_logical_exclusive
) {
    zero_bytes(node, sizeof(*node));
    set_magic(node);
    node->header.version = L1_VERSION;
    node->header.level = level;
    node->header.entry_count = entry_count;
    node->header.generation = generation;
    node->header.first_logical = first_logical;
    node->header.last_logical_exclusive = last_logical_exclusive;
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
    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / allocator->device->block_size;
    uint64_t lba = byte_offset / allocator->device->block_size;
    if (count == 0u || count > UINT32_MAX || lba >= allocator->device->block_count ||
        count > allocator->device->block_count - lba) {
        return false;
    }
    *out_lba = lba;
    *out_device_blocks = (uint32_t)count;
    return true;
}

static bool read_node(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t fs_block,
    struct l1_node_disk *node
) {
    uint64_t lba;
    uint32_t count;
    if (node == NULL || !fs_block_geometry(allocator, fs_block, &lba, &count) ||
        !block_device_read(allocator->device, lba, count, node) ||
        !magic_valid(node) || node->header.version != L1_VERSION ||
        node->header.entry_count == 0u || node->header.entry_count > L1_CAPACITY ||
        node->header.last_logical_exclusive <= node->header.first_logical) {
        return false;
    }
    return node->header.checksum == node_checksum(node);
}

static bool write_node(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t fs_block,
    struct l1_node_disk *node
) {
    uint64_t lba;
    uint32_t count;
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        node == NULL || !fs_block_geometry(allocator, fs_block, &lba, &count)) {
        return false;
    }
    node->header.checksum = node_checksum(node);
    return block_device_write(allocator->device, lba, count, node);
}

static void release_block(struct aurora_fs_v2_allocator *allocator, uint64_t block) {
    if (block != 0u) {
        (void)aurora_fs_v2_allocator_free_range(allocator, block, 1u);
    }
}

bool aurora_fs_v2_extent_tree_append_level1_cow(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root_block,
    const struct aurora_fs_v2_extent *extent,
    uint64_t *out_new_root_block
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        extent == NULL || out_new_root_block == NULL || extent->block_count == 0u ||
        extent->physical_block < allocator->data_start ||
        !read_node(allocator, old_root_block, &l1_root_scratch) ||
        l1_root_scratch.header.level != L1_LEVEL_ROOT ||
        l1_root_scratch.header.entry_count < 2u ||
        l1_root_scratch.header.entry_count > L1_CAPACITY) {
        return false;
    }

    uint64_t physical_end;
    uint64_t logical_end;
    if (!add_u64(extent->physical_block, extent->block_count, &physical_end) ||
        physical_end > allocator->total_fs_blocks ||
        !add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        extent->logical_block < l1_root_scratch.header.last_logical_exclusive) {
        return false;
    }

    uint16_t last_index = l1_root_scratch.header.entry_count - 1u;
    uint64_t old_leaf_block = l1_root_scratch.entries[last_index].physical_or_child;
    uint64_t old_child_start = l1_root_scratch.entries[last_index].logical_block;
    uint64_t old_child_span = l1_root_scratch.entries[last_index].block_count_or_span;
    uint64_t old_child_end;
    if (old_leaf_block < allocator->data_start || old_child_span == 0u ||
        !add_u64(old_child_start, old_child_span, &old_child_end) ||
        old_child_end != l1_root_scratch.header.last_logical_exclusive ||
        !read_node(allocator, old_leaf_block, &l1_leaf_scratch) ||
        l1_leaf_scratch.header.level != L1_LEVEL_LEAF ||
        l1_leaf_scratch.header.entry_count >= L1_CAPACITY ||
        l1_leaf_scratch.header.first_logical != old_child_start ||
        l1_leaf_scratch.header.last_logical_exclusive != old_child_end) {
        return false;
    }

    uint16_t leaf_slot = l1_leaf_scratch.header.entry_count;
    l1_leaf_scratch.entries[leaf_slot].logical_block = extent->logical_block;
    l1_leaf_scratch.entries[leaf_slot].physical_or_child = extent->physical_block;
    l1_leaf_scratch.entries[leaf_slot].block_count_or_span = extent->block_count;
    l1_leaf_scratch.entries[leaf_slot].reserved = 0u;
    l1_leaf_scratch.header.entry_count++;
    l1_leaf_scratch.header.last_logical_exclusive = logical_end;
    l1_leaf_scratch.header.generation++;

    uint64_t new_leaf = 0u;
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_leaf)) {
        return false;
    }
    if (!write_node(allocator, new_leaf, &l1_leaf_scratch) ||
        !block_device_flush(allocator->device)) {
        release_block(allocator, new_leaf);
        return false;
    }

    l1_root_scratch.entries[last_index].physical_or_child = new_leaf;
    l1_root_scratch.entries[last_index].block_count_or_span = logical_end - old_child_start;
    l1_root_scratch.header.last_logical_exclusive = logical_end;
    l1_root_scratch.header.generation++;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_root)) {
        release_block(allocator, new_leaf);
        return false;
    }
    if (!write_node(allocator, new_root, &l1_root_scratch) ||
        !block_device_flush(allocator->device)) {
        release_block(allocator, new_root);
        release_block(allocator, new_leaf);
        return false;
    }

    *out_new_root_block = new_root;
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

static bool geometry_read_block(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    void *buffer
) {
    uint64_t lba;
    uint32_t count;
    return buffer != NULL && geometry_lba(device, geometry, fs_block, &lba, &count) &&
        block_device_read(device, lba, count, buffer);
}

static bool geometry_write_block(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    const void *buffer
) {
    uint64_t lba;
    uint32_t count;
    return buffer != NULL && !device->read_only &&
        geometry_lba(device, geometry, fs_block, &lba, &count) &&
        block_device_write(device, lba, count, buffer);
}

static bool inode_position(
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t *out_fs_block,
    uint32_t *out_slot
) {
    uint64_t capacity;
    if (geometry == NULL || out_fs_block == NULL || out_slot == NULL ||
        !mul_u64(geometry->inode_blocks, L1_INODES_PER_BLOCK, &capacity) ||
        inode_index >= capacity) {
        return false;
    }
    *out_fs_block = geometry->inode_start + inode_index / L1_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(inode_index % L1_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct l1_inode_disk *out_inode
) {
    uint64_t fs_block;
    uint32_t slot;
    if (out_inode == NULL || !inode_position(geometry, inode_index, &fs_block, &slot) ||
        !geometry_read_block(device, geometry, fs_block, l1_inode_io)) {
        return false;
    }
    const struct l1_inode_disk *inodes = (const struct l1_inode_disk *)l1_inode_io;
    *out_inode = inodes[slot];
    return true;
}

static bool write_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct l1_inode_disk *inode
) {
    uint64_t fs_block;
    uint32_t slot;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_position(geometry, inode_index, &fs_block, &slot) ||
        !geometry_read_block(device, geometry, fs_block, l1_inode_io)) {
        return false;
    }
    struct l1_inode_disk *inodes = (struct l1_inode_disk *)l1_inode_io;
    inodes[slot] = *inode;
    return geometry_write_block(device, geometry, fs_block, l1_inode_io) &&
        block_device_flush(device);
}

bool aurora_fs_v2_inode_extent_append_level1_cow(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct aurora_fs_v2_extent *extent
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        !geometry_valid(allocator->device, geometry) || extent == NULL ||
        extent->block_count == 0u || extent->physical_block < allocator->data_start) {
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

    struct l1_inode_disk inode;
    if (!read_inode(allocator->device, geometry, inode_index, &inode) ||
        inode.object_id == 0u || inode.type != L1_INODE_FILE ||
        inode.extent_tree_root == 0u || inode.extent_count < 127u ||
        inode.extent_count == UINT32_MAX ||
        allocated_add > UINT64_MAX - inode.allocated_bytes) {
        return false;
    }

    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_append_level1_cow(
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

static void bitmap_set(uint64_t fs_block) {
    l1_test.bitmap[fs_block >> 3] |= (uint8_t)(1u << (fs_block & 7u));
}

static int slot_for(uint64_t fs_block, bool create) {
    for (uint32_t i = 0u; i < L1_TEST_SLOT_COUNT; ++i) {
        if (l1_test.slot_block[i] == fs_block) {
            return (int)i;
        }
    }
    if (!create) {
        return -1;
    }
    for (uint32_t i = 0u; i < L1_TEST_SLOT_COUNT; ++i) {
        if (l1_test.slot_block[i] == UINT64_MAX) {
            l1_test.slot_block[i] = fs_block;
            zero_bytes(l1_test.slot_data[i], AURORA_FS_V2_FS_BLOCK_SIZE);
            return (int)i;
        }
    }
    return -1;
}

static bool transfer_geometry(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    uint64_t *out_offset,
    uint64_t *out_length
) {
    return device != NULL && out_offset != NULL && out_length != NULL &&
        block_count != 0u && lba < device->block_count &&
        (uint64_t)block_count <= device->block_count - lba &&
        mul_u64(lba, device->block_size, out_offset) &&
        mul_u64(block_count, device->block_size, out_length);
}

static bool sparse_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    uint64_t offset;
    uint64_t length;
    if (buffer == NULL || !transfer_geometry(device, lba, block_count, &offset, &length) ||
        length != AURORA_FS_V2_FS_BLOCK_SIZE) {
        return false;
    }
    uint8_t *out = buffer;
    zero_bytes(out, (size_t)length);
    uint64_t bitmap_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        L1_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            out[i] = l1_test.bitmap[i];
        }
        return true;
    }
    if (offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u) {
        return false;
    }
    uint64_t fs_block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    int slot = slot_for(fs_block, false);
    if (slot < 0) {
        return true;
    }
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
        out[i] = l1_test.slot_data[(uint32_t)slot][i];
    }
    return true;
}

static bool sparse_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    uint64_t offset;
    uint64_t length;
    if (buffer == NULL || device == NULL || device->read_only ||
        !transfer_geometry(device, lba, block_count, &offset, &length) ||
        length != AURORA_FS_V2_FS_BLOCK_SIZE) {
        return false;
    }
    const uint8_t *source = buffer;
    uint64_t bitmap_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        L1_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            l1_test.bitmap[i] = source[i];
        }
        return true;
    }
    if (offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u) {
        return false;
    }
    uint64_t fs_block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    int slot = slot_for(fs_block, true);
    if (slot < 0) {
        return false;
    }
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
        l1_test.slot_data[(uint32_t)slot][i] = source[i];
    }
    return true;
}

static bool sparse_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool store_seed_node(uint64_t block, struct l1_node_disk *node) {
    int slot = slot_for(block, true);
    if (slot < 0) {
        return false;
    }
    node->header.checksum = node_checksum(node);
    const uint8_t *bytes = (const uint8_t *)node;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
        l1_test.slot_data[(uint32_t)slot][i] = bytes[i];
    }
    return true;
}

static bool seed_level1_tree(void) {
    struct l1_node_disk leaf_a;
    struct l1_node_disk leaf_b;
    struct l1_node_disk root;
    init_node(&leaf_a, L1_LEVEL_LEAF, L1_CAPACITY, 10u, 0u, L1_CAPACITY);
    for (uint32_t i = 0u; i < L1_CAPACITY; ++i) {
        leaf_a.entries[i].logical_block = i;
        leaf_a.entries[i].physical_or_child = L1_TEST_FIRST_DATA + i;
        leaf_a.entries[i].block_count_or_span = 1u;
    }
    init_node(&leaf_b, L1_LEVEL_LEAF, 1u, 10u, L1_CAPACITY, L1_CAPACITY + 1u);
    leaf_b.entries[0].logical_block = L1_CAPACITY;
    leaf_b.entries[0].physical_or_child = L1_TEST_FIRST_DATA + L1_CAPACITY;
    leaf_b.entries[0].block_count_or_span = 1u;
    init_node(&root, L1_LEVEL_ROOT, 2u, 10u, 0u, L1_CAPACITY + 1u);
    root.entries[0].logical_block = 0u;
    root.entries[0].physical_or_child = L1_TEST_LEAF_A;
    root.entries[0].block_count_or_span = L1_CAPACITY;
    root.entries[1].logical_block = L1_CAPACITY;
    root.entries[1].physical_or_child = L1_TEST_LEAF_B;
    root.entries[1].block_count_or_span = 1u;
    return store_seed_node(L1_TEST_LEAF_A, &leaf_a) &&
        store_seed_node(L1_TEST_LEAF_B, &leaf_b) &&
        store_seed_node(L1_TEST_ROOT, &root);
}

static bool setup_test(
    uint32_t device_block_size,
    struct aurora_block_device *device,
    struct aurora_fs_v2_format_geometry *geometry,
    struct aurora_fs_v2_allocator *allocator
) {
    zero_bytes(&l1_test, sizeof(l1_test));
    for (uint32_t i = 0u; i < L1_TEST_SLOT_COUNT; ++i) {
        l1_test.slot_block[i] = UINT64_MAX;
    }
    for (uint64_t block = 0u; block < L1_TEST_DATA_START; ++block) {
        bitmap_set(block);
    }
    bitmap_set(L1_TEST_ROOT);
    bitmap_set(L1_TEST_LEAF_A);
    bitmap_set(L1_TEST_LEAF_B);
    for (uint64_t block = L1_TEST_FIRST_DATA;
         block < L1_TEST_FIRST_DATA + L1_CAPACITY + 2u;
         ++block) {
        bitmap_set(block);
    }
    uint64_t virtual_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        L1_TEST_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    *device = (struct aurora_block_device) {
        .name = "aurorafs-v2-level1-append-test",
        .block_size = device_block_size,
        .block_count = virtual_bytes / device_block_size,
        .read_only = false,
        .context = &l1_test,
        .read_blocks = sparse_read,
        .write_blocks = sparse_write,
        .flush = sparse_flush
    };
    *geometry = (struct aurora_fs_v2_format_geometry) {
        .base_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES,
        .total_fs_blocks = L1_TEST_TOTAL_BLOCKS,
        .bitmap_start = L1_TEST_BITMAP_START,
        .bitmap_blocks = L1_TEST_BITMAP_BLOCKS,
        .inode_start = L1_TEST_INODE_START,
        .inode_blocks = L1_TEST_INODE_BLOCKS,
        .data_start = L1_TEST_DATA_START
    };
    if (!aurora_fs_v2_allocator_init(
            allocator,
            device,
            geometry->base_bytes,
            geometry->total_fs_blocks,
            geometry->bitmap_start,
            geometry->bitmap_blocks,
            geometry->data_start)) {
        return false;
    }
    return seed_level1_tree();
}

static bool run_tree_geometry(uint32_t device_block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!setup_test(device_block_size, &device, &geometry, &allocator)) {
        return false;
    }
    struct l1_node_disk old_root;
    struct l1_node_disk old_leaf;
    if (!read_node(&allocator, L1_TEST_ROOT, &old_root) ||
        !read_node(&allocator, L1_TEST_LEAF_B, &old_leaf)) {
        return false;
    }
    struct aurora_fs_v2_extent next = {
        .logical_block = L1_CAPACITY + 1u,
        .physical_block = L1_TEST_FIRST_DATA + L1_CAPACITY + 1u,
        .block_count = 1u
    };
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_append_level1_cow(
            &allocator, L1_TEST_ROOT, &next, &new_root) ||
        new_root == L1_TEST_ROOT) {
        return false;
    }
    for (uint64_t logical = 0u; logical < L1_CAPACITY + 2u; ++logical) {
        uint64_t physical = 0u;
        uint64_t contiguous = 0u;
        if (!aurora_fs_v2_extent_tree_lookup(
                &allocator, new_root, logical, &physical, &contiguous) ||
            physical != L1_TEST_FIRST_DATA + logical || contiguous != 1u) {
            return false;
        }
    }
    struct l1_node_disk root_after;
    struct l1_node_disk leaf_after;
    if (!read_node(&allocator, L1_TEST_ROOT, &root_after) ||
        !read_node(&allocator, L1_TEST_LEAF_B, &leaf_after) ||
        root_after.header.generation != old_root.header.generation ||
        root_after.header.last_logical_exclusive != old_root.header.last_logical_exclusive ||
        leaf_after.header.generation != old_leaf.header.generation ||
        leaf_after.header.entry_count != old_leaf.header.entry_count) {
        return false;
    }
    return true;
}

static bool run_inode_geometry(uint32_t device_block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!setup_test(device_block_size, &device, &geometry, &allocator)) {
        return false;
    }
    struct l1_inode_disk inode;
    zero_bytes(&inode, sizeof(inode));
    inode.object_id = 2u;
    inode.parent_object_id = 1u;
    inode.size = (uint64_t)(L1_CAPACITY + 1u) * AURORA_FS_V2_FS_BLOCK_SIZE;
    inode.allocated_bytes = inode.size;
    inode.generation = 30u;
    inode.extent_tree_root = L1_TEST_ROOT;
    inode.type = L1_INODE_FILE;
    inode.extent_count = L1_CAPACITY + 1u;
    if (!write_inode(&device, &geometry, 1u, &inode)) {
        return false;
    }
    struct aurora_fs_v2_extent next = {
        .logical_block = L1_CAPACITY + 1u,
        .physical_block = L1_TEST_FIRST_DATA + L1_CAPACITY + 1u,
        .block_count = 1u
    };
    if (!aurora_fs_v2_inode_extent_append_level1_cow(
            &allocator, &geometry, 1u, &next)) {
        return false;
    }
    struct l1_inode_disk persisted;
    if (!read_inode(&device, &geometry, 1u, &persisted) ||
        persisted.extent_count != L1_CAPACITY + 2u ||
        persisted.extent_tree_root == L1_TEST_ROOT ||
        persisted.generation != 31u ||
        persisted.size != (uint64_t)(L1_CAPACITY + 2u) * AURORA_FS_V2_FS_BLOCK_SIZE ||
        persisted.allocated_bytes != persisted.size) {
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
    for (uint64_t logical = 0u; logical < L1_CAPACITY + 2u; ++logical) {
        uint64_t physical = 0u;
        uint64_t contiguous = 0u;
        if (!aurora_fs_v2_inode_extent_lookup(
                &reopened, &geometry, 1u, logical, &physical, &contiguous) ||
            physical != L1_TEST_FIRST_DATA + logical || contiguous != 1u) {
            return false;
        }
    }
    return true;
}

bool aurora_fs_v2_extent_tree_level1_append_self_test(void) {
    return run_tree_geometry(512u) && run_tree_geometry(4096u);
}

bool aurora_fs_v2_inode_level1_append_self_test(void) {
    return run_inode_geometry(512u) && run_inode_geometry(4096u);
}
