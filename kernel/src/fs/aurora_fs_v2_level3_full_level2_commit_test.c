#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define L3F2C_HEADER_SIZE 64u
#define L3F2C_ENTRY_SIZE 32u
#define L3F2C_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - L3F2C_HEADER_SIZE) / L3F2C_ENTRY_SIZE)
#define L3F2C_LEVEL_LEAF 0u
#define L3F2C_LEVEL_ONE 1u
#define L3F2C_LEVEL_TWO 2u
#define L3F2C_LEVEL_THREE 3u
#define L3F2C_INODE_SIZE 256u
#define L3F2C_INODE_FILE 1u
#define L3F2C_PREFIX_EXTENTS 1ull
#define L3F2C_FULL_LEVEL1_EXTENTS ((uint64_t)L3F2C_CAPACITY * (uint64_t)L3F2C_CAPACITY)
#define L3F2C_FULL_LEVEL2_EXTENTS ((uint64_t)L3F2C_CAPACITY * L3F2C_FULL_LEVEL1_EXTENTS)
#define L3F2C_EXISTING_EXTENTS (L3F2C_PREFIX_EXTENTS + L3F2C_FULL_LEVEL2_EXTENTS)
#define L3F2C_TOTAL_BLOCKS 32768u
#define L3F2C_BITMAP_START 1u
#define L3F2C_INODE_START 2u
#define L3F2C_INODE_BLOCKS 1u
#define L3F2C_DATA_START 16u
#define L3F2C_OLD_LAST_LEAF 20u
#define L3F2C_OLD_LAST_LEVEL1 21u
#define L3F2C_OLD_FULL_LEVEL2 22u
#define L3F2C_OLD_LEVEL3 23u
#define L3F2C_PREFIX_BRANCH 24u
#define L3F2C_DUMMY_LEVEL1_BASE 256u
#define L3F2C_DUMMY_LEAF_BASE 512u
#define L3F2C_LAST_LEAF_DATA 1024u
#define L3F2C_NEW_DATA 4096u
#define L3F2C_INODE_INDEX 1u
#define L3F2C_SLOT_COUNT 24u

struct l3f2c_header {
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

struct l3f2c_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct l3f2c_node {
    struct l3f2c_header header;
    struct l3f2c_entry entries[L3F2C_CAPACITY];
} __attribute__((packed));

struct l3f2c_inode_extent {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct l3f2c_inode {
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
    struct l3f2c_inode_extent extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct l3f2c_test_context {
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint8_t inode_block[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint64_t slot_block[L3F2C_SLOT_COUNT];
    uint8_t slot_data[L3F2C_SLOT_COUNT][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static struct l3f2c_test_context l3f2c_test;
static struct l3f2c_node l3f2c_seed;
static uint8_t l3f2c_old_root_before[AURORA_FS_V2_FS_BLOCK_SIZE];

bool aurora_fs_v2_inode_append_level3_full_level2_cow_commit(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t expected_old_root,
    const struct aurora_fs_v2_extent *extent);

_Static_assert(sizeof(struct l3f2c_header) == L3F2C_HEADER_SIZE,
               "AuroraFS v2 level-3 full-level2 commit header size");
_Static_assert(sizeof(struct l3f2c_entry) == L3F2C_ENTRY_SIZE,
               "AuroraFS v2 level-3 full-level2 commit entry size");
_Static_assert(sizeof(struct l3f2c_node) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 level-3 full-level2 commit node size");
_Static_assert(sizeof(struct l3f2c_inode) == L3F2C_INODE_SIZE,
               "AuroraFS v2 level-3 full-level2 commit inode size");
_Static_assert(L3F2C_CAPACITY == 126u,
               "AuroraFS v2 level-3 full-level2 commit assumes 126 entries");
_Static_assert(L3F2C_FULL_LEVEL2_EXTENTS == 2000376ull,
               "AuroraFS v2 level-3 full-level2 commit boundary changed");
_Static_assert(L3F2C_EXISTING_EXTENTS < UINT32_MAX,
               "AuroraFS v2 extent_count must represent full-level2 gate");

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

static uint32_t node_checksum(struct l3f2c_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t result = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return result;
}

static void init_node(struct l3f2c_node *node, uint16_t level, uint16_t count,
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

static int slot_for(uint64_t block, bool create) {
    for (uint32_t i = 0u; i < L3F2C_SLOT_COUNT; ++i)
        if (l3f2c_test.slot_block[i] == block) return (int)i;
    if (!create) return -1;
    for (uint32_t i = 0u; i < L3F2C_SLOT_COUNT; ++i) {
        if (l3f2c_test.slot_block[i] == UINT64_MAX) {
            l3f2c_test.slot_block[i] = block;
            zero_bytes(l3f2c_test.slot_data[i], AURORA_FS_V2_FS_BLOCK_SIZE);
            return (int)i;
        }
    }
    return -1;
}

static void bitmap_set(uint64_t block) {
    l3f2c_test.bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
}

static bool store_node(uint64_t block, struct l3f2c_node *node) {
    int slot = slot_for(block, true);
    if (slot < 0 || node == NULL) return false;
    node->header.checksum = node_checksum(node);
    const uint8_t *src = (const uint8_t *)node;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        l3f2c_test.slot_data[(uint32_t)slot][i] = src[i];
    return true;
}

static bool seed_tree(void) {
    uint64_t last_leaf_first = L3F2C_EXISTING_EXTENTS - L3F2C_CAPACITY;
    init_node(&l3f2c_seed, L3F2C_LEVEL_LEAF, L3F2C_CAPACITY, 200u,
              last_leaf_first, L3F2C_EXISTING_EXTENTS);
    for (uint32_t i = 0u; i < L3F2C_CAPACITY; ++i) {
        l3f2c_seed.entries[i] = (struct l3f2c_entry){
            last_leaf_first + i, L3F2C_LAST_LEAF_DATA + i, 1u, 0u
        };
    }
    if (!store_node(L3F2C_OLD_LAST_LEAF, &l3f2c_seed)) return false;

    uint64_t last_level1_first = L3F2C_EXISTING_EXTENTS - L3F2C_FULL_LEVEL1_EXTENTS;
    init_node(&l3f2c_seed, L3F2C_LEVEL_ONE, L3F2C_CAPACITY, 200u,
              last_level1_first, L3F2C_EXISTING_EXTENTS);
    for (uint32_t i = 0u; i < L3F2C_CAPACITY; ++i) {
        uint64_t first = last_level1_first + (uint64_t)i * L3F2C_CAPACITY;
        uint64_t child = i + 1u == L3F2C_CAPACITY ?
            L3F2C_OLD_LAST_LEAF : L3F2C_DUMMY_LEAF_BASE + i;
        l3f2c_seed.entries[i] = (struct l3f2c_entry){
            first, child, L3F2C_CAPACITY, 0u
        };
    }
    if (!store_node(L3F2C_OLD_LAST_LEVEL1, &l3f2c_seed)) return false;

    init_node(&l3f2c_seed, L3F2C_LEVEL_TWO, L3F2C_CAPACITY, 200u,
              L3F2C_PREFIX_EXTENTS, L3F2C_EXISTING_EXTENTS);
    for (uint32_t i = 0u; i < L3F2C_CAPACITY; ++i) {
        uint64_t first = L3F2C_PREFIX_EXTENTS +
            (uint64_t)i * L3F2C_FULL_LEVEL1_EXTENTS;
        uint64_t child = i + 1u == L3F2C_CAPACITY ?
            L3F2C_OLD_LAST_LEVEL1 : L3F2C_DUMMY_LEVEL1_BASE + i;
        l3f2c_seed.entries[i] = (struct l3f2c_entry){
            first, child, L3F2C_FULL_LEVEL1_EXTENTS, 0u
        };
    }
    if (!store_node(L3F2C_OLD_FULL_LEVEL2, &l3f2c_seed)) return false;

    init_node(&l3f2c_seed, L3F2C_LEVEL_THREE, 2u, 200u,
              0u, L3F2C_EXISTING_EXTENTS);
    l3f2c_seed.entries[0] = (struct l3f2c_entry){
        0u, L3F2C_PREFIX_BRANCH, L3F2C_PREFIX_EXTENTS, 0u
    };
    l3f2c_seed.entries[1] = (struct l3f2c_entry){
        L3F2C_PREFIX_EXTENTS, L3F2C_OLD_FULL_LEVEL2,
        L3F2C_FULL_LEVEL2_EXTENTS, 0u
    };
    return store_node(L3F2C_OLD_LEVEL3, &l3f2c_seed);
}

static void seed_inode(void) {
    zero_bytes(l3f2c_test.inode_block, sizeof(l3f2c_test.inode_block));
    struct l3f2c_inode *inode =
        &((struct l3f2c_inode *)l3f2c_test.inode_block)[L3F2C_INODE_INDEX];
    inode->object_id = 2u;
    inode->parent_object_id = 1u;
    inode->size = L3F2C_EXISTING_EXTENTS * (uint64_t)AURORA_FS_V2_FS_BLOCK_SIZE;
    inode->allocated_bytes = inode->size;
    inode->generation = 200u;
    inode->extent_tree_root = L3F2C_OLD_LEVEL3;
    inode->type = L3F2C_INODE_FILE;
    inode->extent_count = (uint32_t)L3F2C_EXISTING_EXTENTS;
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
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u)
        return false;
    uint64_t block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint8_t *out = buffer;
    zero_bytes(out, AURORA_FS_V2_FS_BLOCK_SIZE);
    if (block == L3F2C_BITMAP_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
            out[i] = l3f2c_test.bitmap[i];
        return true;
    }
    if (block == L3F2C_INODE_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
            out[i] = l3f2c_test.inode_block[i];
        return true;
    }
    int slot = slot_for(block, false);
    if (slot < 0) return true;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        out[i] = l3f2c_test.slot_data[(uint32_t)slot][i];
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
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u)
        return false;
    uint64_t block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    const uint8_t *src = buffer;
    if (block == L3F2C_BITMAP_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
            l3f2c_test.bitmap[i] = src[i];
        return true;
    }
    if (block == L3F2C_INODE_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
            l3f2c_test.inode_block[i] = src[i];
        return true;
    }
    int slot = slot_for(block, true);
    if (slot < 0) return false;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        l3f2c_test.slot_data[(uint32_t)slot][i] = src[i];
    return true;
}

static bool sparse_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool read_inode(struct l3f2c_inode *out_inode) {
    if (out_inode == NULL) return false;
    *out_inode =
        ((const struct l3f2c_inode *)l3f2c_test.inode_block)[L3F2C_INODE_INDEX];
    return true;
}

static bool run_test(uint32_t block_size) {
    zero_bytes(&l3f2c_test, sizeof(l3f2c_test));
    for (uint32_t i = 0u; i < L3F2C_SLOT_COUNT; ++i)
        l3f2c_test.slot_block[i] = UINT64_MAX;

    for (uint64_t block = 0u; block < L3F2C_DATA_START; ++block) bitmap_set(block);
    bitmap_set(L3F2C_OLD_LAST_LEAF);
    bitmap_set(L3F2C_OLD_LAST_LEVEL1);
    bitmap_set(L3F2C_OLD_FULL_LEVEL2);
    bitmap_set(L3F2C_OLD_LEVEL3);
    bitmap_set(L3F2C_PREFIX_BRANCH);
    for (uint64_t block = L3F2C_LAST_LEAF_DATA;
         block < L3F2C_LAST_LEAF_DATA + L3F2C_CAPACITY; ++block) bitmap_set(block);
    bitmap_set(L3F2C_NEW_DATA);

    if (!seed_tree()) return false;
    seed_inode();

    int old_root_slot = slot_for(L3F2C_OLD_LEVEL3, false);
    if (old_root_slot < 0) return false;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        l3f2c_old_root_before[i] =
            l3f2c_test.slot_data[(uint32_t)old_root_slot][i];

    uint64_t bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        (uint64_t)L3F2C_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct aurora_block_device device = {
        .name = "aurorafs-v2-level3-full-level2-commit-test",
        .block_size = block_size,
        .block_count = bytes / block_size,
        .read_only = false,
        .context = &l3f2c_test,
        .read_blocks = sparse_read,
        .write_blocks = sparse_write,
        .flush = sparse_flush
    };
    struct aurora_fs_v2_format_geometry geometry = {
        .base_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES,
        .total_fs_blocks = L3F2C_TOTAL_BLOCKS,
        .bitmap_start = L3F2C_BITMAP_START,
        .bitmap_blocks = 1u,
        .inode_start = L3F2C_INODE_START,
        .inode_blocks = L3F2C_INODE_BLOCKS,
        .data_start = L3F2C_DATA_START
    };
    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start))
        return false;

    struct aurora_fs_v2_extent next = {
        .logical_block = L3F2C_EXISTING_EXTENTS,
        .physical_block = L3F2C_NEW_DATA,
        .block_count = 1u
    };
    if (!aurora_fs_v2_inode_append_level3_full_level2_cow_commit(
            &allocator, &geometry, L3F2C_INODE_INDEX, L3F2C_OLD_LEVEL3, &next))
        return false;

    struct aurora_fs_v2_allocator reopened;
    struct l3f2c_inode persisted;
    uint64_t expected_extents = L3F2C_EXISTING_EXTENTS + 1u;
    uint64_t expected_bytes =
        expected_extents * (uint64_t)AURORA_FS_V2_FS_BLOCK_SIZE;

    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start) ||
        !read_inode(&persisted) ||
        persisted.extent_tree_root == L3F2C_OLD_LEVEL3 ||
        persisted.extent_count != (uint32_t)expected_extents ||
        persisted.size != expected_bytes ||
        persisted.allocated_bytes != expected_bytes ||
        persisted.generation != 201u)
        return false;

    old_root_slot = slot_for(L3F2C_OLD_LEVEL3, false);
    if (old_root_slot < 0) return false;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        if (l3f2c_old_root_before[i] !=
            l3f2c_test.slot_data[(uint32_t)old_root_slot][i])
            return false;

    uint64_t physical = 0u;
    uint64_t contiguous = 0u;
    if (!aurora_fs_v2_inode_extent_lookup_unified(
            &reopened, &geometry, L3F2C_INODE_INDEX,
            L3F2C_EXISTING_EXTENTS, &physical, &contiguous) ||
        physical != L3F2C_NEW_DATA || contiguous != 1u)
        return false;

    physical = 0u;
    contiguous = 0u;
    return aurora_fs_v2_inode_extent_lookup_unified(
               &reopened, &geometry, L3F2C_INODE_INDEX,
               L3F2C_EXISTING_EXTENTS - 1u, &physical, &contiguous) &&
        physical == L3F2C_LAST_LEAF_DATA + L3F2C_CAPACITY - 1u &&
        contiguous == 1u;
}

bool aurora_fs_v2_level3_full_level2_commit_self_test(void) {
    return run_test(512u) && run_test(4096u);
}
