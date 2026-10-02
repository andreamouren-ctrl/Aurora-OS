#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define U_MAGIC_0 'A'
#define U_MAGIC_1 'U'
#define U_MAGIC_2 'R'
#define U_MAGIC_3 'E'
#define U_MAGIC_4 'X'
#define U_MAGIC_5 'T'
#define U_MAGIC_6 '2'
#define U_MAGIC_7 '\0'
#define U_VERSION 1u
#define U_HEADER_SIZE 64u
#define U_ENTRY_SIZE 32u
#define U_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - U_HEADER_SIZE) / U_ENTRY_SIZE)
#define U_LEVEL_LEAF 0u
#define U_LEVEL_ONE 1u
#define U_LEVEL_TWO 2u
#define U_TOTAL_FS_BLOCKS 64u
#define U_INODE_BLOCK 2u
#define U_LEAF_BLOCK 10u
#define U_LEVEL1_BLOCK 11u
#define U_LEVEL2_BLOCK 12u
#define U_DATA_BLOCK 20u
#define U_LOGICAL_BLOCK 7u
#define U_INODE_INDEX 0u
#define U_INODE_FILE 1u
#define U_STORAGE_BYTES ((U_TOTAL_FS_BLOCKS + 1u) * AURORA_FS_V2_FS_BLOCK_SIZE)

struct u_header {
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

struct u_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct u_node {
    struct u_header header;
    struct u_entry entries[U_CAPACITY];
} __attribute__((packed));

struct u_inode_extent {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct u_inode {
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
    struct u_inode_extent extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

static uint8_t u_storage[U_STORAGE_BYTES];

_Static_assert(sizeof(struct u_header) == U_HEADER_SIZE,
               "AuroraFS v2 unified lookup header must remain 64 bytes");
_Static_assert(sizeof(struct u_entry) == U_ENTRY_SIZE,
               "AuroraFS v2 unified lookup entry must remain 32 bytes");
_Static_assert(sizeof(struct u_node) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 unified lookup node must fill one filesystem block");
_Static_assert(sizeof(struct u_inode) == 256u,
               "AuroraFS v2 unified lookup inode must remain 256 bytes");

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

static uint32_t node_checksum(struct u_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return checksum;
}

static void init_node(
    struct u_node *node,
    uint16_t level,
    uint64_t first,
    uint64_t last
) {
    zero_bytes(node, sizeof(*node));
    static const uint8_t magic[8] = {
        U_MAGIC_0, U_MAGIC_1, U_MAGIC_2, U_MAGIC_3,
        U_MAGIC_4, U_MAGIC_5, U_MAGIC_6, U_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        node->header.magic[i] = magic[i];
    }
    node->header.version = U_VERSION;
    node->header.level = level;
    node->header.entry_count = 1u;
    node->header.generation = 1u;
    node->header.first_logical = first;
    node->header.last_logical_exclusive = last;
}

static bool test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }
    uint64_t offset;
    uint64_t length;
    if (!mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > sizeof(u_storage) || length > sizeof(u_storage) - offset) {
        return false;
    }
    uint8_t *out = buffer;
    for (uint64_t i = 0u; i < length; ++i) {
        out[i] = u_storage[offset + i];
    }
    return true;
}

static bool test_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    if (device == NULL || buffer == NULL || device->read_only || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }
    uint64_t offset;
    uint64_t length;
    if (!mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > sizeof(u_storage) || length > sizeof(u_storage) - offset) {
        return false;
    }
    const uint8_t *in = buffer;
    for (uint64_t i = 0u; i < length; ++i) {
        u_storage[offset + i] = in[i];
    }
    return true;
}

static bool test_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static uint8_t *fs_block_ptr(uint64_t fs_block) {
    uint64_t offset = AURORA_FS_V2_DEFAULT_BASE_BYTES +
        fs_block * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset > sizeof(u_storage) ||
        AURORA_FS_V2_FS_BLOCK_SIZE > sizeof(u_storage) - offset) {
        return NULL;
    }
    return &u_storage[offset];
}

static bool seed_tree_and_inode(void) {
    struct u_node leaf;
    struct u_node level1;
    struct u_node level2;
    init_node(&leaf, U_LEVEL_LEAF, U_LOGICAL_BLOCK, U_LOGICAL_BLOCK + 1u);
    leaf.entries[0] = (struct u_entry){
        U_LOGICAL_BLOCK, U_DATA_BLOCK, 1u, 0u
    };
    leaf.header.checksum = node_checksum(&leaf);

    init_node(&level1, U_LEVEL_ONE, U_LOGICAL_BLOCK, U_LOGICAL_BLOCK + 1u);
    level1.entries[0] = (struct u_entry){
        U_LOGICAL_BLOCK, U_LEAF_BLOCK, 1u, 0u
    };
    level1.header.checksum = node_checksum(&level1);

    init_node(&level2, U_LEVEL_TWO, U_LOGICAL_BLOCK, U_LOGICAL_BLOCK + 1u);
    level2.entries[0] = (struct u_entry){
        U_LOGICAL_BLOCK, U_LEVEL1_BLOCK, 1u, 0u
    };
    level2.header.checksum = node_checksum(&level2);

    uint8_t *leaf_dst = fs_block_ptr(U_LEAF_BLOCK);
    uint8_t *level1_dst = fs_block_ptr(U_LEVEL1_BLOCK);
    uint8_t *level2_dst = fs_block_ptr(U_LEVEL2_BLOCK);
    uint8_t *inode_dst = fs_block_ptr(U_INODE_BLOCK);
    if (leaf_dst == NULL || level1_dst == NULL || level2_dst == NULL || inode_dst == NULL) {
        return false;
    }
    for (size_t i = 0u; i < sizeof(leaf); ++i) {
        leaf_dst[i] = ((const uint8_t *)&leaf)[i];
        level1_dst[i] = ((const uint8_t *)&level1)[i];
        level2_dst[i] = ((const uint8_t *)&level2)[i];
    }

    zero_bytes(inode_dst, AURORA_FS_V2_FS_BLOCK_SIZE);
    struct u_inode *inode = (struct u_inode *)inode_dst;
    inode[U_INODE_INDEX].object_id = 2u;
    inode[U_INODE_INDEX].parent_object_id = 1u;
    inode[U_INODE_INDEX].size = AURORA_FS_V2_FS_BLOCK_SIZE;
    inode[U_INODE_INDEX].allocated_bytes = AURORA_FS_V2_FS_BLOCK_SIZE;
    inode[U_INODE_INDEX].generation = 1u;
    inode[U_INODE_INDEX].extent_tree_root = U_LEVEL2_BLOCK;
    inode[U_INODE_INDEX].type = U_INODE_FILE;
    inode[U_INODE_INDEX].extent_count = 1u;
    return true;
}

bool aurora_fs_v2_extent_tree_lookup_unified(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t root_block,
    uint64_t logical_block,
    uint64_t *out_physical_block,
    uint64_t *out_contiguous_blocks
) {
    return aurora_fs_v2_extent_tree_lookup_level2(
        allocator,
        root_block,
        logical_block,
        out_physical_block,
        out_contiguous_blocks);
}

bool aurora_fs_v2_inode_extent_lookup_unified(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t logical_block,
    uint64_t *out_physical_block,
    uint64_t *out_contiguous_blocks
) {
    return aurora_fs_v2_inode_extent_lookup_level2(
        allocator,
        geometry,
        inode_index,
        logical_block,
        out_physical_block,
        out_contiguous_blocks);
}

static bool run_geometry(uint32_t block_size) {
    zero_bytes(u_storage, sizeof(u_storage));
    if (!seed_tree_and_inode()) {
        return false;
    }

    struct aurora_block_device device = {
        .name = "aurorafs-v2-unified-lookup-test",
        .block_size = block_size,
        .block_count = sizeof(u_storage) / block_size,
        .read_only = false,
        .context = NULL,
        .read_blocks = test_read,
        .write_blocks = test_write,
        .flush = test_flush
    };
    struct aurora_fs_v2_format_geometry geometry = {
        .base_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES,
        .total_fs_blocks = U_TOTAL_FS_BLOCKS,
        .bitmap_start = 1u,
        .bitmap_blocks = 1u,
        .inode_start = U_INODE_BLOCK,
        .inode_blocks = 1u,
        .data_start = 4u
    };
    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator,
            &device,
            geometry.base_bytes,
            geometry.total_fs_blocks,
            geometry.bitmap_start,
            geometry.bitmap_blocks,
            geometry.data_start)) {
        return false;
    }

    uint64_t physical = 0u;
    uint64_t contiguous = 0u;
    if (!aurora_fs_v2_extent_tree_lookup_unified(
            &allocator,
            U_LEVEL2_BLOCK,
            U_LOGICAL_BLOCK,
            &physical,
            &contiguous) ||
        physical != U_DATA_BLOCK || contiguous != 1u) {
        return false;
    }

    physical = 0u;
    contiguous = 0u;
    return aurora_fs_v2_inode_extent_lookup_unified(
               &allocator,
               &geometry,
               U_INODE_INDEX,
               U_LOGICAL_BLOCK,
               &physical,
               &contiguous) &&
        physical == U_DATA_BLOCK && contiguous == 1u;
}

bool aurora_fs_v2_unified_lookup_self_test(void) {
    return run_geometry(512u) && run_geometry(4096u);
}
