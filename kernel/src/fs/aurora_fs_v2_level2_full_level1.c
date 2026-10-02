#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define F1_HEADER_SIZE 64u
#define F1_ENTRY_SIZE 32u
#define F1_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - F1_HEADER_SIZE) / F1_ENTRY_SIZE)
#define F1_LEVEL_LEAF 0u
#define F1_LEVEL_ONE 1u
#define F1_LEVEL_TWO 2u
#define F1_TOTAL_BLOCKS 32768u
#define F1_BITMAP_START 1u
#define F1_BITMAP_BLOCKS 1u
#define F1_DATA_START 16u
#define F1_OLD_LEVEL1 20u
#define F1_OLD_LEVEL2 21u
#define F1_FIRST_LEAF 32u
#define F1_FIRST_DATA 1024u
#define F1_FULL_LEVEL1_EXTENTS (F1_CAPACITY * F1_CAPACITY)
#define F1_SLOT_COUNT 8u

struct f1_header {
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

struct f1_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct f1_node {
    struct f1_header header;
    struct f1_entry entries[F1_CAPACITY];
} __attribute__((packed));

struct f1_test_context {
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint64_t slot_block[F1_SLOT_COUNT];
    uint8_t slot_data[F1_SLOT_COUNT][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static struct f1_node root_io;
static struct f1_node level1_io;
static struct f1_node leaf_io;
static struct f1_test_context test_ctx;

_Static_assert(sizeof(struct f1_header) == F1_HEADER_SIZE,
               "AuroraFS v2 full-level1 header size");
_Static_assert(sizeof(struct f1_entry) == F1_ENTRY_SIZE,
               "AuroraFS v2 full-level1 entry size");
_Static_assert(sizeof(struct f1_node) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 full-level1 node size");
_Static_assert(F1_CAPACITY == 126u,
               "AuroraFS v2 full-level1 gate assumes 126 entries");

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

static uint32_t node_checksum(struct f1_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t result = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return result;
}

static void set_magic(struct f1_node *node) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        node->header.magic[i] = magic[i];
    }
}

static bool magic_valid(const struct f1_node *node) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        if (node->header.magic[i] != magic[i]) {
            return false;
        }
    }
    return true;
}

static void init_node(struct f1_node *node, uint16_t level, uint16_t count,
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

static bool read_node(struct aurora_fs_v2_allocator *allocator,
                      uint64_t block, struct f1_node *node) {
    uint64_t lba;
    uint32_t count;
    if (node == NULL || !fs_geometry(allocator, block, &lba, &count) ||
        !block_device_read(allocator->device, lba, count, node) || !magic_valid(node) ||
        node->header.version != 1u || node->header.entry_count == 0u ||
        node->header.entry_count > F1_CAPACITY ||
        node->header.last_logical_exclusive <= node->header.first_logical) {
        return false;
    }
    return node->header.checksum == node_checksum(node);
}

static bool write_node(struct aurora_fs_v2_allocator *allocator,
                       uint64_t block, struct f1_node *node) {
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

bool aurora_fs_v2_extent_tree_append_level2_full_level1_cow(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root_block,
    const struct aurora_fs_v2_extent *extent,
    uint64_t *out_new_root_block
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        extent == NULL || out_new_root_block == NULL || extent->block_count == 0u ||
        extent->physical_block < allocator->data_start ||
        !read_node(allocator, old_root_block, &root_io) ||
        root_io.header.level != F1_LEVEL_TWO || root_io.header.entry_count == 0u ||
        root_io.header.entry_count >= F1_CAPACITY) {
        return false;
    }

    uint16_t root_last = root_io.header.entry_count - 1u;
    struct f1_entry old_level1_entry = root_io.entries[root_last];
    if (!read_node(allocator, old_level1_entry.physical_or_child, &level1_io) ||
        level1_io.header.level != F1_LEVEL_ONE || level1_io.header.entry_count != F1_CAPACITY ||
        level1_io.header.first_logical != old_level1_entry.logical_block ||
        level1_io.header.last_logical_exclusive != root_io.header.last_logical_exclusive) {
        return false;
    }

    uint16_t level1_last = level1_io.header.entry_count - 1u;
    struct f1_entry old_leaf_entry = level1_io.entries[level1_last];
    if (!read_node(allocator, old_leaf_entry.physical_or_child, &leaf_io) ||
        leaf_io.header.level != F1_LEVEL_LEAF || leaf_io.header.entry_count != F1_CAPACITY ||
        leaf_io.header.first_logical != old_leaf_entry.logical_block ||
        leaf_io.header.last_logical_exclusive != level1_io.header.last_logical_exclusive) {
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

    uint64_t generation = root_io.header.generation + 1u;
    struct f1_node new_leaf_node;
    init_node(&new_leaf_node, F1_LEVEL_LEAF, 1u, generation,
              extent->logical_block, logical_end);
    new_leaf_node.entries[0] = (struct f1_entry){
        extent->logical_block, extent->physical_block, extent->block_count, 0u
    };

    uint64_t new_leaf = 0u;
    uint64_t new_level1 = 0u;
    uint64_t new_level2 = 0u;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_leaf)) {
        return false;
    }
    if (!write_node(allocator, new_leaf, &new_leaf_node) ||
        !block_device_flush(allocator->device)) {
        release_block(allocator, new_leaf);
        return false;
    }

    struct f1_node new_level1_node;
    init_node(&new_level1_node, F1_LEVEL_ONE, 1u, generation,
              extent->logical_block, logical_end);
    new_level1_node.entries[0] = (struct f1_entry){
        extent->logical_block, new_leaf, extent->block_count, 0u
    };
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_level1)) {
        release_block(allocator, new_leaf);
        return false;
    }
    if (!write_node(allocator, new_level1, &new_level1_node) ||
        !block_device_flush(allocator->device)) {
        release_block(allocator, new_level1);
        release_block(allocator, new_leaf);
        return false;
    }

    uint16_t new_slot = root_io.header.entry_count;
    root_io.entries[new_slot] = (struct f1_entry){
        extent->logical_block, new_level1, logical_end - extent->logical_block, 0u
    };
    root_io.header.entry_count++;
    root_io.header.last_logical_exclusive = logical_end;
    root_io.header.generation++;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_level2)) {
        release_block(allocator, new_level1);
        release_block(allocator, new_leaf);
        return false;
    }
    if (!write_node(allocator, new_level2, &root_io) ||
        !block_device_flush(allocator->device)) {
        release_block(allocator, new_level2);
        release_block(allocator, new_level1);
        release_block(allocator, new_leaf);
        return false;
    }

    *out_new_root_block = new_level2;
    return true;
}

static void bitmap_set(uint64_t block) {
    test_ctx.bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
}

static int slot_for(uint64_t block, bool create) {
    for (uint32_t i = 0u; i < F1_SLOT_COUNT; ++i) {
        if (test_ctx.slot_block[i] == block) {
            return (int)i;
        }
    }
    if (!create) {
        return -1;
    }
    for (uint32_t i = 0u; i < F1_SLOT_COUNT; ++i) {
        if (test_ctx.slot_block[i] == UINT64_MAX) {
            test_ctx.slot_block[i] = block;
            zero_bytes(test_ctx.slot_data[i], AURORA_FS_V2_FS_BLOCK_SIZE);
            return (int)i;
        }
    }
    return -1;
}

static void seed_old_level1(struct f1_node *node) {
    init_node(node, F1_LEVEL_ONE, F1_CAPACITY, 20u, 0u, F1_FULL_LEVEL1_EXTENTS);
    for (uint16_t child = 0u; child < F1_CAPACITY; ++child) {
        uint64_t first = (uint64_t)child * F1_CAPACITY;
        node->entries[child] = (struct f1_entry){
            first, F1_FIRST_LEAF + child, F1_CAPACITY, 0u
        };
    }
    node->header.checksum = node_checksum(node);
}

static void seed_old_root(struct f1_node *node) {
    init_node(node, F1_LEVEL_TWO, 1u, 20u, 0u, F1_FULL_LEVEL1_EXTENTS);
    node->entries[0] = (struct f1_entry){
        0u, F1_OLD_LEVEL1, F1_FULL_LEVEL1_EXTENTS, 0u
    };
    node->header.checksum = node_checksum(node);
}

static void seed_old_leaf(uint64_t fs_block, struct f1_node *node) {
    uint64_t child = fs_block - F1_FIRST_LEAF;
    uint64_t first = child * F1_CAPACITY;
    init_node(node, F1_LEVEL_LEAF, F1_CAPACITY, 20u, first, first + F1_CAPACITY);
    for (uint16_t entry = 0u; entry < F1_CAPACITY; ++entry) {
        uint64_t logical = first + entry;
        node->entries[entry] = (struct f1_entry){
            logical, F1_FIRST_DATA + logical, 1u, 0u
        };
    }
    node->header.checksum = node_checksum(node);
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
        length != AURORA_FS_V2_FS_BLOCK_SIZE) {
        return false;
    }
    uint8_t *out = buffer;
    zero_bytes(out, (size_t)length);
    uint64_t bitmap_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        F1_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
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
    uint64_t fs_block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;

    int slot = slot_for(fs_block, false);
    if (slot >= 0) {
        for (uint32_t i = 0u; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) {
            out[i] = test_ctx.slot_data[(uint32_t)slot][i];
        }
        return true;
    }

    struct f1_node generated;
    bool generated_node = true;
    if (fs_block == F1_OLD_LEVEL2) {
        seed_old_root(&generated);
    } else if (fs_block == F1_OLD_LEVEL1) {
        seed_old_level1(&generated);
    } else if (fs_block >= F1_FIRST_LEAF &&
               fs_block < F1_FIRST_LEAF + F1_CAPACITY) {
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
        length != AURORA_FS_V2_FS_BLOCK_SIZE) {
        return false;
    }
    const uint8_t *src = buffer;
    uint64_t bitmap_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        F1_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
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
    uint64_t fs_block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
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

static bool setup_test(uint32_t block_size, struct aurora_block_device *device,
                       struct aurora_fs_v2_allocator *allocator) {
    zero_bytes(&test_ctx, sizeof(test_ctx));
    for (uint32_t i = 0u; i < F1_SLOT_COUNT; ++i) {
        test_ctx.slot_block[i] = UINT64_MAX;
    }
    for (uint64_t block = 0u; block < F1_DATA_START; ++block) {
        bitmap_set(block);
    }
    bitmap_set(F1_OLD_LEVEL1);
    bitmap_set(F1_OLD_LEVEL2);
    for (uint64_t child = 0u; child < F1_CAPACITY; ++child) {
        bitmap_set(F1_FIRST_LEAF + child);
    }
    for (uint64_t logical = 0u; logical <= F1_FULL_LEVEL1_EXTENTS; ++logical) {
        bitmap_set(F1_FIRST_DATA + logical);
    }

    uint64_t bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        (uint64_t)F1_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    *device = (struct aurora_block_device){
        .name = "aurorafs-v2-level2-full-level1-test",
        .block_size = block_size,
        .block_count = bytes / block_size,
        .read_only = false,
        .context = &test_ctx,
        .read_blocks = sparse_read,
        .write_blocks = sparse_write,
        .flush = sparse_flush
    };
    return aurora_fs_v2_allocator_init(
        allocator, device, AURORA_FS_V2_DEFAULT_BASE_BYTES,
        F1_TOTAL_BLOCKS, F1_BITMAP_START, F1_BITMAP_BLOCKS, F1_DATA_START);
}

static bool run_test(uint32_t block_size) {
    struct aurora_block_device device;
    struct aurora_fs_v2_allocator allocator;
    if (!setup_test(block_size, &device, &allocator)) {
        return false;
    }

    struct f1_node old_root;
    struct f1_node old_level1;
    struct f1_node old_last_leaf;
    if (!read_node(&allocator, F1_OLD_LEVEL2, &old_root) ||
        !read_node(&allocator, F1_OLD_LEVEL1, &old_level1) ||
        !read_node(&allocator, F1_FIRST_LEAF + F1_CAPACITY - 1u, &old_last_leaf)) {
        return false;
    }

    struct aurora_fs_v2_extent next = {
        .logical_block = F1_FULL_LEVEL1_EXTENTS,
        .physical_block = F1_FIRST_DATA + F1_FULL_LEVEL1_EXTENTS,
        .block_count = 1u
    };
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_append_level2_full_level1_cow(
            &allocator, F1_OLD_LEVEL2, &next, &new_root) ||
        new_root == F1_OLD_LEVEL2) {
        return false;
    }

    uint64_t physical = 0u;
    uint64_t contiguous = 0u;
    if (!aurora_fs_v2_extent_tree_lookup_unified(
            &allocator, new_root, 0u, &physical, &contiguous) ||
        physical != F1_FIRST_DATA || contiguous != 1u ||
        !aurora_fs_v2_extent_tree_lookup_unified(
            &allocator, new_root, F1_FULL_LEVEL1_EXTENTS, &physical, &contiguous) ||
        physical != F1_FIRST_DATA + F1_FULL_LEVEL1_EXTENTS || contiguous != 1u) {
        return false;
    }

    struct f1_node old_root_after;
    struct f1_node old_level1_after;
    struct f1_node old_last_leaf_after;
    if (!read_node(&allocator, F1_OLD_LEVEL2, &old_root_after) ||
        !read_node(&allocator, F1_OLD_LEVEL1, &old_level1_after) ||
        !read_node(&allocator, F1_FIRST_LEAF + F1_CAPACITY - 1u, &old_last_leaf_after) ||
        old_root_after.header.generation != old_root.header.generation ||
        old_root_after.header.entry_count != old_root.header.entry_count ||
        old_level1_after.header.generation != old_level1.header.generation ||
        old_level1_after.header.entry_count != old_level1.header.entry_count ||
        old_last_leaf_after.header.generation != old_last_leaf.header.generation ||
        old_last_leaf_after.header.entry_count != old_last_leaf.header.entry_count) {
        return false;
    }

    struct f1_node replacement_root;
    if (!read_node(&allocator, new_root, &replacement_root) ||
        replacement_root.header.level != F1_LEVEL_TWO ||
        replacement_root.header.entry_count != 2u ||
        replacement_root.entries[0].physical_or_child != F1_OLD_LEVEL1 ||
        replacement_root.entries[1].logical_block != F1_FULL_LEVEL1_EXTENTS ||
        replacement_root.entries[1].physical_or_child == F1_OLD_LEVEL1) {
        return false;
    }

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, AURORA_FS_V2_DEFAULT_BASE_BYTES,
            F1_TOTAL_BLOCKS, F1_BITMAP_START, F1_BITMAP_BLOCKS, F1_DATA_START)) {
        return false;
    }
    return aurora_fs_v2_extent_tree_lookup_unified(
               &reopened, new_root, F1_FULL_LEVEL1_EXTENTS, &physical, &contiguous) &&
        physical == F1_FIRST_DATA + F1_FULL_LEVEL1_EXTENTS && contiguous == 1u;
}

bool aurora_fs_v2_extent_tree_level2_full_level1_self_test(void) {
    return run_test(512u) && run_test(4096u);
}
