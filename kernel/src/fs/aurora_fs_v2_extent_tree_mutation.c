#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define V2_EXTENT_NODE_MAGIC_0 'A'
#define V2_EXTENT_NODE_MAGIC_1 'U'
#define V2_EXTENT_NODE_MAGIC_2 'R'
#define V2_EXTENT_NODE_MAGIC_3 'E'
#define V2_EXTENT_NODE_MAGIC_4 'X'
#define V2_EXTENT_NODE_MAGIC_5 'T'
#define V2_EXTENT_NODE_MAGIC_6 '2'
#define V2_EXTENT_NODE_MAGIC_7 '\0'
#define V2_EXTENT_NODE_VERSION 1u
#define V2_EXTENT_NODE_HEADER_SIZE 64u
#define V2_EXTENT_NODE_ENTRY_SIZE 32u
#define V2_EXTENT_NODE_CAPACITY \
    ((AURORA_FS_V2_FS_BLOCK_SIZE - V2_EXTENT_NODE_HEADER_SIZE) / V2_EXTENT_NODE_ENTRY_SIZE)
#define V2_EXTENT_LEVEL_LEAF 0u

struct mutation_extent_node_header_disk {
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

struct mutation_extent_node_entry_disk {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct mutation_extent_node_disk {
    struct mutation_extent_node_header_disk header;
    struct mutation_extent_node_entry_disk entries[V2_EXTENT_NODE_CAPACITY];
} __attribute__((packed));

static struct mutation_extent_node_disk mutation_node;

_Static_assert(sizeof(struct mutation_extent_node_header_disk) == V2_EXTENT_NODE_HEADER_SIZE,
               "AuroraFS v2 extent mutation header must remain 64 bytes");
_Static_assert(sizeof(struct mutation_extent_node_entry_disk) == V2_EXTENT_NODE_ENTRY_SIZE,
               "AuroraFS v2 extent mutation entry must remain 32 bytes");
_Static_assert(sizeof(struct mutation_extent_node_disk) == AURORA_FS_V2_FS_BLOCK_SIZE,
               "AuroraFS v2 extent mutation node must fill one filesystem block");

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

static uint32_t node_checksum(struct mutation_extent_node_disk *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return checksum;
}

static bool magic_valid(const struct mutation_extent_node_disk *node) {
    static const uint8_t magic[8] = {
        V2_EXTENT_NODE_MAGIC_0, V2_EXTENT_NODE_MAGIC_1,
        V2_EXTENT_NODE_MAGIC_2, V2_EXTENT_NODE_MAGIC_3,
        V2_EXTENT_NODE_MAGIC_4, V2_EXTENT_NODE_MAGIC_5,
        V2_EXTENT_NODE_MAGIC_6, V2_EXTENT_NODE_MAGIC_7
    };
    for (size_t i = 0u; i < sizeof(magic); ++i) {
        if (node->header.magic[i] != magic[i]) {
            return false;
        }
    }
    return true;
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

    uint64_t device_blocks = AURORA_FS_V2_FS_BLOCK_SIZE / allocator->device->block_size;
    uint64_t lba = byte_offset / allocator->device->block_size;
    if (device_blocks == 0u || device_blocks > UINT32_MAX ||
        lba >= allocator->device->block_count ||
        device_blocks > allocator->device->block_count - lba) {
        return false;
    }

    *out_lba = lba;
    *out_device_blocks = (uint32_t)device_blocks;
    return true;
}

static bool read_node(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t fs_block,
    struct mutation_extent_node_disk *node
) {
    uint64_t lba;
    uint32_t device_blocks;
    if (node == NULL || !fs_block_geometry(allocator, fs_block, &lba, &device_blocks) ||
        !block_device_read(allocator->device, lba, device_blocks, node) ||
        !magic_valid(node) || node->header.version != V2_EXTENT_NODE_VERSION ||
        node->header.level != V2_EXTENT_LEVEL_LEAF || node->header.entry_count == 0u ||
        node->header.entry_count > V2_EXTENT_NODE_CAPACITY ||
        node->header.last_logical_exclusive <= node->header.first_logical) {
        return false;
    }
    return node->header.checksum == node_checksum(node);
}

static bool write_node(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t fs_block,
    struct mutation_extent_node_disk *node
) {
    uint64_t lba;
    uint32_t device_blocks;
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        node == NULL || !fs_block_geometry(allocator, fs_block, &lba, &device_blocks)) {
        return false;
    }
    node->header.checksum = node_checksum(node);
    return block_device_write(allocator->device, lba, device_blocks, node);
}

bool aurora_fs_v2_extent_tree_clone_append_leaf(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root_block,
    const struct aurora_fs_v2_extent *extent,
    uint64_t *out_new_root_block
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        extent == NULL || out_new_root_block == NULL || extent->block_count == 0u ||
        extent->physical_block < allocator->data_start) {
        return false;
    }

    uint64_t physical_end;
    uint64_t logical_end;
    if (!add_u64(extent->physical_block, extent->block_count, &physical_end) ||
        physical_end > allocator->total_fs_blocks ||
        !add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        !read_node(allocator, old_root_block, &mutation_node) ||
        mutation_node.header.entry_count >= V2_EXTENT_NODE_CAPACITY ||
        extent->logical_block < mutation_node.header.last_logical_exclusive) {
        return false;
    }

    uint16_t slot = mutation_node.header.entry_count;
    mutation_node.entries[slot].logical_block = extent->logical_block;
    mutation_node.entries[slot].physical_or_child = extent->physical_block;
    mutation_node.entries[slot].block_count_or_span = extent->block_count;
    mutation_node.entries[slot].reserved = 0u;
    mutation_node.header.entry_count++;
    mutation_node.header.last_logical_exclusive = logical_end;
    mutation_node.header.generation++;

    uint64_t new_root_block;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_root_block)) {
        return false;
    }

    if (!write_node(allocator, new_root_block, &mutation_node) ||
        !block_device_flush(allocator->device)) {
        (void)aurora_fs_v2_allocator_free_range(allocator, new_root_block, 1u);
        return false;
    }

    *out_new_root_block = new_root_block;
    return true;
}
