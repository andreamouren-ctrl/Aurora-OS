#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define GROW_MAGIC_0 'A'
#define GROW_MAGIC_1 'U'
#define GROW_MAGIC_2 'R'
#define GROW_MAGIC_3 'E'
#define GROW_MAGIC_4 'X'
#define GROW_MAGIC_5 'T'
#define GROW_MAGIC_6 '2'
#define GROW_MAGIC_7 '\0'
#define GROW_VERSION 1u
#define GROW_HEADER_SIZE 64u
#define GROW_ENTRY_SIZE 32u
#define GROW_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - GROW_HEADER_SIZE) / GROW_ENTRY_SIZE)
#define GROW_LEVEL_LEAF 0u
#define GROW_LEVEL_ROOT 1u
#define GROW_TEST_TOTAL_BLOCKS 1024u
#define GROW_TEST_BITMAP_START 1u
#define GROW_TEST_BITMAP_BLOCKS 1u
#define GROW_TEST_DATA_START 16u
#define GROW_TEST_OLD_ROOT 16u
#define GROW_TEST_FIRST_DATA 100u
#define GROW_TEST_SLOT_COUNT 8u

struct grow_node_header_disk {
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

struct grow_node_entry_disk {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct grow_node_disk {
    struct grow_node_header_disk header;
    struct grow_node_entry_disk entries[GROW_CAPACITY];
} __attribute__((packed));

struct grow_test_context {
    uint32_t device_block_size;
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint64_t slot_block[GROW_TEST_SLOT_COUNT];
    uint8_t slot_data[GROW_TEST_SLOT_COUNT][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static struct grow_node_disk grow_old;
static struct grow_node_disk grow_leaf_a;
static struct grow_node_disk grow_leaf_b;
static struct grow_node_disk grow_root;
static struct grow_test_context grow_test;

_Static_assert(sizeof(struct grow_node_header_disk) == GROW_HEADER_SIZE,
               "AuroraFS v2 extent growth header must remain 64 bytes");
_Static_assert(sizeof(struct grow_node_entry_disk) == GROW_ENTRY_SIZE,
               "AuroraFS v2 extent growth entry must remain 32 bytes");
_Static_assert(sizeof(struct grow_node_disk) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 extent growth node must fill one filesystem block");
_Static_assert(GROW_CAPACITY == 126u,
               "AuroraFS v2 extent growth assumes 126 entries per node");

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

static uint32_t node_checksum(struct grow_node_disk *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return checksum;
}

static void set_magic(struct grow_node_disk *node) {
    static const uint8_t magic[8] = {
        GROW_MAGIC_0, GROW_MAGIC_1, GROW_MAGIC_2, GROW_MAGIC_3,
        GROW_MAGIC_4, GROW_MAGIC_5, GROW_MAGIC_6, GROW_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        node->header.magic[i] = magic[i];
    }
}

static bool magic_valid(const struct grow_node_disk *node) {
    static const uint8_t magic[8] = {
        GROW_MAGIC_0, GROW_MAGIC_1, GROW_MAGIC_2, GROW_MAGIC_3,
        GROW_MAGIC_4, GROW_MAGIC_5, GROW_MAGIC_6, GROW_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        if (node->header.magic[i] != magic[i]) {
            return false;
        }
    }
    return true;
}

static void init_node(
    struct grow_node_disk *node,
    uint16_t level,
    uint16_t entry_count,
    uint64_t generation,
    uint64_t first_logical,
    uint64_t last_logical_exclusive
) {
    zero_bytes(node, sizeof(*node));
    set_magic(node);
    node->header.version = GROW_VERSION;
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
    struct grow_node_disk *node
) {
    uint64_t lba;
    uint32_t count;
    if (node == NULL || !fs_block_geometry(allocator, fs_block, &lba, &count) ||
        !block_device_read(allocator->device, lba, count, node) ||
        !magic_valid(node) || node->header.version != GROW_VERSION ||
        node->header.entry_count == 0u || node->header.entry_count > GROW_CAPACITY ||
        node->header.last_logical_exclusive <= node->header.first_logical) {
        return false;
    }
    return node->header.checksum == node_checksum(node);
}

static bool write_node(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t fs_block,
    struct grow_node_disk *node
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

static void release_new_blocks(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t a,
    bool have_a,
    uint64_t b,
    bool have_b,
    uint64_t root,
    bool have_root
) {
    if (have_root) {
        (void)aurora_fs_v2_allocator_free_range(allocator, root, 1u);
    }
    if (have_b) {
        (void)aurora_fs_v2_allocator_free_range(allocator, b, 1u);
    }
    if (have_a) {
        (void)aurora_fs_v2_allocator_free_range(allocator, a, 1u);
    }
}

bool aurora_fs_v2_extent_tree_expand_full_leaf_cow(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root_block,
    const struct aurora_fs_v2_extent *extent,
    uint64_t *out_new_root_block
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        extent == NULL || out_new_root_block == NULL || extent->block_count == 0u ||
        extent->physical_block < allocator->data_start ||
        !read_node(allocator, old_root_block, &grow_old) ||
        grow_old.header.level != GROW_LEVEL_LEAF ||
        grow_old.header.entry_count != GROW_CAPACITY ||
        extent->logical_block < grow_old.header.last_logical_exclusive) {
        return false;
    }

    uint64_t physical_end;
    uint64_t logical_end;
    if (!add_u64(extent->physical_block, extent->block_count, &physical_end) ||
        physical_end > allocator->total_fs_blocks ||
        !add_u64(extent->logical_block, extent->block_count, &logical_end)) {
        return false;
    }

    grow_leaf_a = grow_old;
    grow_leaf_a.header.generation++;
    grow_leaf_a.header.checksum = 0u;

    init_node(
        &grow_leaf_b,
        GROW_LEVEL_LEAF,
        1u,
        grow_old.header.generation + 1u,
        extent->logical_block,
        logical_end);
    grow_leaf_b.entries[0].logical_block = extent->logical_block;
    grow_leaf_b.entries[0].physical_or_child = extent->physical_block;
    grow_leaf_b.entries[0].block_count_or_span = extent->block_count;

    uint64_t leaf_a_block = 0u;
    uint64_t leaf_b_block = 0u;
    uint64_t root_block = 0u;
    bool have_a = false;
    bool have_b = false;
    bool have_root = false;

    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &leaf_a_block)) {
        return false;
    }
    have_a = true;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &leaf_b_block)) {
        release_new_blocks(allocator, leaf_a_block, have_a, 0u, false, 0u, false);
        return false;
    }
    have_b = true;

    if (!write_node(allocator, leaf_a_block, &grow_leaf_a) ||
        !write_node(allocator, leaf_b_block, &grow_leaf_b) ||
        !block_device_flush(allocator->device)) {
        release_new_blocks(
            allocator, leaf_a_block, have_a, leaf_b_block, have_b, 0u, false);
        return false;
    }

    init_node(
        &grow_root,
        GROW_LEVEL_ROOT,
        2u,
        grow_old.header.generation + 1u,
        grow_old.header.first_logical,
        logical_end);
    grow_root.entries[0].logical_block = grow_old.header.first_logical;
    grow_root.entries[0].physical_or_child = leaf_a_block;
    grow_root.entries[0].block_count_or_span =
        grow_old.header.last_logical_exclusive - grow_old.header.first_logical;
    grow_root.entries[1].logical_block = extent->logical_block;
    grow_root.entries[1].physical_or_child = leaf_b_block;
    grow_root.entries[1].block_count_or_span = extent->block_count;

    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &root_block)) {
        release_new_blocks(
            allocator, leaf_a_block, have_a, leaf_b_block, have_b, 0u, false);
        return false;
    }
    have_root = true;
    if (!write_node(allocator, root_block, &grow_root) ||
        !block_device_flush(allocator->device)) {
        release_new_blocks(
            allocator, leaf_a_block, have_a, leaf_b_block, have_b, root_block, have_root);
        return false;
    }

    *out_new_root_block = root_block;
    return true;
}

static void bitmap_set(uint64_t fs_block) {
    grow_test.bitmap[fs_block >> 3] |= (uint8_t)(1u << (fs_block & 7u));
}

static int slot_for(uint64_t fs_block, bool create) {
    for (uint32_t i = 0u; i < GROW_TEST_SLOT_COUNT; ++i) {
        if (grow_test.slot_block[i] == fs_block) {
            return (int)i;
        }
    }
    if (!create) {
        return -1;
    }
    for (uint32_t i = 0u; i < GROW_TEST_SLOT_COUNT; ++i) {
        if (grow_test.slot_block[i] == UINT64_MAX) {
            grow_test.slot_block[i] = fs_block;
            zero_bytes(grow_test.slot_data[i], AURORA_FS_V2_FS_BLOCK_SIZE);
            return (int)i;
        }
    }
    return -1;
}

static bool test_transfer_geometry(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    uint64_t *out_byte_offset,
    uint64_t *out_byte_length
) {
    if (device == NULL || out_byte_offset == NULL || out_byte_length == NULL ||
        block_count == 0u || lba >= device->block_count ||
        (uint64_t)block_count > device->block_count - lba ||
        !mul_u64(lba, device->block_size, out_byte_offset) ||
        !mul_u64(block_count, device->block_size, out_byte_length)) {
        return false;
    }
    return true;
}

static bool sparse_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    uint64_t offset;
    uint64_t length;
    if (buffer == NULL || !test_transfer_geometry(device, lba, block_count, &offset, &length)) {
        return false;
    }
    uint8_t *out = buffer;
    zero_bytes(out, (size_t)length);

    uint64_t bitmap_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        GROW_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset && length == AURORA_FS_V2_FS_BLOCK_SIZE) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            out[i] = grow_test.bitmap[i];
        }
        return true;
    }

    if (offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u ||
        length != AURORA_FS_V2_FS_BLOCK_SIZE) {
        return false;
    }
    uint64_t fs_block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    int slot = slot_for(fs_block, false);
    if (slot < 0) {
        return true;
    }
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
        out[i] = grow_test.slot_data[(uint32_t)slot][i];
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
        !test_transfer_geometry(device, lba, block_count, &offset, &length)) {
        return false;
    }
    const uint8_t *source = buffer;
    uint64_t bitmap_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        GROW_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset && length == AURORA_FS_V2_FS_BLOCK_SIZE) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            grow_test.bitmap[i] = source[i];
        }
        return true;
    }
    if (offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u ||
        length != AURORA_FS_V2_FS_BLOCK_SIZE) {
        return false;
    }
    uint64_t fs_block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    int slot = slot_for(fs_block, true);
    if (slot < 0) {
        return false;
    }
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
        grow_test.slot_data[(uint32_t)slot][i] = source[i];
    }
    return true;
}

static bool sparse_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool run_growth_geometry(uint32_t device_block_size) {
    zero_bytes(&grow_test, sizeof(grow_test));
    grow_test.device_block_size = device_block_size;
    for (uint32_t i = 0u; i < GROW_TEST_SLOT_COUNT; ++i) {
        grow_test.slot_block[i] = UINT64_MAX;
    }

    for (uint64_t block = 0u; block < GROW_TEST_DATA_START; ++block) {
        bitmap_set(block);
    }
    bitmap_set(GROW_TEST_OLD_ROOT);
    for (uint64_t block = GROW_TEST_FIRST_DATA;
         block < GROW_TEST_FIRST_DATA + GROW_CAPACITY + 1u;
         ++block) {
        bitmap_set(block);
    }

    uint64_t virtual_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        GROW_TEST_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct aurora_block_device device = {
        .name = "aurorafs-v2-growth-test",
        .block_size = device_block_size,
        .block_count = virtual_bytes / device_block_size,
        .read_only = false,
        .context = &grow_test,
        .read_blocks = sparse_read,
        .write_blocks = sparse_write,
        .flush = sparse_flush
    };
    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator,
            &device,
            AURORA_FS_V2_DEFAULT_BASE_BYTES,
            GROW_TEST_TOTAL_BLOCKS,
            GROW_TEST_BITMAP_START,
            GROW_TEST_BITMAP_BLOCKS,
            GROW_TEST_DATA_START)) {
        return false;
    }

    init_node(&grow_old, GROW_LEVEL_LEAF, GROW_CAPACITY, 7u, 0u, GROW_CAPACITY);
    for (uint32_t i = 0u; i < GROW_CAPACITY; ++i) {
        grow_old.entries[i].logical_block = i;
        grow_old.entries[i].physical_or_child = GROW_TEST_FIRST_DATA + i;
        grow_old.entries[i].block_count_or_span = 1u;
    }
    int old_slot = slot_for(GROW_TEST_OLD_ROOT, true);
    if (old_slot < 0) {
        return false;
    }
    grow_old.header.checksum = node_checksum(&grow_old);
    const uint8_t *old_bytes = (const uint8_t *)&grow_old;
    for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
        grow_test.slot_data[(uint32_t)old_slot][i] = old_bytes[i];
    }

    struct aurora_fs_v2_extent next = {
        .logical_block = GROW_CAPACITY,
        .physical_block = GROW_TEST_FIRST_DATA + GROW_CAPACITY,
        .block_count = 1u
    };
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_expand_full_leaf_cow(
            &allocator, GROW_TEST_OLD_ROOT, &next, &new_root) ||
        new_root == GROW_TEST_OLD_ROOT) {
        return false;
    }

    struct grow_node_disk persisted_root;
    if (!read_node(&allocator, new_root, &persisted_root) ||
        persisted_root.header.level != GROW_LEVEL_ROOT ||
        persisted_root.header.entry_count != 2u ||
        persisted_root.header.first_logical != 0u ||
        persisted_root.header.last_logical_exclusive != GROW_CAPACITY + 1u) {
        return false;
    }

    for (uint64_t logical = 0u; logical < GROW_CAPACITY + 1u; ++logical) {
        uint64_t physical = 0u;
        uint64_t contiguous = 0u;
        if (!aurora_fs_v2_extent_tree_lookup(
                &allocator, new_root, logical, &physical, &contiguous) ||
            physical != GROW_TEST_FIRST_DATA + logical || contiguous != 1u) {
            return false;
        }
    }

    struct grow_node_disk old_after;
    if (!read_node(&allocator, GROW_TEST_OLD_ROOT, &old_after) ||
        old_after.header.level != GROW_LEVEL_LEAF ||
        old_after.header.entry_count != GROW_CAPACITY ||
        old_after.header.generation != 7u) {
        return false;
    }
    return true;
}

bool aurora_fs_v2_extent_tree_growth_self_test(void) {
    return run_growth_geometry(512u) && run_growth_geometry(4096u);
}
