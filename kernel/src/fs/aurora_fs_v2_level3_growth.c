#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define L3_HEADER_SIZE 64u
#define L3_ENTRY_SIZE 32u
#define L3_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - L3_HEADER_SIZE) / L3_ENTRY_SIZE)
#define L3_LEVEL_LEAF 0u
#define L3_LEVEL_ONE 1u
#define L3_LEVEL_TWO 2u
#define L3_LEVEL_THREE 3u
#define L3_FULL_LEVEL1_EXTENTS ((uint64_t)L3_CAPACITY * (uint64_t)L3_CAPACITY)
#define L3_FULL_LEVEL2_EXTENTS ((uint64_t)L3_CAPACITY * L3_FULL_LEVEL1_EXTENTS)
#define L3_TEST_TOTAL_BLOCKS 32768u
#define L3_TEST_BITMAP_START 1u
#define L3_TEST_BITMAP_BLOCKS 1u
#define L3_TEST_DATA_START 16u
#define L3_TEST_OLD_ROOT 20u
#define L3_TEST_LAST_LEVEL1 21u
#define L3_TEST_LAST_LEAF 22u
#define L3_TEST_NEW_DATA 4096u
#define L3_TEST_SLOT_COUNT 10u

struct l3_header {
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

struct l3_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct l3_node {
    struct l3_header header;
    struct l3_entry entries[L3_CAPACITY];
} __attribute__((packed));

struct l3_test_context {
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint64_t slot_block[L3_TEST_SLOT_COUNT];
    uint8_t slot_data[L3_TEST_SLOT_COUNT][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static struct l3_node l3_root_io;
static struct l3_node l3_level1_io;
static struct l3_node l3_leaf_io;
static struct l3_test_context l3_test_ctx;

_Static_assert(sizeof(struct l3_header) == L3_HEADER_SIZE, "AuroraFS v2 level-3 header size");
_Static_assert(sizeof(struct l3_entry) == L3_ENTRY_SIZE, "AuroraFS v2 level-3 entry size");
_Static_assert(sizeof(struct l3_node) == AURORA_FS_V2_FS_BLOCK_SIZE, "AuroraFS v2 level-3 node size");
_Static_assert(L3_CAPACITY == 126u, "AuroraFS v2 level-3 gate assumes 126 entries");
_Static_assert(L3_FULL_LEVEL2_EXTENTS == 2000376u, "AuroraFS v2 level-3 boundary changed");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static bool add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || b > UINT64_MAX - a) return false;
    *out = a + b;
    return true;
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

static uint32_t node_checksum(struct l3_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t result = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return result;
}

static void set_magic(struct l3_node *node) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0u; i < sizeof(magic); ++i) node->header.magic[i] = magic[i];
}

static bool magic_valid(const struct l3_node *node) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0u; i < sizeof(magic); ++i) if (node->header.magic[i] != magic[i]) return false;
    return true;
}

static void init_node(struct l3_node *node, uint16_t level, uint16_t count,
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

static bool fs_geometry(const struct aurora_fs_v2_allocator *allocator,
                        uint64_t fs_block, uint64_t *out_lba, uint32_t *out_count) {
    if (allocator == NULL || allocator->device == NULL || out_lba == NULL || out_count == NULL ||
        fs_block >= allocator->total_fs_blocks || allocator->device->block_size == 0u ||
        allocator->device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        (AURORA_FS_V2_FS_BLOCK_SIZE % allocator->device->block_size) != 0u ||
        (allocator->base_bytes % allocator->device->block_size) != 0u) return false;
    uint64_t fs_offset;
    uint64_t byte_offset;
    if (!mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_offset) ||
        !add_u64(allocator->base_bytes, fs_offset, &byte_offset)) return false;
    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / allocator->device->block_size;
    uint64_t lba = byte_offset / allocator->device->block_size;
    if (count == 0u || count > UINT32_MAX || lba >= allocator->device->block_count ||
        count > allocator->device->block_count - lba) return false;
    *out_lba = lba;
    *out_count = (uint32_t)count;
    return true;
}

static bool read_node(struct aurora_fs_v2_allocator *allocator, uint64_t block, struct l3_node *node) {
    uint64_t lba;
    uint32_t count;
    if (node == NULL || !fs_geometry(allocator, block, &lba, &count) ||
        !block_device_read(allocator->device, lba, count, node) || !magic_valid(node) ||
        node->header.version != 1u || node->header.entry_count == 0u ||
        node->header.entry_count > L3_CAPACITY ||
        node->header.last_logical_exclusive <= node->header.first_logical) return false;
    return node->header.checksum == node_checksum(node);
}

static bool write_node(struct aurora_fs_v2_allocator *allocator, uint64_t block, struct l3_node *node) {
    uint64_t lba;
    uint32_t count;
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        node == NULL || !fs_geometry(allocator, block, &lba, &count)) return false;
    node->header.checksum = node_checksum(node);
    return block_device_write(allocator->device, lba, count, node);
}

static void release_block(struct aurora_fs_v2_allocator *allocator, uint64_t block) {
    if (block != 0u) (void)aurora_fs_v2_allocator_free_range(allocator, block, 1u);
}

bool aurora_fs_v2_extent_tree_grow_level2_full_root_cow(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root_block,
    const struct aurora_fs_v2_extent *extent,
    uint64_t *out_new_root_block
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        extent == NULL || out_new_root_block == NULL || extent->block_count == 0u ||
        extent->physical_block < allocator->data_start ||
        !read_node(allocator, old_root_block, &l3_root_io) ||
        l3_root_io.header.level != L3_LEVEL_TWO ||
        l3_root_io.header.entry_count != L3_CAPACITY) return false;

    uint16_t root_last = L3_CAPACITY - 1u;
    struct l3_entry old_level1_entry = l3_root_io.entries[root_last];
    if (!read_node(allocator, old_level1_entry.physical_or_child, &l3_level1_io) ||
        l3_level1_io.header.level != L3_LEVEL_ONE ||
        l3_level1_io.header.entry_count != L3_CAPACITY ||
        l3_level1_io.header.first_logical != old_level1_entry.logical_block ||
        l3_level1_io.header.last_logical_exclusive != l3_root_io.header.last_logical_exclusive) return false;

    uint16_t level1_last = L3_CAPACITY - 1u;
    struct l3_entry old_leaf_entry = l3_level1_io.entries[level1_last];
    if (!read_node(allocator, old_leaf_entry.physical_or_child, &l3_leaf_io) ||
        l3_leaf_io.header.level != L3_LEVEL_LEAF ||
        l3_leaf_io.header.entry_count != L3_CAPACITY ||
        l3_leaf_io.header.first_logical != old_leaf_entry.logical_block ||
        l3_leaf_io.header.last_logical_exclusive != l3_level1_io.header.last_logical_exclusive) return false;

    uint64_t physical_end;
    uint64_t logical_end;
    if (!add_u64(extent->physical_block, extent->block_count, &physical_end) ||
        physical_end > allocator->total_fs_blocks ||
        !add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        extent->logical_block < l3_root_io.header.last_logical_exclusive) return false;

    uint64_t generation = l3_root_io.header.generation + 1u;
    uint64_t new_leaf = 0u, new_level1 = 0u, new_level2 = 0u, new_level3 = 0u;

    struct l3_node leaf;
    init_node(&leaf, L3_LEVEL_LEAF, 1u, generation, extent->logical_block, logical_end);
    leaf.entries[0] = (struct l3_entry){extent->logical_block, extent->physical_block, extent->block_count, 0u};
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_leaf) ||
        !write_node(allocator, new_leaf, &leaf) || !block_device_flush(allocator->device)) {
        release_block(allocator, new_leaf);
        return false;
    }

    struct l3_node level1;
    init_node(&level1, L3_LEVEL_ONE, 1u, generation, extent->logical_block, logical_end);
    level1.entries[0] = (struct l3_entry){extent->logical_block, new_leaf, extent->block_count, 0u};
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_level1) ||
        !write_node(allocator, new_level1, &level1) || !block_device_flush(allocator->device)) {
        release_block(allocator, new_level1); release_block(allocator, new_leaf); return false;
    }

    struct l3_node level2;
    init_node(&level2, L3_LEVEL_TWO, 1u, generation, extent->logical_block, logical_end);
    level2.entries[0] = (struct l3_entry){extent->logical_block, new_level1, extent->block_count, 0u};
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_level2) ||
        !write_node(allocator, new_level2, &level2) || !block_device_flush(allocator->device)) {
        release_block(allocator, new_level2); release_block(allocator, new_level1); release_block(allocator, new_leaf); return false;
    }

    struct l3_node level3;
    init_node(&level3, L3_LEVEL_THREE, 2u, generation,
              l3_root_io.header.first_logical, logical_end);
    level3.entries[0] = (struct l3_entry){
        l3_root_io.header.first_logical,
        old_root_block,
        l3_root_io.header.last_logical_exclusive - l3_root_io.header.first_logical,
        0u
    };
    level3.entries[1] = (struct l3_entry){
        extent->logical_block,
        new_level2,
        logical_end - extent->logical_block,
        0u
    };
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_level3) ||
        !write_node(allocator, new_level3, &level3) || !block_device_flush(allocator->device)) {
        release_block(allocator, new_level3); release_block(allocator, new_level2);
        release_block(allocator, new_level1); release_block(allocator, new_leaf); return false;
    }

    *out_new_root_block = new_level3;
    return true;
}

static void bitmap_set(uint64_t block) {
    l3_test_ctx.bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
}

static int slot_for(uint64_t block, bool create) {
    for (uint32_t i = 0u; i < L3_TEST_SLOT_COUNT; ++i)
        if (l3_test_ctx.slot_block[i] == block) return (int)i;
    if (!create) return -1;
    for (uint32_t i = 0u; i < L3_TEST_SLOT_COUNT; ++i) {
        if (l3_test_ctx.slot_block[i] == UINT64_MAX) {
            l3_test_ctx.slot_block[i] = block;
            zero_bytes(l3_test_ctx.slot_data[i], AURORA_FS_V2_FS_BLOCK_SIZE);
            return (int)i;
        }
    }
    return -1;
}

static bool transfer_geometry(struct aurora_block_device *device, uint64_t lba, uint32_t count,
                              uint64_t *out_offset, uint64_t *out_length) {
    return device != NULL && out_offset != NULL && out_length != NULL && count != 0u &&
        lba < device->block_count && (uint64_t)count <= device->block_count - lba &&
        mul_u64(lba, device->block_size, out_offset) && mul_u64(count, device->block_size, out_length);
}

static bool sparse_read(struct aurora_block_device *device, uint64_t lba, uint32_t count, void *buffer) {
    uint64_t offset, length;
    if (buffer == NULL || !transfer_geometry(device, lba, count, &offset, &length) ||
        length != AURORA_FS_V2_FS_BLOCK_SIZE) return false;
    uint8_t *out = buffer;
    zero_bytes(out, (size_t)length);
    uint64_t bitmap_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES + L3_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) out[i] = l3_test_ctx.bitmap[i];
        return true;
    }
    if (offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u) return false;
    uint64_t block = (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    int slot = slot_for(block, false);
    if (slot < 0) return false;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) out[i] = l3_test_ctx.slot_data[(uint32_t)slot][i];
    return true;
}

static bool sparse_write(struct aurora_block_device *device, uint64_t lba, uint32_t count, const void *buffer) {
    uint64_t offset, length;
    if (buffer == NULL || device == NULL || device->read_only ||
        !transfer_geometry(device, lba, count, &offset, &length) || length != AURORA_FS_V2_FS_BLOCK_SIZE) return false;
    const uint8_t *src = buffer;
    uint64_t bitmap_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES + L3_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) l3_test_ctx.bitmap[i] = src[i];
        return true;
    }
    if (offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u) return false;
    uint64_t block = (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    int slot = slot_for(block, true);
    if (slot < 0) return false;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) l3_test_ctx.slot_data[(uint32_t)slot][i] = src[i];
    return true;
}

static bool sparse_flush(struct aurora_block_device *device) { return device != NULL; }

static bool store_node(uint64_t block, struct l3_node *node) {
    int slot = slot_for(block, true);
    if (slot < 0) return false;
    node->header.checksum = node_checksum(node);
    const uint8_t *src = (const uint8_t *)node;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        l3_test_ctx.slot_data[(uint32_t)slot][i] = src[i];
    return true;
}

static bool seed_boundary(void) {
    struct l3_node root;
    init_node(&root, L3_LEVEL_TWO, L3_CAPACITY, 80u, 0u, L3_FULL_LEVEL2_EXTENTS);
    for (uint16_t i = 0u; i < L3_CAPACITY; ++i) {
        uint64_t first = (uint64_t)i * L3_FULL_LEVEL1_EXTENTS;
        root.entries[i] = (struct l3_entry){
            first,
            (i == L3_CAPACITY - 1u) ? L3_TEST_LAST_LEVEL1 : (100u + i),
            L3_FULL_LEVEL1_EXTENTS,
            0u
        };
    }
    if (!store_node(L3_TEST_OLD_ROOT, &root)) return false;

    uint64_t last_level1_first = L3_FULL_LEVEL2_EXTENTS - L3_FULL_LEVEL1_EXTENTS;
    struct l3_node level1;
    init_node(&level1, L3_LEVEL_ONE, L3_CAPACITY, 80u,
              last_level1_first, L3_FULL_LEVEL2_EXTENTS);
    for (uint16_t i = 0u; i < L3_CAPACITY; ++i) {
        uint64_t first = last_level1_first + (uint64_t)i * L3_CAPACITY;
        level1.entries[i] = (struct l3_entry){
            first,
            (i == L3_CAPACITY - 1u) ? L3_TEST_LAST_LEAF : (300u + i),
            L3_CAPACITY,
            0u
        };
    }
    if (!store_node(L3_TEST_LAST_LEVEL1, &level1)) return false;

    uint64_t last_leaf_first = L3_FULL_LEVEL2_EXTENTS - L3_CAPACITY;
    struct l3_node leaf;
    init_node(&leaf, L3_LEVEL_LEAF, L3_CAPACITY, 80u,
              last_leaf_first, L3_FULL_LEVEL2_EXTENTS);
    for (uint16_t i = 0u; i < L3_CAPACITY; ++i)
        leaf.entries[i] = (struct l3_entry){last_leaf_first + i, 8192u + i, 1u, 0u};
    return store_node(L3_TEST_LAST_LEAF, &leaf);
}

static bool run_geometry(uint32_t block_size) {
    zero_bytes(&l3_test_ctx, sizeof(l3_test_ctx));
    for (uint32_t i = 0u; i < L3_TEST_SLOT_COUNT; ++i) l3_test_ctx.slot_block[i] = UINT64_MAX;
    for (uint64_t block = 0u; block < L3_TEST_DATA_START; ++block) bitmap_set(block);
    bitmap_set(L3_TEST_OLD_ROOT); bitmap_set(L3_TEST_LAST_LEVEL1); bitmap_set(L3_TEST_LAST_LEAF);
    bitmap_set(L3_TEST_NEW_DATA);
    if (!seed_boundary()) return false;

    uint64_t bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES + (uint64_t)L3_TEST_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct aurora_block_device device = {
        .name = "aurorafs-v2-level3-growth-test",
        .block_size = block_size,
        .block_count = bytes / block_size,
        .read_only = false,
        .context = NULL,
        .read_blocks = sparse_read,
        .write_blocks = sparse_write,
        .flush = sparse_flush
    };
    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(&allocator, &device,
            AURORA_FS_V2_DEFAULT_BASE_BYTES, L3_TEST_TOTAL_BLOCKS,
            L3_TEST_BITMAP_START, L3_TEST_BITMAP_BLOCKS, L3_TEST_DATA_START)) return false;

    uint8_t old_root_before[AURORA_FS_V2_FS_BLOCK_SIZE];
    int old_slot = slot_for(L3_TEST_OLD_ROOT, false);
    if (old_slot < 0) return false;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        old_root_before[i] = l3_test_ctx.slot_data[(uint32_t)old_slot][i];

    struct aurora_fs_v2_extent extent = {
        .logical_block = L3_FULL_LEVEL2_EXTENTS,
        .physical_block = L3_TEST_NEW_DATA,
        .block_count = 1u
    };
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_grow_level2_full_root_cow(&allocator, L3_TEST_OLD_ROOT, &extent, &new_root) ||
        new_root == 0u || new_root == L3_TEST_OLD_ROOT) return false;

    old_slot = slot_for(L3_TEST_OLD_ROOT, false);
    if (old_slot < 0) return false;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        if (old_root_before[i] != l3_test_ctx.slot_data[(uint32_t)old_slot][i]) return false;

    uint64_t physical = 0u, contiguous = 0u;
    return aurora_fs_v2_extent_tree_lookup_unified(&allocator, new_root,
               L3_FULL_LEVEL2_EXTENTS, &physical, &contiguous) &&
        physical == L3_TEST_NEW_DATA && contiguous == 1u;
}

bool aurora_fs_v2_extent_tree_level3_growth_self_test(void) {
    return run_geometry(512u) && run_geometry(4096u);
}
