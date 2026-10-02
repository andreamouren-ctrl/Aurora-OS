#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define L2A_HEADER_SIZE 64u
#define L2A_ENTRY_SIZE 32u
#define L2A_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - L2A_HEADER_SIZE) / L2A_ENTRY_SIZE)
#define L2A_LEVEL_LEAF 0u
#define L2A_LEVEL_ONE 1u
#define L2A_LEVEL_TWO 2u
#define L2A_TEST_TOTAL_BLOCKS 128u
#define L2A_TEST_BITMAP_START 1u
#define L2A_TEST_BITMAP_BLOCKS 1u
#define L2A_TEST_DATA_START 8u
#define L2A_OLD_LEAF 20u
#define L2A_OLD_LEVEL1 21u
#define L2A_OLD_LEVEL2 22u
#define L2A_FIRST_DATA 80u
#define L2A_SLOT_COUNT 12u

struct l2a_header {
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

struct l2a_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct l2a_node {
    struct l2a_header header;
    struct l2a_entry entries[L2A_CAPACITY];
} __attribute__((packed));

struct l2a_test_context {
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint64_t slot_block[L2A_SLOT_COUNT];
    uint8_t slot_data[L2A_SLOT_COUNT][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static struct l2a_node l2a_root_io;
static struct l2a_node l2a_level1_io;
static struct l2a_node l2a_leaf_io;
static struct l2a_test_context l2a_test;

_Static_assert(sizeof(struct l2a_header) == L2A_HEADER_SIZE,
               "AuroraFS v2 level-2 append header size");
_Static_assert(sizeof(struct l2a_entry) == L2A_ENTRY_SIZE,
               "AuroraFS v2 level-2 append entry size");
_Static_assert(sizeof(struct l2a_node) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 level-2 append node size");
_Static_assert(L2A_CAPACITY == 126u,
               "AuroraFS v2 level-2 append assumes 126 entries");

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

static uint32_t node_checksum(struct l2a_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t result = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return result;
}

static void set_magic(struct l2a_node *node) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        node->header.magic[i] = magic[i];
    }
}

static bool magic_valid(const struct l2a_node *node) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        if (node->header.magic[i] != magic[i]) {
            return false;
        }
    }
    return true;
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
    struct l2a_node *node
) {
    uint64_t lba;
    uint32_t count;
    if (node == NULL || !fs_geometry(allocator, block, &lba, &count) ||
        !block_device_read(allocator->device, lba, count, node) || !magic_valid(node) ||
        node->header.version != 1u || node->header.entry_count == 0u ||
        node->header.entry_count > L2A_CAPACITY ||
        node->header.last_logical_exclusive <= node->header.first_logical) {
        return false;
    }
    return node->header.checksum == node_checksum(node);
}

static bool write_node(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t block,
    struct l2a_node *node
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

static void release_block(struct aurora_fs_v2_allocator *allocator, uint64_t block) {
    if (block != 0u) {
        (void)aurora_fs_v2_allocator_free_range(allocator, block, 1u);
    }
}

bool aurora_fs_v2_extent_tree_append_level2_cow(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root_block,
    const struct aurora_fs_v2_extent *extent,
    uint64_t *out_new_root_block
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        extent == NULL || out_new_root_block == NULL || extent->block_count == 0u ||
        extent->physical_block < allocator->data_start ||
        !read_node(allocator, old_root_block, &l2a_root_io) ||
        l2a_root_io.header.level != L2A_LEVEL_TWO ||
        l2a_root_io.header.entry_count == 0u ||
        l2a_root_io.header.entry_count > L2A_CAPACITY) {
        return false;
    }

    uint64_t physical_end;
    uint64_t logical_end;
    if (!add_u64(extent->physical_block, extent->block_count, &physical_end) ||
        physical_end > allocator->total_fs_blocks ||
        !add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        extent->logical_block < l2a_root_io.header.last_logical_exclusive) {
        return false;
    }

    uint16_t root_last = l2a_root_io.header.entry_count - 1u;
    struct l2a_entry old_level1_entry = l2a_root_io.entries[root_last];
    if (!read_node(allocator, old_level1_entry.physical_or_child, &l2a_level1_io) ||
        l2a_level1_io.header.level != L2A_LEVEL_ONE ||
        l2a_level1_io.header.entry_count == 0u ||
        l2a_level1_io.header.entry_count > L2A_CAPACITY ||
        l2a_level1_io.header.last_logical_exclusive != l2a_root_io.header.last_logical_exclusive) {
        return false;
    }

    uint16_t level1_last = l2a_level1_io.header.entry_count - 1u;
    struct l2a_entry old_leaf_entry = l2a_level1_io.entries[level1_last];
    if (!read_node(allocator, old_leaf_entry.physical_or_child, &l2a_leaf_io) ||
        l2a_leaf_io.header.level != L2A_LEVEL_LEAF ||
        l2a_leaf_io.header.entry_count == 0u ||
        l2a_leaf_io.header.entry_count >= L2A_CAPACITY ||
        l2a_leaf_io.header.last_logical_exclusive != l2a_level1_io.header.last_logical_exclusive) {
        return false;
    }

    uint16_t leaf_slot = l2a_leaf_io.header.entry_count;
    l2a_leaf_io.entries[leaf_slot] = (struct l2a_entry){
        extent->logical_block, extent->physical_block, extent->block_count, 0u
    };
    l2a_leaf_io.header.entry_count++;
    l2a_leaf_io.header.last_logical_exclusive = logical_end;
    l2a_leaf_io.header.generation++;

    uint64_t new_leaf = 0u;
    uint64_t new_level1 = 0u;
    uint64_t new_level2 = 0u;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_leaf)) {
        return false;
    }
    if (!write_node(allocator, new_leaf, &l2a_leaf_io) ||
        !block_device_flush(allocator->device)) {
        release_block(allocator, new_leaf);
        return false;
    }

    l2a_level1_io.entries[level1_last].physical_or_child = new_leaf;
    l2a_level1_io.entries[level1_last].block_count_or_span =
        logical_end - old_leaf_entry.logical_block;
    l2a_level1_io.header.last_logical_exclusive = logical_end;
    l2a_level1_io.header.generation++;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_level1)) {
        release_block(allocator, new_leaf);
        return false;
    }
    if (!write_node(allocator, new_level1, &l2a_level1_io) ||
        !block_device_flush(allocator->device)) {
        release_block(allocator, new_level1);
        release_block(allocator, new_leaf);
        return false;
    }

    l2a_root_io.entries[root_last].physical_or_child = new_level1;
    l2a_root_io.entries[root_last].block_count_or_span =
        logical_end - old_level1_entry.logical_block;
    l2a_root_io.header.last_logical_exclusive = logical_end;
    l2a_root_io.header.generation++;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_level2)) {
        release_block(allocator, new_level1);
        release_block(allocator, new_leaf);
        return false;
    }
    if (!write_node(allocator, new_level2, &l2a_root_io) ||
        !block_device_flush(allocator->device)) {
        release_block(allocator, new_level2);
        release_block(allocator, new_level1);
        release_block(allocator, new_leaf);
        return false;
    }

    *out_new_root_block = new_level2;
    return true;
}

static uint8_t *block_ptr(uint64_t fs_block) {
    if (fs_block >= L2A_TEST_TOTAL_BLOCKS) {
        return NULL;
    }
    uint64_t offset = fs_block * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset + AURORA_FS_V2_FS_BLOCK_SIZE >
        sizeof(l2a_test.slot_data)) {
        return NULL;
    }
    return NULL;
}

static void bitmap_set(uint64_t block) {
    l2a_test.bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
}

static int slot_for(uint64_t block, bool create) {
    for (uint32_t i = 0u; i < L2A_SLOT_COUNT; ++i) {
        if (l2a_test.slot_block[i] == block) {
            return (int)i;
        }
    }
    if (!create) {
        return -1;
    }
    for (uint32_t i = 0u; i < L2A_SLOT_COUNT; ++i) {
        if (l2a_test.slot_block[i] == UINT64_MAX) {
            l2a_test.slot_block[i] = block;
            zero_bytes(l2a_test.slot_data[i], AURORA_FS_V2_FS_BLOCK_SIZE);
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
        L2A_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            out[i] = l2a_test.bitmap[i];
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
        out[i] = l2a_test.slot_data[(uint32_t)slot][i];
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
        L2A_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            l2a_test.bitmap[i] = src[i];
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
        l2a_test.slot_data[(uint32_t)slot][i] = src[i];
    }
    return true;
}

static bool sparse_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool store_node(uint64_t block, struct l2a_node *node) {
    int slot = slot_for(block, true);
    if (slot < 0) {
        return false;
    }
    node->header.checksum = node_checksum(node);
    const uint8_t *src = (const uint8_t *)node;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
        l2a_test.slot_data[(uint32_t)slot][i] = src[i];
    }
    return true;
}

static void init_node(
    struct l2a_node *node,
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

static bool setup_test(
    uint32_t block_size,
    struct aurora_block_device *device,
    struct aurora_fs_v2_allocator *allocator
) {
    zero_bytes(&l2a_test, sizeof(l2a_test));
    for (uint32_t i = 0u; i < L2A_SLOT_COUNT; ++i) {
        l2a_test.slot_block[i] = UINT64_MAX;
    }
    for (uint64_t block = 0u; block < L2A_TEST_DATA_START; ++block) {
        bitmap_set(block);
    }
    bitmap_set(L2A_OLD_LEAF);
    bitmap_set(L2A_OLD_LEVEL1);
    bitmap_set(L2A_OLD_LEVEL2);
    bitmap_set(L2A_FIRST_DATA);
    bitmap_set(L2A_FIRST_DATA + 1u);

    struct l2a_node leaf;
    struct l2a_node level1;
    struct l2a_node level2;
    init_node(&leaf, L2A_LEVEL_LEAF, 1u, 10u, 0u, 1u);
    leaf.entries[0] = (struct l2a_entry){0u, L2A_FIRST_DATA, 1u, 0u};
    init_node(&level1, L2A_LEVEL_ONE, 1u, 10u, 0u, 1u);
    level1.entries[0] = (struct l2a_entry){0u, L2A_OLD_LEAF, 1u, 0u};
    init_node(&level2, L2A_LEVEL_TWO, 1u, 10u, 0u, 1u);
    level2.entries[0] = (struct l2a_entry){0u, L2A_OLD_LEVEL1, 1u, 0u};
    if (!store_node(L2A_OLD_LEAF, &leaf) ||
        !store_node(L2A_OLD_LEVEL1, &level1) ||
        !store_node(L2A_OLD_LEVEL2, &level2)) {
        return false;
    }

    uint64_t bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        (uint64_t)L2A_TEST_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    *device = (struct aurora_block_device){
        .name = "aurorafs-v2-level2-append-test",
        .block_size = block_size,
        .block_count = bytes / block_size,
        .read_only = false,
        .context = &l2a_test,
        .read_blocks = sparse_read,
        .write_blocks = sparse_write,
        .flush = sparse_flush
    };
    return aurora_fs_v2_allocator_init(
        allocator,
        device,
        AURORA_FS_V2_DEFAULT_BASE_BYTES,
        L2A_TEST_TOTAL_BLOCKS,
        L2A_TEST_BITMAP_START,
        L2A_TEST_BITMAP_BLOCKS,
        L2A_TEST_DATA_START);
}

static bool run_geometry(uint32_t block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_allocator allocator;
    if (!setup_test(block_size, &device, &allocator)) {
        return false;
    }

    struct l2a_node old_root;
    struct l2a_node old_level1;
    struct l2a_node old_leaf;
    if (!read_node(&allocator, L2A_OLD_LEVEL2, &old_root) ||
        !read_node(&allocator, L2A_OLD_LEVEL1, &old_level1) ||
        !read_node(&allocator, L2A_OLD_LEAF, &old_leaf)) {
        return false;
    }

    struct aurora_fs_v2_extent next = {
        .logical_block = 1u,
        .physical_block = L2A_FIRST_DATA + 1u,
        .block_count = 1u
    };
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_append_level2_cow(
            &allocator, L2A_OLD_LEVEL2, &next, &new_root) ||
        new_root == L2A_OLD_LEVEL2) {
        return false;
    }

    for (uint64_t logical = 0u; logical < 2u; ++logical) {
        uint64_t physical = 0u;
        uint64_t contiguous = 0u;
        if (!aurora_fs_v2_extent_tree_lookup_unified(
                &allocator, new_root, logical, &physical, &contiguous) ||
            physical != L2A_FIRST_DATA + logical || contiguous != 1u) {
            return false;
        }
    }

    struct l2a_node root_after;
    struct l2a_node level1_after;
    struct l2a_node leaf_after;
    if (!read_node(&allocator, L2A_OLD_LEVEL2, &root_after) ||
        !read_node(&allocator, L2A_OLD_LEVEL1, &level1_after) ||
        !read_node(&allocator, L2A_OLD_LEAF, &leaf_after) ||
        root_after.header.generation != old_root.header.generation ||
        root_after.header.last_logical_exclusive != old_root.header.last_logical_exclusive ||
        level1_after.header.generation != old_level1.header.generation ||
        level1_after.header.last_logical_exclusive != old_level1.header.last_logical_exclusive ||
        leaf_after.header.generation != old_leaf.header.generation ||
        leaf_after.header.entry_count != old_leaf.header.entry_count) {
        return false;
    }

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened,
            &device,
            AURORA_FS_V2_DEFAULT_BASE_BYTES,
            L2A_TEST_TOTAL_BLOCKS,
            L2A_TEST_BITMAP_START,
            L2A_TEST_BITMAP_BLOCKS,
            L2A_TEST_DATA_START)) {
        return false;
    }
    uint64_t physical = 0u;
    uint64_t contiguous = 0u;
    return aurora_fs_v2_extent_tree_lookup_unified(
               &reopened, new_root, 1u, &physical, &contiguous) &&
        physical == L2A_FIRST_DATA + 1u && contiguous == 1u;
}

bool aurora_fs_v2_extent_tree_level2_append_self_test(void) {
    return run_geometry(512u) && run_geometry(4096u);
}
