#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_limits.h>
#include <aurora/block_device.h>

#define L3L_HEADER_SIZE 64u
#define L3L_ENTRY_SIZE 32u
#define L3L_CAPACITY AURORA_FS_V2_EXTENT_NODE_CAPACITY
#define L3L_LEVEL_THREE AURORA_FS_V2_MAX_ROOT_LEVEL
#define L3L_TOTAL_BLOCKS 128u
#define L3L_BITMAP_START 1u
#define L3L_INODE_START 2u
#define L3L_INODE_BLOCKS 1u
#define L3L_DATA_START 16u
#define L3L_ROOT_BLOCK 20u
#define L3L_NEW_DATA 40u
#define L3L_INODE_INDEX 1u
#define L3L_LEVEL2_SPAN AURORA_FS_V2_LEVEL2_EXTENT_CAPACITY
#define L3L_MAX_EXTENTS AURORA_FS_V2_LEVEL3_EXTENT_CAPACITY
#define L3L_INODE_SIZE 256u
#define L3L_INODE_FILE 1u

struct l3l_header {
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

struct l3l_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct l3l_node {
    struct l3l_header header;
    struct l3l_entry entries[L3L_CAPACITY];
} __attribute__((packed));

struct l3l_inode_extent {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct l3l_inode {
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
    struct l3l_inode_extent extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct l3l_test_context {
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint8_t inode_block[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint8_t root_block[AURORA_FS_V2_FS_BLOCK_SIZE];
};

static struct l3l_test_context l3l_test;
static uint8_t l3l_bitmap_before[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t l3l_inode_before[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t l3l_root_before[AURORA_FS_V2_FS_BLOCK_SIZE];

_Static_assert(sizeof(struct l3l_header) == L3L_HEADER_SIZE,
               "AuroraFS v2 level-3 limit header size");
_Static_assert(sizeof(struct l3l_entry) == L3L_ENTRY_SIZE,
               "AuroraFS v2 level-3 limit entry size");
_Static_assert(sizeof(struct l3l_node) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 level-3 limit node size");
_Static_assert(sizeof(struct l3l_inode) == L3L_INODE_SIZE,
               "AuroraFS v2 level-3 limit inode size");
_Static_assert(L3L_CAPACITY == 126u,
               "AuroraFS v2 level-3 limit assumes 126 entries");
_Static_assert(L3L_LEVEL_THREE == 3u,
               "AuroraFS v2 maximum root level changed");
_Static_assert(L3L_LEVEL2_SPAN == 2000376ull,
               "AuroraFS v2 level-2 capacity contract changed");
_Static_assert(L3L_MAX_EXTENTS == 252047376ull,
               "AuroraFS v2 level-3 maximum mapping count changed");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source, size_t length) {
    for (size_t i = 0u; i < length; ++i) destination[i] = source[i];
}

static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t length) {
    for (size_t i = 0u; i < length; ++i)
        if (a[i] != b[i]) return false;
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

static uint32_t node_checksum(struct l3l_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t result = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return result;
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
        length != AURORA_FS_V2_FS_BLOCK_SIZE ||
        offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u)
        return false;

    uint64_t block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint8_t *out = buffer;
    zero_bytes(out, AURORA_FS_V2_FS_BLOCK_SIZE);

    if (block == L3L_BITMAP_START) {
        copy_bytes(out, l3l_test.bitmap, AURORA_FS_V2_FS_BLOCK_SIZE);
        return true;
    }
    if (block == L3L_INODE_START) {
        copy_bytes(out, l3l_test.inode_block, AURORA_FS_V2_FS_BLOCK_SIZE);
        return true;
    }
    if (block == L3L_ROOT_BLOCK) {
        copy_bytes(out, l3l_test.root_block, AURORA_FS_V2_FS_BLOCK_SIZE);
        return true;
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
        ((offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE) != 0u)
        return false;

    uint64_t block =
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    const uint8_t *src = buffer;

    if (block == L3L_BITMAP_START) {
        copy_bytes(l3l_test.bitmap, src, AURORA_FS_V2_FS_BLOCK_SIZE);
        return true;
    }
    if (block == L3L_INODE_START) {
        copy_bytes(l3l_test.inode_block, src, AURORA_FS_V2_FS_BLOCK_SIZE);
        return true;
    }
    if (block == L3L_ROOT_BLOCK) {
        copy_bytes(l3l_test.root_block, src, AURORA_FS_V2_FS_BLOCK_SIZE);
        return true;
    }
    return true;
}

static bool sparse_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static void bitmap_set(uint64_t block) {
    l3l_test.bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
}

static void seed_state(void) {
    zero_bytes(&l3l_test, sizeof(l3l_test));

    for (uint64_t block = 0u; block < L3L_DATA_START; ++block) bitmap_set(block);
    bitmap_set(L3L_ROOT_BLOCK);
    bitmap_set(L3L_NEW_DATA);

    struct l3l_node *root = (struct l3l_node *)l3l_test.root_block;
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0u; i < sizeof(magic); ++i) root->header.magic[i] = magic[i];
    root->header.version = 1u;
    root->header.level = L3L_LEVEL_THREE;
    root->header.entry_count = L3L_CAPACITY;
    root->header.generation = 300u;
    root->header.first_logical = 0u;
    root->header.last_logical_exclusive = L3L_MAX_EXTENTS;

    for (uint32_t i = 0u; i < L3L_CAPACITY; ++i) {
        root->entries[i] = (struct l3l_entry){
            .logical_block = (uint64_t)i * L3L_LEVEL2_SPAN,
            .physical_or_child = 64u + i,
            .block_count_or_span = L3L_LEVEL2_SPAN,
            .reserved = 0u
        };
    }
    root->header.checksum = node_checksum(root);

    struct l3l_inode *inode =
        &((struct l3l_inode *)l3l_test.inode_block)[L3L_INODE_INDEX];
    inode->object_id = 2u;
    inode->parent_object_id = 1u;
    inode->size = L3L_MAX_EXTENTS * (uint64_t)AURORA_FS_V2_FS_BLOCK_SIZE;
    inode->allocated_bytes = inode->size;
    inode->generation = 300u;
    inode->extent_tree_root = L3L_ROOT_BLOCK;
    inode->type = L3L_INODE_FILE;
    inode->extent_count = (uint32_t)L3L_MAX_EXTENTS;

    copy_bytes(l3l_bitmap_before, l3l_test.bitmap, AURORA_FS_V2_FS_BLOCK_SIZE);
    copy_bytes(l3l_inode_before, l3l_test.inode_block, AURORA_FS_V2_FS_BLOCK_SIZE);
    copy_bytes(l3l_root_before, l3l_test.root_block, AURORA_FS_V2_FS_BLOCK_SIZE);
}

static bool state_unchanged(void) {
    return bytes_equal(l3l_bitmap_before, l3l_test.bitmap, AURORA_FS_V2_FS_BLOCK_SIZE) &&
        bytes_equal(l3l_inode_before, l3l_test.inode_block, AURORA_FS_V2_FS_BLOCK_SIZE) &&
        bytes_equal(l3l_root_before, l3l_test.root_block, AURORA_FS_V2_FS_BLOCK_SIZE);
}

static bool run_test(uint32_t block_size) {
    seed_state();

    uint64_t bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        (uint64_t)L3L_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct aurora_block_device device = {
        .name = "aurorafs-v2-level3-capacity-limit-test",
        .block_size = block_size,
        .block_count = bytes / block_size,
        .read_only = false,
        .context = &l3l_test,
        .read_blocks = sparse_read,
        .write_blocks = sparse_write,
        .flush = sparse_flush
    };
    struct aurora_fs_v2_format_geometry geometry = {
        .base_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES,
        .total_fs_blocks = L3L_TOTAL_BLOCKS,
        .bitmap_start = L3L_BITMAP_START,
        .bitmap_blocks = 1u,
        .inode_start = L3L_INODE_START,
        .inode_blocks = L3L_INODE_BLOCKS,
        .data_start = L3L_DATA_START
    };
    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start))
        return false;

    struct aurora_fs_v2_extent next = {
        .logical_block = L3L_MAX_EXTENTS,
        .physical_block = L3L_NEW_DATA,
        .block_count = 1u
    };

    uint64_t new_root = UINT64_MAX;
    if (aurora_fs_v2_extent_tree_append_level3_full_level2_cow(
            &allocator, L3L_ROOT_BLOCK, &next, &new_root) ||
        new_root != UINT64_MAX || !state_unchanged())
        return false;

    if (aurora_fs_v2_inode_append_level3_full_level2_cow_commit(
            &allocator, &geometry, L3L_INODE_INDEX, L3L_ROOT_BLOCK, &next) ||
        !state_unchanged())
        return false;

    return true;
}

bool aurora_fs_v2_level3_capacity_limit_self_test(void) {
    return run_test(512u) && run_test(4096u);
}
