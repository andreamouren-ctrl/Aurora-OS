#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_inode_publish.h>
#include <aurora/block_device.h>

#define F1C_HEADER_SIZE 64u
#define F1C_ENTRY_SIZE 32u
#define F1C_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - F1C_HEADER_SIZE) / F1C_ENTRY_SIZE)
#define F1C_LEVEL_LEAF 0u
#define F1C_LEVEL_ONE 1u
#define F1C_LEVEL_TWO 2u
#define F1C_INODE_SIZE 256u
#define F1C_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / F1C_INODE_SIZE)
#define F1C_INODE_FILE 1u
#define F1C_TOTAL_BLOCKS 32768u
#define F1C_BITMAP_START 1u
#define F1C_INODE_START 2u
#define F1C_INODE_BLOCKS 1u
#define F1C_DATA_START 16u
#define F1C_OLD_LEVEL1 20u
#define F1C_OLD_LEVEL2 21u
#define F1C_FIRST_LEAF 32u
#define F1C_FIRST_DATA 1024u
#define F1C_FULL_LEVEL1_EXTENTS (F1C_CAPACITY * F1C_CAPACITY)
#define F1C_NEW_DATA (F1C_FIRST_DATA + F1C_FULL_LEVEL1_EXTENTS)
#define F1C_SLOT_COUNT 8u
#define F1C_INODE_INDEX 1u

struct f1c_header {
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

struct f1c_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct f1c_node {
    struct f1c_header header;
    struct f1c_entry entries[F1C_CAPACITY];
} __attribute__((packed));

struct f1c_inode_extent {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct f1c_inode {
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
    struct f1c_inode_extent extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct f1c_test_context {
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint8_t inode_block[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint64_t slot_block[F1C_SLOT_COUNT];
    uint8_t slot_data[F1C_SLOT_COUNT][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static struct f1c_test_context test_ctx;

_Static_assert(sizeof(struct f1c_header) == F1C_HEADER_SIZE,
               "AuroraFS v2 full-level1 commit header size");
_Static_assert(sizeof(struct f1c_entry) == F1C_ENTRY_SIZE,
               "AuroraFS v2 full-level1 commit entry size");
_Static_assert(sizeof(struct f1c_node) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 full-level1 commit node size");
_Static_assert(sizeof(struct f1c_inode) == F1C_INODE_SIZE,
               "AuroraFS v2 full-level1 commit inode size");
_Static_assert(F1C_CAPACITY == 126u,
               "AuroraFS v2 full-level1 commit assumes 126 entries");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) {
        bytes[i] = 0u;
    }
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

static uint32_t node_checksum(struct f1c_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t result = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return result;
}

static void set_magic(struct f1c_node *node) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        node->header.magic[i] = magic[i];
    }
}

static void init_node(struct f1c_node *node, uint16_t level, uint16_t count,
                      uint64_t generation, uint64_t first, uint64_t last) {
    zero_bytes(node, sizeof(*node));
    set_magic(node);
    node->header.version = 1u;
    node->header.level = level;
    node->header.entry_count = count;
    node->header.generation = generation;
    node->header.first_logical = first;
    node->header.last_logical_exclusive = last;
}

static void seed_old_level1(struct f1c_node *node) {
    init_node(node, F1C_LEVEL_ONE, F1C_CAPACITY, 20u,
              0u, F1C_FULL_LEVEL1_EXTENTS);
    for (uint16_t child = 0u; child < F1C_CAPACITY; ++child) {
        uint64_t first = (uint64_t)child * F1C_CAPACITY;
        node->entries[child] = (struct f1c_entry){
            first, F1C_FIRST_LEAF + child, F1C_CAPACITY, 0u
        };
    }
    node->header.checksum = node_checksum(node);
}

static void seed_old_root(struct f1c_node *node) {
    init_node(node, F1C_LEVEL_TWO, 1u, 20u,
              0u, F1C_FULL_LEVEL1_EXTENTS);
    node->entries[0] = (struct f1c_entry){
        0u, F1C_OLD_LEVEL1, F1C_FULL_LEVEL1_EXTENTS, 0u
    };
    node->header.checksum = node_checksum(node);
}

static void seed_old_leaf(uint64_t fs_block, struct f1c_node *node) {
    uint64_t child = fs_block - F1C_FIRST_LEAF;
    uint64_t first = child * F1C_CAPACITY;
    init_node(node, F1C_LEVEL_LEAF, F1C_CAPACITY, 20u,
              first, first + F1C_CAPACITY);
    for (uint16_t entry = 0u; entry < F1C_CAPACITY; ++entry) {
        uint64_t logical = first + entry;
        node->entries[entry] = (struct f1c_entry){
            logical, F1C_FIRST_DATA + logical, 1u, 0u
        };
    }
    node->header.checksum = node_checksum(node);
}

static int slot_for(uint64_t block, bool create) {
    for (uint32_t i = 0u; i < F1C_SLOT_COUNT; ++i) {
        if (test_ctx.slot_block[i] == block) {
            return (int)i;
        }
    }
    if (!create) {
        return -1;
    }
    for (uint32_t i = 0u; i < F1C_SLOT_COUNT; ++i) {
        if (test_ctx.slot_block[i] == UINT64_MAX) {
            test_ctx.slot_block[i] = block;
            zero_bytes(test_ctx.slot_data[i], AURORA_FS_V2_FS_BLOCK_SIZE);
            return (int)i;
        }
    }
    return -1;
}

static bool transfer_geometry(struct aurora_block_device *device, uint64_t lba,
                              uint32_t count, uint64_t *out_offset, uint64_t *out_length) {
    return device != NULL && out_offset != NULL && out_length != NULL && count != 0u &&
        lba < device->block_count && (uint64_t)count <= device->block_count - lba &&
        mul_u64(lba, device->block_size, out_offset) &&
        mul_u64(count, device->block_size, out_length);
}

static bool sparse_read(struct aurora_block_device *device, uint64_t lba,
                        uint32_t count, void *buffer) {
    uint64_t offset;
    uint64_t length;
    if (buffer == NULL || !transfer_geometry(device, lba, count, &offset, &length) ||
        length != AURORA_FS_V2_FS_BLOCK_SIZE ||
        offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u) {
        return false;
    }
    uint64_t fs_block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint8_t *out = buffer;
    zero_bytes(out, AURORA_FS_V2_FS_BLOCK_SIZE);

    if (fs_block == F1C_BITMAP_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            out[i] = test_ctx.bitmap[i];
        }
        return true;
    }
    if (fs_block == F1C_INODE_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            out[i] = test_ctx.inode_block[i];
        }
        return true;
    }

    int slot = slot_for(fs_block, false);
    if (slot >= 0) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            out[i] = test_ctx.slot_data[(uint32_t)slot][i];
        }
        return true;
    }

    struct f1c_node generated;
    bool generated_node = true;
    if (fs_block == F1C_OLD_LEVEL2) {
        seed_old_root(&generated);
    } else if (fs_block == F1C_OLD_LEVEL1) {
        seed_old_level1(&generated);
    } else if (fs_block >= F1C_FIRST_LEAF &&
               fs_block < F1C_FIRST_LEAF + F1C_CAPACITY) {
        seed_old_leaf(fs_block, &generated);
    } else {
        generated_node = false;
    }
    if (generated_node) {
        const uint8_t *src = (const uint8_t *)&generated;
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            out[i] = src[i];
        }
    }
    return true;
}

static bool sparse_write(struct aurora_block_device *device, uint64_t lba,
                         uint32_t count, const void *buffer) {
    uint64_t offset;
    uint64_t length;
    if (buffer == NULL || device == NULL || device->read_only ||
        !transfer_geometry(device, lba, count, &offset, &length) ||
        length != AURORA_FS_V2_FS_BLOCK_SIZE ||
        offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u) {
        return false;
    }
    uint64_t fs_block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    const uint8_t *src = buffer;

    if (fs_block == F1C_BITMAP_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            test_ctx.bitmap[i] = src[i];
        }
        return true;
    }
    if (fs_block == F1C_INODE_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            test_ctx.inode_block[i] = src[i];
        }
        return true;
    }

    int slot = slot_for(fs_block, true);
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

static void bitmap_set(uint64_t block) {
    test_ctx.bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
}

static void seed_inode(void) {
    zero_bytes(test_ctx.inode_block, sizeof(test_ctx.inode_block));
    struct f1c_inode *inodes = (struct f1c_inode *)test_ctx.inode_block;
    struct f1c_inode *inode = &inodes[F1C_INODE_INDEX];
    inode->object_id = 2u;
    inode->parent_object_id = 1u;
    inode->size = (uint64_t)F1C_FULL_LEVEL1_EXTENTS * AURORA_FS_V2_FS_BLOCK_SIZE;
    inode->allocated_bytes = inode->size;
    inode->generation = 20u;
    inode->extent_tree_root = F1C_OLD_LEVEL2;
    inode->type = F1C_INODE_FILE;
    inode->extent_count = F1C_FULL_LEVEL1_EXTENTS;
}

static bool read_inode(struct f1c_inode *out_inode) {
    if (out_inode == NULL || F1C_INODE_INDEX >= F1C_INODES_PER_BLOCK) {
        return false;
    }
    *out_inode = ((const struct f1c_inode *)test_ctx.inode_block)[F1C_INODE_INDEX];
    return true;
}

bool aurora_fs_v2_inode_append_level2_full_level1_cow_commit(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t expected_old_root,
    const struct aurora_fs_v2_extent *extent
) {
    if (allocator == NULL || geometry == NULL || extent == NULL) {
        return false;
    }
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_append_level2_full_level1_cow(
            allocator, expected_old_root, extent, &new_root)) {
        return false;
    }
    return aurora_fs_v2_inode_publish_extent_root_cow(
        allocator, geometry, inode_index, expected_old_root, new_root, extent);
}

static bool run_test(uint32_t block_size) {
    zero_bytes(&test_ctx, sizeof(test_ctx));
    for (uint32_t i = 0u; i < F1C_SLOT_COUNT; ++i) {
        test_ctx.slot_block[i] = UINT64_MAX;
    }

    for (uint64_t block = 0u; block < F1C_DATA_START; ++block) {
        bitmap_set(block);
    }
    bitmap_set(F1C_OLD_LEVEL1);
    bitmap_set(F1C_OLD_LEVEL2);
    for (uint64_t child = 0u; child < F1C_CAPACITY; ++child) {
        bitmap_set(F1C_FIRST_LEAF + child);
    }
    for (uint64_t logical = 0u; logical <= F1C_FULL_LEVEL1_EXTENTS; ++logical) {
        bitmap_set(F1C_FIRST_DATA + logical);
    }
    seed_inode();

    uint64_t bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        (uint64_t)F1C_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct aurora_block_device device = {
        .name = "aurorafs-v2-level2-full-level1-commit-test",
        .block_size = block_size,
        .block_count = bytes / block_size,
        .read_only = false,
        .context = &test_ctx,
        .read_blocks = sparse_read,
        .write_blocks = sparse_write,
        .flush = sparse_flush
    };
    struct aurora_fs_v2_format_geometry geometry = {
        .base_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES,
        .total_fs_blocks = F1C_TOTAL_BLOCKS,
        .bitmap_start = F1C_BITMAP_START,
        .bitmap_blocks = 1u,
        .inode_start = F1C_INODE_START,
        .inode_blocks = F1C_INODE_BLOCKS,
        .data_start = F1C_DATA_START
    };
    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) {
        return false;
    }

    struct aurora_fs_v2_extent next = {
        .logical_block = F1C_FULL_LEVEL1_EXTENTS,
        .physical_block = F1C_NEW_DATA,
        .block_count = 1u
    };
    if (!aurora_fs_v2_inode_append_level2_full_level1_cow_commit(
            &allocator, &geometry, F1C_INODE_INDEX, F1C_OLD_LEVEL2, &next)) {
        return false;
    }

    struct aurora_fs_v2_allocator reopened;
    struct f1c_inode persisted;
    uint64_t expected_size =
        (uint64_t)(F1C_FULL_LEVEL1_EXTENTS + 1u) * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start) ||
        !read_inode(&persisted) ||
        persisted.extent_tree_root == F1C_OLD_LEVEL2 ||
        persisted.extent_count != F1C_FULL_LEVEL1_EXTENTS + 1u ||
        persisted.size != expected_size ||
        persisted.allocated_bytes != expected_size ||
        persisted.generation != 21u) {
        return false;
    }

    uint64_t physical = 0u;
    uint64_t contiguous = 0u;
    if (!aurora_fs_v2_inode_extent_lookup_unified(
            &reopened, &geometry, F1C_INODE_INDEX, F1C_FULL_LEVEL1_EXTENTS,
            &physical, &contiguous) ||
        physical != F1C_NEW_DATA || contiguous != 1u) {
        return false;
    }

    physical = 0u;
    contiguous = 0u;
    return aurora_fs_v2_inode_extent_lookup_unified(
               &reopened, &geometry, F1C_INODE_INDEX, 0u,
               &physical, &contiguous) &&
        physical == F1C_FIRST_DATA && contiguous == 1u;
}

bool aurora_fs_v2_level2_full_level1_commit_self_test(void) {
    return run_test(512u) && run_test(4096u);
}
