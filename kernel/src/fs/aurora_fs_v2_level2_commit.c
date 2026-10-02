#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_inode_publish.h>
#include <aurora/block_device.h>

#define COMMIT_HEADER_SIZE 64u
#define COMMIT_ENTRY_SIZE 32u
#define COMMIT_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - COMMIT_HEADER_SIZE) / COMMIT_ENTRY_SIZE)
#define COMMIT_LEVEL_LEAF 0u
#define COMMIT_LEVEL_ONE 1u
#define COMMIT_LEVEL_TWO 2u
#define COMMIT_INODE_SIZE 256u
#define COMMIT_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / COMMIT_INODE_SIZE)
#define COMMIT_INODE_FILE 1u
#define COMMIT_TOTAL_BLOCKS 64u
#define COMMIT_BITMAP_START 1u
#define COMMIT_INODE_START 2u
#define COMMIT_INODE_BLOCKS 1u
#define COMMIT_DATA_START 8u
#define COMMIT_OLD_LEAF 12u
#define COMMIT_OLD_LEVEL1 13u
#define COMMIT_OLD_LEVEL2 14u
#define COMMIT_DATA0 40u
#define COMMIT_DATA1 41u

struct commit_node_header {
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

struct commit_node_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct commit_node {
    struct commit_node_header header;
    struct commit_node_entry entries[COMMIT_CAPACITY];
} __attribute__((packed));

struct commit_inode_extent {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct commit_inode {
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
    struct commit_inode_extent extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

static uint8_t commit_storage[(COMMIT_TOTAL_BLOCKS + 1u) * AURORA_FS_V2_FS_BLOCK_SIZE];

_Static_assert(sizeof(struct commit_node_header) == COMMIT_HEADER_SIZE,
               "AuroraFS v2 level-2 commit header size");
_Static_assert(sizeof(struct commit_node_entry) == COMMIT_ENTRY_SIZE,
               "AuroraFS v2 level-2 commit entry size");
_Static_assert(sizeof(struct commit_node) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 level-2 commit node size");
_Static_assert(sizeof(struct commit_inode) == COMMIT_INODE_SIZE,
               "AuroraFS v2 level-2 commit inode size");

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

static uint32_t node_checksum(struct commit_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t result = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return result;
}

static uint8_t *fs_block_ptr(uint64_t fs_block) {
    if (fs_block >= COMMIT_TOTAL_BLOCKS) {
        return NULL;
    }
    uint64_t offset = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        fs_block * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset + AURORA_FS_V2_FS_BLOCK_SIZE > sizeof(commit_storage)) {
        return NULL;
    }
    return &commit_storage[offset];
}

static bool device_read(struct aurora_block_device *device, uint64_t lba,
                        uint32_t block_count, void *buffer) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }
    uint64_t offset;
    uint64_t length;
    if (!mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > sizeof(commit_storage) || length > sizeof(commit_storage) - offset) {
        return false;
    }
    uint8_t *out = buffer;
    for (uint64_t i = 0u; i < length; ++i) {
        out[i] = commit_storage[offset + i];
    }
    return true;
}

static bool device_write(struct aurora_block_device *device, uint64_t lba,
                         uint32_t block_count, const void *buffer) {
    if (device == NULL || buffer == NULL || device->read_only || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }
    uint64_t offset;
    uint64_t length;
    if (!mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > sizeof(commit_storage) || length > sizeof(commit_storage) - offset) {
        return false;
    }
    const uint8_t *in = buffer;
    for (uint64_t i = 0u; i < length; ++i) {
        commit_storage[offset + i] = in[i];
    }
    return true;
}

static bool device_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static void bitmap_set(uint64_t block) {
    uint8_t *bitmap = fs_block_ptr(COMMIT_BITMAP_START);
    if (bitmap != NULL) {
        bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
    }
}

static void init_node(struct commit_node *node, uint16_t level,
                      uint64_t first, uint64_t last) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    zero_bytes(node, sizeof(*node));
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        node->header.magic[i] = magic[i];
    }
    node->header.version = 1u;
    node->header.level = level;
    node->header.entry_count = 1u;
    node->header.generation = 10u;
    node->header.first_logical = first;
    node->header.last_logical_exclusive = last;
}

static bool store_node(uint64_t block, struct commit_node *node) {
    uint8_t *dst = fs_block_ptr(block);
    if (dst == NULL || node == NULL) {
        return false;
    }
    node->header.checksum = node_checksum(node);
    const uint8_t *src = (const uint8_t *)node;
    for (size_t i = 0u; i < sizeof(*node); ++i) {
        dst[i] = src[i];
    }
    return true;
}

static bool store_inode(uint64_t inode_index, const struct commit_inode *inode) {
    if (inode == NULL || inode_index >= COMMIT_INODES_PER_BLOCK) {
        return false;
    }
    uint8_t *block = fs_block_ptr(COMMIT_INODE_START);
    if (block == NULL) {
        return false;
    }
    ((struct commit_inode *)block)[inode_index] = *inode;
    return true;
}

static bool read_inode(uint64_t inode_index, struct commit_inode *inode) {
    if (inode == NULL || inode_index >= COMMIT_INODES_PER_BLOCK) {
        return false;
    }
    uint8_t *block = fs_block_ptr(COMMIT_INODE_START);
    if (block == NULL) {
        return false;
    }
    *inode = ((const struct commit_inode *)block)[inode_index];
    return true;
}

bool aurora_fs_v2_inode_append_level2_cow_commit(
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
    if (!aurora_fs_v2_extent_tree_append_level2_cow(
            allocator, expected_old_root, extent, &new_root)) {
        return false;
    }
    return aurora_fs_v2_inode_publish_extent_root_cow(
        allocator, geometry, inode_index, expected_old_root, new_root, extent);
}

static bool run_test(uint32_t block_size) {
    zero_bytes(commit_storage, sizeof(commit_storage));

    uint64_t bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        (uint64_t)COMMIT_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct aurora_block_device device = {
        .name = "aurorafs-v2-level2-commit-test",
        .block_size = block_size,
        .block_count = bytes / block_size,
        .read_only = false,
        .context = NULL,
        .read_blocks = device_read,
        .write_blocks = device_write,
        .flush = device_flush
    };
    struct aurora_fs_v2_format_geometry geometry = {
        .base_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES,
        .total_fs_blocks = COMMIT_TOTAL_BLOCKS,
        .bitmap_start = COMMIT_BITMAP_START,
        .bitmap_blocks = 1u,
        .inode_start = COMMIT_INODE_START,
        .inode_blocks = COMMIT_INODE_BLOCKS,
        .data_start = COMMIT_DATA_START
    };
    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) {
        return false;
    }

    for (uint64_t block = 0u; block < COMMIT_DATA_START; ++block) {
        bitmap_set(block);
    }
    bitmap_set(COMMIT_OLD_LEAF);
    bitmap_set(COMMIT_OLD_LEVEL1);
    bitmap_set(COMMIT_OLD_LEVEL2);
    bitmap_set(COMMIT_DATA0);
    bitmap_set(COMMIT_DATA1);

    struct commit_node leaf;
    struct commit_node level1;
    struct commit_node level2;
    init_node(&leaf, COMMIT_LEVEL_LEAF, 0u, 1u);
    leaf.entries[0] = (struct commit_node_entry){0u, COMMIT_DATA0, 1u, 0u};
    init_node(&level1, COMMIT_LEVEL_ONE, 0u, 1u);
    level1.entries[0] = (struct commit_node_entry){0u, COMMIT_OLD_LEAF, 1u, 0u};
    init_node(&level2, COMMIT_LEVEL_TWO, 0u, 1u);
    level2.entries[0] = (struct commit_node_entry){0u, COMMIT_OLD_LEVEL1, 1u, 0u};
    if (!store_node(COMMIT_OLD_LEAF, &leaf) ||
        !store_node(COMMIT_OLD_LEVEL1, &level1) ||
        !store_node(COMMIT_OLD_LEVEL2, &level2)) {
        return false;
    }

    struct commit_inode inode;
    zero_bytes(&inode, sizeof(inode));
    inode.object_id = 2u;
    inode.parent_object_id = 1u;
    inode.size = AURORA_FS_V2_FS_BLOCK_SIZE;
    inode.allocated_bytes = AURORA_FS_V2_FS_BLOCK_SIZE;
    inode.generation = 10u;
    inode.extent_tree_root = COMMIT_OLD_LEVEL2;
    inode.type = COMMIT_INODE_FILE;
    inode.extent_count = 1u;
    if (!store_inode(1u, &inode)) {
        return false;
    }

    struct aurora_fs_v2_extent next = {
        .logical_block = 1u,
        .physical_block = COMMIT_DATA1,
        .block_count = 1u
    };
    if (!aurora_fs_v2_inode_append_level2_cow_commit(
            &allocator, &geometry, 1u, COMMIT_OLD_LEVEL2, &next)) {
        return false;
    }

    struct aurora_fs_v2_allocator reopened;
    struct commit_inode persisted;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start) ||
        !read_inode(1u, &persisted) ||
        persisted.extent_tree_root == COMMIT_OLD_LEVEL2 ||
        persisted.extent_count != 2u ||
        persisted.size != 2u * AURORA_FS_V2_FS_BLOCK_SIZE ||
        persisted.allocated_bytes != 2u * AURORA_FS_V2_FS_BLOCK_SIZE ||
        persisted.generation != 11u) {
        return false;
    }

    uint64_t physical = 0u;
    uint64_t contiguous = 0u;
    return aurora_fs_v2_inode_extent_lookup_unified(
               &reopened, &geometry, 1u, 1u, &physical, &contiguous) &&
        physical == COMMIT_DATA1 && contiguous == 1u;
}

bool aurora_fs_v2_level2_commit_self_test(void) {
    return run_test(512u) && run_test(4096u);
}
