#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define V2_GROW_INODE_SIZE 256u
#define V2_GROW_INODE_FILE 1u
#define V2_GROW_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2_GROW_INODE_SIZE)
#define V2_GROW_NODE_MAGIC_0 'A'
#define V2_GROW_NODE_MAGIC_1 'U'
#define V2_GROW_NODE_MAGIC_2 'R'
#define V2_GROW_NODE_MAGIC_3 'E'
#define V2_GROW_NODE_MAGIC_4 'X'
#define V2_GROW_NODE_MAGIC_5 'T'
#define V2_GROW_NODE_MAGIC_6 '2'
#define V2_GROW_NODE_MAGIC_7 '\0'
#define V2_GROW_NODE_VERSION 1u
#define V2_GROW_NODE_HEADER_SIZE 64u
#define V2_GROW_NODE_ENTRY_SIZE 32u
#define V2_GROW_NODE_CAPACITY \
    ((AURORA_FS_V2_FS_BLOCK_SIZE - V2_GROW_NODE_HEADER_SIZE) / V2_GROW_NODE_ENTRY_SIZE)
#define V2_GROW_NODE_LEVEL_LEAF 0u
#define V2_GROW_TEST_TOTAL_BLOCKS 1024u
#define V2_GROW_TEST_BITMAP_START 1u
#define V2_GROW_TEST_BITMAP_BLOCKS 1u
#define V2_GROW_TEST_INODE_START 2u
#define V2_GROW_TEST_INODE_BLOCKS 1u
#define V2_GROW_TEST_DATA_START 16u
#define V2_GROW_TEST_OLD_ROOT 16u
#define V2_GROW_TEST_FIRST_DATA 100u
#define V2_GROW_TEST_SLOT_COUNT 10u

struct v2_grow_inode_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2_grow_inode_disk {
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
    struct v2_grow_inode_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct v2_grow_node_header_disk {
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

struct v2_grow_node_entry_disk {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct v2_grow_node_disk {
    struct v2_grow_node_header_disk header;
    struct v2_grow_node_entry_disk entries[V2_GROW_NODE_CAPACITY];
} __attribute__((packed));

struct v2_grow_test_context {
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint64_t slot_block[V2_GROW_TEST_SLOT_COUNT];
    uint8_t slot_data[V2_GROW_TEST_SLOT_COUNT][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static uint8_t grow_inode_io[AURORA_FS_V2_FS_BLOCK_SIZE];
static struct v2_grow_test_context grow_test_context;
static struct v2_grow_node_disk grow_seed_leaf;

_Static_assert(sizeof(struct v2_grow_inode_disk) == V2_GROW_INODE_SIZE,
               "AuroraFS v2 inode must remain 256 bytes");
_Static_assert(sizeof(struct v2_grow_node_header_disk) == V2_GROW_NODE_HEADER_SIZE,
               "AuroraFS v2 extent header must remain 64 bytes");
_Static_assert(sizeof(struct v2_grow_node_entry_disk) == V2_GROW_NODE_ENTRY_SIZE,
               "AuroraFS v2 extent entry must remain 32 bytes");
_Static_assert(sizeof(struct v2_grow_node_disk) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 extent node must fill one filesystem block");
_Static_assert(V2_GROW_NODE_CAPACITY == 126u,
               "AuroraFS v2 inode growth gate assumes 126-entry leaves");

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

static uint32_t node_checksum(struct v2_grow_node_disk *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return checksum;
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

static bool fs_block_lba(
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
    uint64_t fs_block,
    void *buffer
) {
    uint64_t lba;
    uint32_t count;
    return buffer != NULL && fs_block_lba(device, geometry, fs_block, &lba, &count) &&
        block_device_read(device, lba, count, buffer);
}

static bool write_fs_block(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    const void *buffer
) {
    uint64_t lba;
    uint32_t count;
    return buffer != NULL && !device->read_only &&
        fs_block_lba(device, geometry, fs_block, &lba, &count) &&
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
        !mul_u64(geometry->inode_blocks, V2_GROW_INODES_PER_BLOCK, &capacity) ||
        inode_index >= capacity) {
        return false;
    }
    *out_fs_block = geometry->inode_start + inode_index / V2_GROW_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(inode_index % V2_GROW_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct v2_grow_inode_disk *out_inode
) {
    uint64_t fs_block;
    uint32_t slot;
    if (out_inode == NULL || !inode_position(geometry, inode_index, &fs_block, &slot) ||
        !read_fs_block(device, geometry, fs_block, grow_inode_io)) {
        return false;
    }
    const struct v2_grow_inode_disk *inodes =
        (const struct v2_grow_inode_disk *)grow_inode_io;
    *out_inode = inodes[slot];
    return true;
}

static bool write_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct v2_grow_inode_disk *inode
) {
    uint64_t fs_block;
    uint32_t slot;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_position(geometry, inode_index, &fs_block, &slot) ||
        !read_fs_block(device, geometry, fs_block, grow_inode_io)) {
        return false;
    }
    struct v2_grow_inode_disk *inodes = (struct v2_grow_inode_disk *)grow_inode_io;
    inodes[slot] = *inode;
    return write_fs_block(device, geometry, fs_block, grow_inode_io) &&
        block_device_flush(device);
}

static bool extent_valid(
    const struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_extent *extent
) {
    uint64_t physical_end;
    uint64_t logical_end;
    return allocator != NULL && extent != NULL && extent->block_count != 0u &&
        extent->physical_block >= allocator->data_start &&
        add_u64(extent->physical_block, extent->block_count, &physical_end) &&
        physical_end <= allocator->total_fs_blocks &&
        add_u64(extent->logical_block, extent->block_count, &logical_end);
}

bool aurora_fs_v2_inode_extent_append_tree_grow_cow(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct aurora_fs_v2_extent *extent
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        !geometry_valid(allocator->device, geometry) || !extent_valid(allocator, extent)) {
        return false;
    }

    struct v2_grow_inode_disk inode;
    if (!read_inode(allocator->device, geometry, inode_index, &inode) ||
        inode.object_id == 0u || inode.type != V2_GROW_INODE_FILE ||
        inode.extent_tree_root == 0u ||
        inode.extent_count < AURORA_FS_V2_INLINE_EXTENT_COUNT + 1u ||
        inode.extent_count > V2_GROW_NODE_CAPACITY || inode.extent_count == UINT32_MAX) {
        return false;
    }

    uint64_t logical_end;
    uint64_t logical_bytes;
    uint64_t allocated_add;
    uint64_t new_allocated;
    if (!add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        !mul_u64(logical_end, AURORA_FS_V2_FS_BLOCK_SIZE, &logical_bytes) ||
        !mul_u64(extent->block_count, AURORA_FS_V2_FS_BLOCK_SIZE, &allocated_add) ||
        !add_u64(inode.allocated_bytes, allocated_add, &new_allocated)) {
        return false;
    }

    uint64_t new_root = 0u;
    bool tree_ok = inode.extent_count < V2_GROW_NODE_CAPACITY ?
        aurora_fs_v2_extent_tree_clone_append_leaf(
            allocator, inode.extent_tree_root, extent, &new_root) :
        aurora_fs_v2_extent_tree_expand_full_leaf_cow(
            allocator, inode.extent_tree_root, extent, &new_root);
    if (!tree_ok) {
        return false;
    }

    inode.extent_tree_root = new_root;
    inode.extent_count++;
    inode.allocated_bytes = new_allocated;
    if (logical_bytes > inode.size) {
        inode.size = logical_bytes;
    }
    inode.generation++;
    return write_inode(allocator->device, geometry, inode_index, &inode);
}

static void bitmap_set(uint64_t fs_block) {
    grow_test_context.bitmap[fs_block >> 3] |=
        (uint8_t)(1u << (fs_block & 7u));
}

static int slot_for(uint64_t fs_block, bool create) {
    for (uint32_t i = 0u; i < V2_GROW_TEST_SLOT_COUNT; ++i) {
        if (grow_test_context.slot_block[i] == fs_block) {
            return (int)i;
        }
    }
    if (!create) {
        return -1;
    }
    for (uint32_t i = 0u; i < V2_GROW_TEST_SLOT_COUNT; ++i) {
        if (grow_test_context.slot_block[i] == UINT64_MAX) {
            grow_test_context.slot_block[i] = fs_block;
            zero_bytes(grow_test_context.slot_data[i], AURORA_FS_V2_FS_BLOCK_SIZE);
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
        V2_GROW_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            out[i] = grow_test_context.bitmap[i];
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
        out[i] = grow_test_context.slot_data[(uint32_t)slot][i];
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
        V2_GROW_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            grow_test_context.bitmap[i] = source[i];
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
        grow_test_context.slot_data[(uint32_t)slot][i] = source[i];
    }
    return true;
}

static bool sparse_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static void seed_magic(struct v2_grow_node_disk *node) {
    static const uint8_t magic[8] = {
        V2_GROW_NODE_MAGIC_0, V2_GROW_NODE_MAGIC_1,
        V2_GROW_NODE_MAGIC_2, V2_GROW_NODE_MAGIC_3,
        V2_GROW_NODE_MAGIC_4, V2_GROW_NODE_MAGIC_5,
        V2_GROW_NODE_MAGIC_6, V2_GROW_NODE_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        node->header.magic[i] = magic[i];
    }
}

static bool run_growth_geometry(uint32_t device_block_size) {
    zero_bytes(&grow_test_context, sizeof(grow_test_context));
    for (uint32_t i = 0u; i < V2_GROW_TEST_SLOT_COUNT; ++i) {
        grow_test_context.slot_block[i] = UINT64_MAX;
    }

    for (uint64_t block = 0u; block < V2_GROW_TEST_DATA_START; ++block) {
        bitmap_set(block);
    }
    bitmap_set(V2_GROW_TEST_OLD_ROOT);
    for (uint64_t block = V2_GROW_TEST_FIRST_DATA;
         block < V2_GROW_TEST_FIRST_DATA + V2_GROW_NODE_CAPACITY + 1u;
         ++block) {
        bitmap_set(block);
    }

    uint64_t virtual_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        V2_GROW_TEST_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct aurora_block_device device = {
        .name = "aurorafs-v2-inode-growth-test",
        .block_size = device_block_size,
        .block_count = virtual_bytes / device_block_size,
        .read_only = false,
        .context = &grow_test_context,
        .read_blocks = sparse_read,
        .write_blocks = sparse_write,
        .flush = sparse_flush
    };
    struct aurora_fs_v2_format_geometry geometry = {
        .base_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES,
        .total_fs_blocks = V2_GROW_TEST_TOTAL_BLOCKS,
        .bitmap_start = V2_GROW_TEST_BITMAP_START,
        .bitmap_blocks = V2_GROW_TEST_BITMAP_BLOCKS,
        .inode_start = V2_GROW_TEST_INODE_START,
        .inode_blocks = V2_GROW_TEST_INODE_BLOCKS,
        .data_start = V2_GROW_TEST_DATA_START
    };
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

    zero_bytes(&grow_seed_leaf, sizeof(grow_seed_leaf));
    seed_magic(&grow_seed_leaf);
    grow_seed_leaf.header.version = V2_GROW_NODE_VERSION;
    grow_seed_leaf.header.level = V2_GROW_NODE_LEVEL_LEAF;
    grow_seed_leaf.header.entry_count = V2_GROW_NODE_CAPACITY;
    grow_seed_leaf.header.generation = 9u;
    grow_seed_leaf.header.first_logical = 0u;
    grow_seed_leaf.header.last_logical_exclusive = V2_GROW_NODE_CAPACITY;
    for (uint32_t i = 0u; i < V2_GROW_NODE_CAPACITY; ++i) {
        grow_seed_leaf.entries[i].logical_block = i;
        grow_seed_leaf.entries[i].physical_or_child = V2_GROW_TEST_FIRST_DATA + i;
        grow_seed_leaf.entries[i].block_count_or_span = 1u;
    }
    grow_seed_leaf.header.checksum = node_checksum(&grow_seed_leaf);
    int root_slot = slot_for(V2_GROW_TEST_OLD_ROOT, true);
    if (root_slot < 0) {
        return false;
    }
    const uint8_t *root_bytes = (const uint8_t *)&grow_seed_leaf;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
        grow_test_context.slot_data[(uint32_t)root_slot][i] = root_bytes[i];
    }

    struct v2_grow_inode_disk inode;
    zero_bytes(&inode, sizeof(inode));
    inode.object_id = 2u;
    inode.parent_object_id = 1u;
    inode.size = (uint64_t)V2_GROW_NODE_CAPACITY * AURORA_FS_V2_FS_BLOCK_SIZE;
    inode.allocated_bytes = inode.size;
    inode.generation = 20u;
    inode.extent_tree_root = V2_GROW_TEST_OLD_ROOT;
    inode.type = V2_GROW_INODE_FILE;
    inode.extent_count = V2_GROW_NODE_CAPACITY;
    if (!write_inode(&device, &geometry, 1u, &inode)) {
        return false;
    }

    struct aurora_fs_v2_extent next = {
        .logical_block = V2_GROW_NODE_CAPACITY,
        .physical_block = V2_GROW_TEST_FIRST_DATA + V2_GROW_NODE_CAPACITY,
        .block_count = 1u
    };
    if (!aurora_fs_v2_inode_extent_append_tree_grow_cow(
            &allocator, &geometry, 1u, &next)) {
        return false;
    }

    struct v2_grow_inode_disk persisted;
    if (!read_inode(&device, &geometry, 1u, &persisted) ||
        persisted.extent_count != V2_GROW_NODE_CAPACITY + 1u ||
        persisted.extent_tree_root == V2_GROW_TEST_OLD_ROOT ||
        persisted.extent_tree_root < V2_GROW_TEST_DATA_START ||
        persisted.generation != 21u ||
        persisted.size != (uint64_t)(V2_GROW_NODE_CAPACITY + 1u) * AURORA_FS_V2_FS_BLOCK_SIZE ||
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
    for (uint64_t logical = 0u; logical < V2_GROW_NODE_CAPACITY + 1u; ++logical) {
        uint64_t physical = 0u;
        uint64_t contiguous = 0u;
        if (!aurora_fs_v2_inode_extent_lookup(
                &reopened,
                &geometry,
                1u,
                logical,
                &physical,
                &contiguous) ||
            physical != V2_GROW_TEST_FIRST_DATA + logical || contiguous != 1u) {
            return false;
        }
    }
    return true;
}

bool aurora_fs_v2_inode_tree_growth_self_test(void) {
    return run_growth_geometry(512u) && run_growth_geometry(4096u);
}
