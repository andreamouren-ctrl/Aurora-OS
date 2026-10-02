#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define L3A_HEADER_SIZE 64u
#define L3A_ENTRY_SIZE 32u
#define L3A_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - L3A_HEADER_SIZE) / L3A_ENTRY_SIZE)
#define L3A_LEVEL_TWO 2u
#define L3A_LEVEL_THREE 3u
#define L3A_FULL_LEVEL2_EXTENTS 2000376ull
#define L3A_TEST_TOTAL_BLOCKS 128u
#define L3A_TEST_BITMAP_START 1u
#define L3A_TEST_DATA_START 8u
#define L3A_OLD_LEAF 20u
#define L3A_OLD_LEVEL1 21u
#define L3A_OLD_LEVEL2 22u
#define L3A_OLD_LEVEL3 23u
#define L3A_UNUSED_OLD_BRANCH 24u
#define L3A_OLD_DATA 80u
#define L3A_NEW_DATA 81u
#define L3A_SLOT_COUNT 16u

struct l3a_header {
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

struct l3a_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct l3a_node {
    struct l3a_header header;
    struct l3a_entry entries[L3A_CAPACITY];
} __attribute__((packed));

struct l3a_test_context {
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint64_t slot_block[L3A_SLOT_COUNT];
    uint8_t slot_data[L3A_SLOT_COUNT][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static struct l3a_node l3a_root_io;
static struct l3a_node l3a_child_io;
static struct l3a_test_context l3a_test;

_Static_assert(sizeof(struct l3a_header) == L3A_HEADER_SIZE,
               "AuroraFS v2 level-3 append header size");
_Static_assert(sizeof(struct l3a_entry) == L3A_ENTRY_SIZE,
               "AuroraFS v2 level-3 append entry size");
_Static_assert(sizeof(struct l3a_node) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 level-3 append node size");
_Static_assert(L3A_CAPACITY == 126u,
               "AuroraFS v2 level-3 append assumes 126 entries");

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

static uint32_t node_checksum(struct l3a_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t result = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return result;
}

static bool magic_valid(const struct l3a_node *node) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0u; i < sizeof(magic); ++i)
        if (node->header.magic[i] != magic[i]) return false;
    return true;
}

static void init_node(struct l3a_node *node, uint16_t level, uint16_t count,
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

static bool read_node(struct aurora_fs_v2_allocator *allocator,
                      uint64_t block, struct l3a_node *node) {
    uint64_t lba;
    uint32_t count;
    if (node == NULL || !fs_geometry(allocator, block, &lba, &count) ||
        !block_device_read(allocator->device, lba, count, node) ||
        !magic_valid(node) || node->header.version != 1u ||
        node->header.entry_count == 0u || node->header.entry_count > L3A_CAPACITY ||
        node->header.last_logical_exclusive <= node->header.first_logical) return false;
    return node->header.checksum == node_checksum(node);
}

static bool write_node(struct aurora_fs_v2_allocator *allocator,
                       uint64_t block, struct l3a_node *node) {
    uint64_t lba;
    uint32_t count;
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        node == NULL || !fs_geometry(allocator, block, &lba, &count)) return false;
    node->header.checksum = node_checksum(node);
    return block_device_write(allocator->device, lba, count, node);
}

bool aurora_fs_v2_extent_tree_append_level3_cow(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root_block,
    const struct aurora_fs_v2_extent *extent,
    uint64_t *out_new_root_block
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        extent == NULL || out_new_root_block == NULL || extent->block_count == 0u ||
        extent->physical_block < allocator->data_start ||
        !read_node(allocator, old_root_block, &l3a_root_io) ||
        l3a_root_io.header.level != L3A_LEVEL_THREE ||
        l3a_root_io.header.entry_count == 0u ||
        l3a_root_io.header.entry_count > L3A_CAPACITY) return false;

    uint64_t physical_end;
    uint64_t logical_end;
    if (!add_u64(extent->physical_block, extent->block_count, &physical_end) ||
        physical_end > allocator->total_fs_blocks ||
        !add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        extent->logical_block < l3a_root_io.header.last_logical_exclusive) return false;

    uint16_t root_last = l3a_root_io.header.entry_count - 1u;
    struct l3a_entry old_child_entry = l3a_root_io.entries[root_last];
    if (old_child_entry.block_count_or_span == 0u ||
        old_child_entry.logical_block >= l3a_root_io.header.last_logical_exclusive ||
        !read_node(allocator, old_child_entry.physical_or_child, &l3a_child_io) ||
        l3a_child_io.header.level != L3A_LEVEL_TWO ||
        l3a_child_io.header.first_logical != old_child_entry.logical_block ||
        l3a_child_io.header.last_logical_exclusive != l3a_root_io.header.last_logical_exclusive ||
        old_child_entry.block_count_or_span !=
            l3a_child_io.header.last_logical_exclusive - l3a_child_io.header.first_logical) return false;

    uint64_t new_level2 = 0u;
    if (!aurora_fs_v2_extent_tree_append_level2_cow(
            allocator, old_child_entry.physical_or_child, extent, &new_level2)) return false;

    l3a_root_io.entries[root_last].physical_or_child = new_level2;
    l3a_root_io.entries[root_last].block_count_or_span =
        logical_end - old_child_entry.logical_block;
    l3a_root_io.header.last_logical_exclusive = logical_end;
    l3a_root_io.header.generation++;

    uint64_t new_root = 0u;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_root) ||
        !write_node(allocator, new_root, &l3a_root_io) ||
        !block_device_flush(allocator->device)) {
        /* The already-durable replacement level-2 subtree may remain leaked.
           This matches the current pre-publication COW recovery policy. */
        if (new_root != 0u)
            (void)aurora_fs_v2_allocator_free_range(allocator, new_root, 1u);
        return false;
    }

    *out_new_root_block = new_root;
    return true;
}

static void bitmap_set(uint64_t block) {
    l3a_test.bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
}

static int slot_for(uint64_t block, bool create) {
    for (uint32_t i = 0u; i < L3A_SLOT_COUNT; ++i)
        if (l3a_test.slot_block[i] == block) return (int)i;
    if (!create) return -1;
    for (uint32_t i = 0u; i < L3A_SLOT_COUNT; ++i) {
        if (l3a_test.slot_block[i] == UINT64_MAX) {
            l3a_test.slot_block[i] = block;
            zero_bytes(l3a_test.slot_data[i], AURORA_FS_V2_FS_BLOCK_SIZE);
            return (int)i;
        }
    }
    return -1;
}

static bool transfer_geometry(struct aurora_block_device *device,
                              uint64_t lba, uint32_t count,
                              uint64_t *out_offset, uint64_t *out_length) {
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
        length != AURORA_FS_V2_FS_BLOCK_SIZE || offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u)
        return false;
    uint64_t block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint8_t *out = buffer;
    zero_bytes(out, AURORA_FS_V2_FS_BLOCK_SIZE);
    if (block == L3A_TEST_BITMAP_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) out[i] = l3a_test.bitmap[i];
        return true;
    }
    int slot = slot_for(block, false);
    if (slot < 0) return true;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        out[i] = l3a_test.slot_data[(uint32_t)slot][i];
    return true;
}

static bool sparse_write(struct aurora_block_device *device, uint64_t lba,
                         uint32_t count, const void *buffer) {
    uint64_t offset;
    uint64_t length;
    if (buffer == NULL || device == NULL || device->read_only ||
        !transfer_geometry(device, lba, count, &offset, &length) ||
        length != AURORA_FS_V2_FS_BLOCK_SIZE || offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u)
        return false;
    uint64_t block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    const uint8_t *src = buffer;
    if (block == L3A_TEST_BITMAP_START) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) l3a_test.bitmap[i] = src[i];
        return true;
    }
    int slot = slot_for(block, true);
    if (slot < 0) return false;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        l3a_test.slot_data[(uint32_t)slot][i] = src[i];
    return true;
}

static bool sparse_flush(struct aurora_block_device *device) { return device != NULL; }

static bool store_node(uint64_t block, struct l3a_node *node) {
    int slot = slot_for(block, true);
    if (slot < 0) return false;
    node->header.checksum = node_checksum(node);
    const uint8_t *src = (const uint8_t *)node;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        l3a_test.slot_data[(uint32_t)slot][i] = src[i];
    return true;
}

static bool seed_tree(void) {
    struct l3a_node leaf;
    init_node(&leaf, 0u, 1u, 90u,
              L3A_FULL_LEVEL2_EXTENTS, L3A_FULL_LEVEL2_EXTENTS + 1u);
    leaf.entries[0] = (struct l3a_entry){
        L3A_FULL_LEVEL2_EXTENTS, L3A_OLD_DATA, 1u, 0u
    };
    if (!store_node(L3A_OLD_LEAF, &leaf)) return false;

    struct l3a_node level1;
    init_node(&level1, 1u, 1u, 90u,
              L3A_FULL_LEVEL2_EXTENTS, L3A_FULL_LEVEL2_EXTENTS + 1u);
    level1.entries[0] = (struct l3a_entry){
        L3A_FULL_LEVEL2_EXTENTS, L3A_OLD_LEAF, 1u, 0u
    };
    if (!store_node(L3A_OLD_LEVEL1, &level1)) return false;

    struct l3a_node level2;
    init_node(&level2, 2u, 1u, 90u,
              L3A_FULL_LEVEL2_EXTENTS, L3A_FULL_LEVEL2_EXTENTS + 1u);
    level2.entries[0] = (struct l3a_entry){
        L3A_FULL_LEVEL2_EXTENTS, L3A_OLD_LEVEL1, 1u, 0u
    };
    if (!store_node(L3A_OLD_LEVEL2, &level2)) return false;

    struct l3a_node level3;
    init_node(&level3, 3u, 2u, 90u,
              0u, L3A_FULL_LEVEL2_EXTENTS + 1u);
    level3.entries[0] = (struct l3a_entry){
        0u, L3A_UNUSED_OLD_BRANCH, L3A_FULL_LEVEL2_EXTENTS, 0u
    };
    level3.entries[1] = (struct l3a_entry){
        L3A_FULL_LEVEL2_EXTENTS, L3A_OLD_LEVEL2, 1u, 0u
    };
    return store_node(L3A_OLD_LEVEL3, &level3);
}

static bool run_geometry(uint32_t block_size) {
    zero_bytes(&l3a_test, sizeof(l3a_test));
    for (uint32_t i = 0u; i < L3A_SLOT_COUNT; ++i) l3a_test.slot_block[i] = UINT64_MAX;
    for (uint64_t block = 0u; block < L3A_TEST_DATA_START; ++block) bitmap_set(block);
    bitmap_set(L3A_OLD_LEAF);
    bitmap_set(L3A_OLD_LEVEL1);
    bitmap_set(L3A_OLD_LEVEL2);
    bitmap_set(L3A_OLD_LEVEL3);
    bitmap_set(L3A_UNUSED_OLD_BRANCH);
    bitmap_set(L3A_OLD_DATA);
    bitmap_set(L3A_NEW_DATA);
    if (!seed_tree()) return false;

    uint64_t bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        (uint64_t)L3A_TEST_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct aurora_block_device device = {
        .name = "aurorafs-v2-level3-append-test",
        .block_size = block_size,
        .block_count = bytes / block_size,
        .read_only = false,
        .context = &l3a_test,
        .read_blocks = sparse_read,
        .write_blocks = sparse_write,
        .flush = sparse_flush
    };
    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator, &device, AURORA_FS_V2_DEFAULT_BASE_BYTES,
            L3A_TEST_TOTAL_BLOCKS, L3A_TEST_BITMAP_START, 1u, L3A_TEST_DATA_START))
        return false;

    int old_root_slot = slot_for(L3A_OLD_LEVEL3, false);
    if (old_root_slot < 0) return false;
    uint8_t old_root_before[AURORA_FS_V2_FS_BLOCK_SIZE];
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        old_root_before[i] = l3a_test.slot_data[(uint32_t)old_root_slot][i];

    struct aurora_fs_v2_extent extent = {
        .logical_block = L3A_FULL_LEVEL2_EXTENTS + 1u,
        .physical_block = L3A_NEW_DATA,
        .block_count = 1u
    };
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_append_level3_cow(
            &allocator, L3A_OLD_LEVEL3, &extent, &new_root) ||
        new_root == 0u || new_root == L3A_OLD_LEVEL3) return false;

    old_root_slot = slot_for(L3A_OLD_LEVEL3, false);
    if (old_root_slot < 0) return false;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i)
        if (old_root_before[i] != l3a_test.slot_data[(uint32_t)old_root_slot][i]) return false;

    uint64_t physical = 0u;
    uint64_t contiguous = 0u;
    if (!aurora_fs_v2_extent_tree_lookup_unified(
            &allocator, new_root, L3A_FULL_LEVEL2_EXTENTS + 1u,
            &physical, &contiguous) ||
        physical != L3A_NEW_DATA || contiguous != 1u) return false;

    physical = 0u;
    contiguous = 0u;
    return aurora_fs_v2_extent_tree_lookup_unified(
               &allocator, new_root, L3A_FULL_LEVEL2_EXTENTS,
               &physical, &contiguous) &&
        physical == L3A_OLD_DATA && contiguous == 1u;
}

bool aurora_fs_v2_extent_tree_level3_append_self_test(void) {
    return run_geometry(512u) && run_geometry(4096u);
}
