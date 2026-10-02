#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_inode_publish.h>
#include <aurora/block_device.h>

#define L3C_HEADER_SIZE 64u
#define L3C_ENTRY_SIZE 32u
#define L3C_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - L3C_HEADER_SIZE) / L3C_ENTRY_SIZE)
#define L3C_LEVEL_LEAF 0u
#define L3C_LEVEL_ONE 1u
#define L3C_LEVEL_TWO 2u
#define L3C_INODE_SIZE 256u
#define L3C_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / L3C_INODE_SIZE)
#define L3C_INODE_FILE 1u
#define L3C_FULL_LEVEL1_EXTENTS ((uint64_t)L3C_CAPACITY * (uint64_t)L3C_CAPACITY)
#define L3C_FULL_LEVEL2_EXTENTS ((uint64_t)L3C_CAPACITY * L3C_FULL_LEVEL1_EXTENTS)
#define L3C_TOTAL_BLOCKS 32768u
#define L3C_BITMAP_START 1u
#define L3C_INODE_START 2u
#define L3C_INODE_BLOCKS 1u
#define L3C_DATA_START 16u
#define L3C_OLD_LEVEL2 20u
#define L3C_LAST_LEVEL1 21u
#define L3C_LAST_LEAF 22u
#define L3C_NEW_DATA 4096u
#define L3C_OLD_DATA_BASE 8192u
#define L3C_SLOT_COUNT 12u
#define L3C_INODE_INDEX 1u

struct l3c_header {
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

struct l3c_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct l3c_node {
    struct l3c_header header;
    struct l3c_entry entries[L3C_CAPACITY];
} __attribute__((packed));

struct l3c_inode_extent {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct l3c_inode {
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
    struct l3c_inode_extent extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct l3c_test_context {
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint8_t inode_block[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint64_t slot_block[L3C_SLOT_COUNT];
    uint8_t slot_data[L3C_SLOT_COUNT][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static struct l3c_test_context l3c_test;

_Static_assert(sizeof(struct l3c_header) == L3C_HEADER_SIZE,
               "AuroraFS v2 level-3 commit header size");
_Static_assert(sizeof(struct l3c_entry) == L3C_ENTRY_SIZE,
               "AuroraFS v2 level-3 commit entry size");
_Static_assert(sizeof(struct l3c_node) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 level-3 commit node size");
_Static_assert(sizeof(struct l3c_inode) == L3C_INODE_SIZE,
               "AuroraFS v2 level-3 commit inode size");
_Static_assert(L3C_CAPACITY == 126u,
               "AuroraFS v2 level-3 commit assumes 126 entries");
_Static_assert(L3C_FULL_LEVEL2_EXTENTS == 2000376u,
               "AuroraFS v2 level-3 commit boundary changed");
_Static_assert(L3C_FULL_LEVEL2_EXTENTS < UINT32_MAX,
               "AuroraFS v2 extent_count cannot represent level-3 boundary");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static bool mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || (a != 0u && b > UINT64_MAX / a)) return false;
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

static uint32_t node_checksum(struct l3c_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t result = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return result;
}

static void init_node(struct l3c_node *node, uint16_t level, uint16_t count,
                      uint64_t generation, uint64_t first, uint64_t last) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    zero_bytes(node, sizeof(*node));
    for (size_t i = 0u; i < sizeof(magic); ++i) node->header.magic[i] = magic[i];
    node->header.version = 1u;
    node->header.level = level;
    node->header.entry_count = count;
    node->header.generation = generation;
    node->header.first_logical = first;
    node->header.last_logical_exclusive = last;
}

static void seed_old_root(struct l3c_node *node) {
    init_node(node, L3C_LEVEL_TWO, L3C_CAPACITY, 80u,
              0u, L3C_FULL_LEVEL2_EXTENTS);
    for (uint16_t child = 0u; child < L3C_CAPACITY; ++child) {
        uint64_t first = (uint64_t)child * L3C_FULL_LEVEL1_EXTENTS;
        node->entries[child] = (struct l3c_entry){
            first,
            child == L3C_CAPACITY - 1u ? L3C_LAST_LEVEL1 : 100u + child,
            L3C_FULL_LEVEL1_EXTENTS,
            0u
        };
    }
    node->header.checksum = node_checksum(node);
}

static void seed_last_level1(struct l3c_node *node) {
    uint64_t first_level1 = L3C_FULL_LEVEL2_EXTENTS - L3C_FULL_LEVEL1_EXTENTS;
    init_node(node, L3C_LEVEL_ONE, L3C_CAPACITY, 80u,
              first_level1, L3C_FULL_LEVEL2_EXTENTS);
    for (uint16_t child = 0u; child < L3C_CAPACITY; ++child) {
        uint64_t first = first_level1 + (uint64_t)child * L3C_CAPACITY;
        node->entries[child] = (struct l3c_entry){
            first,
            child == L3C_CAPACITY - 1u ? L3C_LAST_LEAF : 300u + child,
            L3C_CAPACITY,
            0u
        };
    }
    node->header.checksum = node_checksum(node);
}

static void seed_last_leaf(struct l3c_node *node) {
    uint64_t first = L3C_FULL_LEVEL2_EXTENTS - L3C_CAPACITY;
    init_node(node, L3C_LEVEL_LEAF, L3C_CAPACITY, 80u,
              first, L3C_FULL_LEVEL2_EXTENTS);
    for (uint16_t entry = 0u; entry < L3C_CAPACITY; ++entry) {
        uint64_t logical = first + entry;
        node->entries[entry] = (struct l3c_entry){
            logical, L3C_OLD_DATA_BASE + entry, 1u, 0u
        };
    }
    node->header.checksum = node_checksum(node);
}

static int slot_for(uint64_t block, bool create) {
    for (uint32_t i = 0u; i < L3C_SLOT_COUNT; ++i)
        if (l3c_test.slot_block[i] == block) return (int)i;
    if (!create) return -1;
    for (uint32_t i = 0u; i < L3C_SLOT_COUNT; ++i) {
        if (l3c_test.slot_block[i] == UINT64_MAX) {
            l3c_test.slot_block[i] = block;
            zero_bytes(l3c_test.slot_data[i], AURORA_FS_V2_FS_BLOCK_SIZE);
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

    if (fs_block == L3C_BITMAP_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) out[i] = l3c_test.bitmap[i];
        return true;
    }
    if (fs_block == L3C_INODE_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) out[i] = l3c_test.inode_block[i];
        return true;
    }

    int slot = slot_for(fs_block, false);
    if (slot >= 0) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
            out[i] = l3c_test.slot_data[(uint32_t)slot][i];
        return true;
    }

    struct l3c_node generated;
    if (fs_block == L3C_OLD_LEVEL2) {
        seed_old_root(&generated);
    } else if (fs_block == L3C_LAST_LEVEL1) {
        seed_last_level1(&generated);
    } else if (fs_block == L3C_LAST_LEAF) {
        seed_last_leaf(&generated);
    } else {
        return true;
    }
    const uint8_t *src = (const uint8_t *)&generated;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) out[i] = src[i];
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

    if (fs_block == L3C_BITMAP_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) l3c_test.bitmap[i] = src[i];
        return true;
    }
    if (fs_block == L3C_INODE_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) l3c_test.inode_block[i] = src[i];
        return true;
    }

    int slot = slot_for(fs_block, true);
    if (slot < 0) return false;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        l3c_test.slot_data[(uint32_t)slot][i] = src[i];
    return true;
}

static bool sparse_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static void bitmap_set(uint64_t block) {
    l3c_test.bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
}

static void seed_inode(void) {
    zero_bytes(l3c_test.inode_block, sizeof(l3c_test.inode_block));
    struct l3c_inode *inode = &((struct l3c_inode *)l3c_test.inode_block)[L3C_INODE_INDEX];
    inode->object_id = 2u;
    inode->parent_object_id = 1u;
    inode->size = L3C_FULL_LEVEL2_EXTENTS * (uint64_t)AURORA_FS_V2_FS_BLOCK_SIZE;
    inode->allocated_bytes = inode->size;
    inode->generation = 80u;
    inode->extent_tree_root = L3C_OLD_LEVEL2;
    inode->type = L3C_INODE_FILE;
    inode->extent_count = (uint32_t)L3C_FULL_LEVEL2_EXTENTS;
}

static bool read_inode(struct l3c_inode *out_inode) {
    if (out_inode == NULL || L3C_INODE_INDEX >= L3C_INODES_PER_BLOCK) return false;
    *out_inode = ((const struct l3c_inode *)l3c_test.inode_block)[L3C_INODE_INDEX];
    return true;
}

bool aurora_fs_v2_inode_append_level3_grow_cow_commit(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t expected_old_root,
    const struct aurora_fs_v2_extent *extent
) {
    if (allocator == NULL || geometry == NULL || extent == NULL) return false;
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_grow_level2_full_root_cow(
            allocator, expected_old_root, extent, &new_root)) return false;
    return aurora_fs_v2_inode_publish_extent_root_cow(
        allocator, geometry, inode_index, expected_old_root, new_root, extent);
}

static bool run_test(uint32_t block_size) {
    zero_bytes(&l3c_test, sizeof(l3c_test));
    for (uint32_t i = 0u; i < L3C_SLOT_COUNT; ++i) l3c_test.slot_block[i] = UINT64_MAX;

    for (uint64_t block = 0u; block < L3C_DATA_START; ++block) bitmap_set(block);
    bitmap_set(L3C_OLD_LEVEL2);
    bitmap_set(L3C_LAST_LEVEL1);
    bitmap_set(L3C_LAST_LEAF);
    bitmap_set(L3C_NEW_DATA);
    seed_inode();

    uint64_t bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        (uint64_t)L3C_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct aurora_block_device device = {
        .name = "aurorafs-v2-level3-commit-test",
        .block_size = block_size,
        .block_count = bytes / block_size,
        .read_only = false,
        .context = &l3c_test,
        .read_blocks = sparse_read,
        .write_blocks = sparse_write,
        .flush = sparse_flush
    };
    struct aurora_fs_v2_format_geometry geometry = {
        .base_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES,
        .total_fs_blocks = L3C_TOTAL_BLOCKS,
        .bitmap_start = L3C_BITMAP_START,
        .bitmap_blocks = 1u,
        .inode_start = L3C_INODE_START,
        .inode_blocks = L3C_INODE_BLOCKS,
        .data_start = L3C_DATA_START
    };
    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) return false;

    struct l3c_node old_root;
    seed_old_root(&old_root);
    uint8_t old_root_before[AURORA_FS_V2_FS_BLOCK_SIZE];
    const uint8_t *old_root_bytes = (const uint8_t *)&old_root;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        old_root_before[i] = old_root_bytes[i];

    struct aurora_fs_v2_extent next = {
        .logical_block = L3C_FULL_LEVEL2_EXTENTS,
        .physical_block = L3C_NEW_DATA,
        .block_count = 1u
    };
    if (!aurora_fs_v2_inode_append_level3_grow_cow_commit(
            &allocator, &geometry, L3C_INODE_INDEX, L3C_OLD_LEVEL2, &next)) return false;

    struct aurora_fs_v2_allocator reopened;
    struct l3c_inode persisted;
    uint64_t expected_size =
        (L3C_FULL_LEVEL2_EXTENTS + 1u) * (uint64_t)AURORA_FS_V2_FS_BLOCK_SIZE;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start) ||
        !read_inode(&persisted) ||
        persisted.extent_tree_root == L3C_OLD_LEVEL2 ||
        persisted.extent_count != (uint32_t)(L3C_FULL_LEVEL2_EXTENTS + 1u) ||
        persisted.size != expected_size || persisted.allocated_bytes != expected_size ||
        persisted.generation != 81u) return false;

    struct l3c_node old_root_after;
    seed_old_root(&old_root_after);
    const uint8_t *after_bytes = (const uint8_t *)&old_root_after;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        if (old_root_before[i] != after_bytes[i]) return false;

    uint64_t physical = 0u;
    uint64_t contiguous = 0u;
    if (!aurora_fs_v2_inode_extent_lookup_unified(
            &reopened, &geometry, L3C_INODE_INDEX, L3C_FULL_LEVEL2_EXTENTS,
            &physical, &contiguous) ||
        physical != L3C_NEW_DATA || contiguous != 1u) return false;

    physical = 0u;
    contiguous = 0u;
    uint64_t old_last_logical = L3C_FULL_LEVEL2_EXTENTS - 1u;
    return aurora_fs_v2_inode_extent_lookup_unified(
               &reopened, &geometry, L3C_INODE_INDEX, old_last_logical,
               &physical, &contiguous) &&
        physical == L3C_OLD_DATA_BASE + (L3C_CAPACITY - 1u) && contiguous == 1u;
}

bool aurora_fs_v2_level3_commit_self_test(void) {
    return run_test(512u) && run_test(4096u);
}
