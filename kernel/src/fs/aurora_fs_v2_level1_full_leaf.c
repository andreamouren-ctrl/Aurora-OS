#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define FL_HEADER_SIZE 64u
#define FL_ENTRY_SIZE 32u
#define FL_CAPACITY ((AURORA_FS_V2_FS_BLOCK_SIZE - FL_HEADER_SIZE) / FL_ENTRY_SIZE)
#define FL_LEVEL_LEAF 0u
#define FL_LEVEL_ROOT 1u
#define FL_INODE_SIZE 256u
#define FL_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / FL_INODE_SIZE)
#define FL_INODE_FILE 1u
#define FL_TEST_TOTAL_BLOCKS 2048u
#define FL_TEST_BITMAP_START 1u
#define FL_TEST_BITMAP_BLOCKS 1u
#define FL_TEST_INODE_START 2u
#define FL_TEST_INODE_BLOCKS 1u
#define FL_TEST_DATA_START 16u
#define FL_TEST_ROOT 16u
#define FL_TEST_LEAF_A 17u
#define FL_TEST_LEAF_B 18u
#define FL_TEST_FIRST_DATA 256u
#define FL_TEST_SLOT_COUNT 24u

struct fl_header {
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

struct fl_entry {
    uint64_t logical_block;
    uint64_t physical_or_child;
    uint64_t block_count_or_span;
    uint64_t reserved;
} __attribute__((packed));

struct fl_node {
    struct fl_header header;
    struct fl_entry entries[FL_CAPACITY];
} __attribute__((packed));

struct fl_inode_extent {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct fl_inode {
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
    struct fl_inode_extent extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct fl_test_context {
    uint8_t bitmap[AURORA_FS_V2_FS_BLOCK_SIZE];
    uint64_t slot_block[FL_TEST_SLOT_COUNT];
    uint8_t slot_data[FL_TEST_SLOT_COUNT][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static struct fl_node root_io;
static struct fl_node leaf_io;
static uint8_t inode_io[AURORA_FS_V2_FS_BLOCK_SIZE];
static struct fl_test_context test_ctx;

_Static_assert(sizeof(struct fl_header) == FL_HEADER_SIZE, "extent header size");
_Static_assert(sizeof(struct fl_entry) == FL_ENTRY_SIZE, "extent entry size");
_Static_assert(sizeof(struct fl_node) == AURORA_FS_V2_FS_BLOCK_SIZE, "extent node size");
_Static_assert(sizeof(struct fl_inode) == FL_INODE_SIZE, "inode size");
_Static_assert(FL_CAPACITY == 126u, "full-leaf gate assumes 126 entries");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *p = buffer;
    for (size_t i = 0; i < length; ++i) p[i] = 0;
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
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint32_t node_checksum(struct fl_node *node) {
    uint32_t saved = node->header.checksum;
    node->header.checksum = 0;
    uint32_t result = crc32_ieee((const uint8_t *)node, sizeof(*node));
    node->header.checksum = saved;
    return result;
}

static void set_magic(struct fl_node *node) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0; i < 8u; ++i) node->header.magic[i] = magic[i];
}

static bool magic_valid(const struct fl_node *node) {
    static const uint8_t magic[8] = {'A','U','R','E','X','T','2','\0'};
    for (size_t i = 0; i < 8u; ++i) if (node->header.magic[i] != magic[i]) return false;
    return true;
}

static void init_node(struct fl_node *node, uint16_t level, uint16_t count,
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

static bool fs_geometry(const struct aurora_fs_v2_allocator *allocator, uint64_t fs_block,
                        uint64_t *out_lba, uint32_t *out_count) {
    if (allocator == NULL || allocator->device == NULL || out_lba == NULL || out_count == NULL ||
        fs_block >= allocator->total_fs_blocks || allocator->device->block_size == 0u ||
        allocator->device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        AURORA_FS_V2_FS_BLOCK_SIZE % allocator->device->block_size != 0u ||
        allocator->base_bytes % allocator->device->block_size != 0u) return false;
    uint64_t fs_offset, byte_offset;
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

static bool read_node(struct aurora_fs_v2_allocator *allocator, uint64_t block, struct fl_node *node) {
    uint64_t lba; uint32_t count;
    if (node == NULL || !fs_geometry(allocator, block, &lba, &count) ||
        !block_device_read(allocator->device, lba, count, node) || !magic_valid(node) ||
        node->header.version != 1u || node->header.entry_count == 0u ||
        node->header.entry_count > FL_CAPACITY ||
        node->header.last_logical_exclusive <= node->header.first_logical) return false;
    return node->header.checksum == node_checksum(node);
}

static bool write_node(struct aurora_fs_v2_allocator *allocator, uint64_t block, struct fl_node *node) {
    uint64_t lba; uint32_t count;
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        node == NULL || !fs_geometry(allocator, block, &lba, &count)) return false;
    node->header.checksum = node_checksum(node);
    return block_device_write(allocator->device, lba, count, node);
}

static void release_block(struct aurora_fs_v2_allocator *allocator, uint64_t block) {
    if (block != 0u) (void)aurora_fs_v2_allocator_free_range(allocator, block, 1u);
}

bool aurora_fs_v2_extent_tree_append_level1_full_leaf_cow(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t old_root_block,
    const struct aurora_fs_v2_extent *extent,
    uint64_t *out_new_root_block
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        extent == NULL || out_new_root_block == NULL || extent->block_count == 0u ||
        extent->physical_block < allocator->data_start ||
        !read_node(allocator, old_root_block, &root_io) ||
        root_io.header.level != FL_LEVEL_ROOT || root_io.header.entry_count < 2u ||
        root_io.header.entry_count >= FL_CAPACITY) return false;

    uint16_t last_index = root_io.header.entry_count - 1u;
    uint64_t old_leaf_block = root_io.entries[last_index].physical_or_child;
    uint64_t old_child_first = root_io.entries[last_index].logical_block;
    uint64_t old_child_span = root_io.entries[last_index].block_count_or_span;
    uint64_t old_child_end;
    if (old_leaf_block < allocator->data_start || old_child_span == 0u ||
        !add_u64(old_child_first, old_child_span, &old_child_end) ||
        old_child_end != root_io.header.last_logical_exclusive ||
        !read_node(allocator, old_leaf_block, &leaf_io) || leaf_io.header.level != FL_LEVEL_LEAF ||
        leaf_io.header.entry_count != FL_CAPACITY || leaf_io.header.first_logical != old_child_first ||
        leaf_io.header.last_logical_exclusive != old_child_end) return false;

    uint64_t physical_end, logical_end;
    if (!add_u64(extent->physical_block, extent->block_count, &physical_end) ||
        physical_end > allocator->total_fs_blocks ||
        !add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        extent->logical_block < root_io.header.last_logical_exclusive) return false;

    struct fl_node new_leaf_node;
    init_node(&new_leaf_node, FL_LEVEL_LEAF, 1u, root_io.header.generation + 1u,
              extent->logical_block, logical_end);
    new_leaf_node.entries[0].logical_block = extent->logical_block;
    new_leaf_node.entries[0].physical_or_child = extent->physical_block;
    new_leaf_node.entries[0].block_count_or_span = extent->block_count;

    uint64_t new_leaf = 0u, new_root = 0u;
    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_leaf)) return false;
    if (!write_node(allocator, new_leaf, &new_leaf_node) || !block_device_flush(allocator->device)) {
        release_block(allocator, new_leaf);
        return false;
    }

    uint16_t slot = root_io.header.entry_count;
    root_io.entries[slot].logical_block = extent->logical_block;
    root_io.entries[slot].physical_or_child = new_leaf;
    root_io.entries[slot].block_count_or_span = extent->block_count;
    root_io.entries[slot].reserved = 0u;
    root_io.header.entry_count++;
    root_io.header.last_logical_exclusive = logical_end;
    root_io.header.generation++;

    if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &new_root)) {
        release_block(allocator, new_leaf);
        return false;
    }
    if (!write_node(allocator, new_root, &root_io) || !block_device_flush(allocator->device)) {
        release_block(allocator, new_root);
        release_block(allocator, new_leaf);
        return false;
    }
    *out_new_root_block = new_root;
    return true;
}

static bool geometry_valid(const struct aurora_block_device *device,
                           const struct aurora_fs_v2_format_geometry *geometry) {
    return device != NULL && geometry != NULL && device->block_size != 0u &&
        device->block_size <= AURORA_FS_V2_FS_BLOCK_SIZE &&
        AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size == 0u &&
        geometry->base_bytes % device->block_size == 0u && geometry->inode_blocks != 0u &&
        geometry->inode_start < geometry->data_start && geometry->data_start < geometry->total_fs_blocks;
}

static bool geometry_lba(struct aurora_block_device *device,
                         const struct aurora_fs_v2_format_geometry *geometry,
                         uint64_t fs_block, uint64_t *out_lba, uint32_t *out_count) {
    if (!geometry_valid(device, geometry) || out_lba == NULL || out_count == NULL ||
        fs_block >= geometry->total_fs_blocks) return false;
    uint64_t fs_offset, byte_offset;
    if (!mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_offset) ||
        !add_u64(geometry->base_bytes, fs_offset, &byte_offset)) return false;
    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    uint64_t lba = byte_offset / device->block_size;
    if (count == 0u || count > UINT32_MAX || lba >= device->block_count ||
        count > device->block_count - lba) return false;
    *out_lba = lba; *out_count = (uint32_t)count; return true;
}

static bool read_fs_block(struct aurora_block_device *device,
                          const struct aurora_fs_v2_format_geometry *geometry,
                          uint64_t block, void *buffer) {
    uint64_t lba; uint32_t count;
    return buffer != NULL && geometry_lba(device, geometry, block, &lba, &count) &&
        block_device_read(device, lba, count, buffer);
}

static bool write_fs_block(struct aurora_block_device *device,
                           const struct aurora_fs_v2_format_geometry *geometry,
                           uint64_t block, const void *buffer) {
    uint64_t lba; uint32_t count;
    return buffer != NULL && !device->read_only && geometry_lba(device, geometry, block, &lba, &count) &&
        block_device_write(device, lba, count, buffer);
}

static bool inode_position(const struct aurora_fs_v2_format_geometry *geometry, uint64_t index,
                           uint64_t *out_block, uint32_t *out_slot) {
    uint64_t capacity;
    if (geometry == NULL || out_block == NULL || out_slot == NULL ||
        !mul_u64(geometry->inode_blocks, FL_INODES_PER_BLOCK, &capacity) || index >= capacity) return false;
    *out_block = geometry->inode_start + index / FL_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(index % FL_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(struct aurora_block_device *device,
                       const struct aurora_fs_v2_format_geometry *geometry,
                       uint64_t index, struct fl_inode *out_inode) {
    uint64_t block; uint32_t slot;
    if (out_inode == NULL || !inode_position(geometry, index, &block, &slot) ||
        !read_fs_block(device, geometry, block, inode_io)) return false;
    *out_inode = ((const struct fl_inode *)inode_io)[slot];
    return true;
}

static bool write_inode(struct aurora_block_device *device,
                        const struct aurora_fs_v2_format_geometry *geometry,
                        uint64_t index, const struct fl_inode *inode) {
    uint64_t block; uint32_t slot;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_position(geometry, index, &block, &slot) || !read_fs_block(device, geometry, block, inode_io)) return false;
    ((struct fl_inode *)inode_io)[slot] = *inode;
    return write_fs_block(device, geometry, block, inode_io) && block_device_flush(device);
}

bool aurora_fs_v2_inode_extent_append_level1_full_leaf_cow(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct aurora_fs_v2_extent *extent
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        !geometry_valid(allocator->device, geometry) || extent == NULL || extent->block_count == 0u) return false;
    uint64_t physical_end, logical_end, logical_bytes, allocated_add;
    if (!add_u64(extent->physical_block, extent->block_count, &physical_end) ||
        physical_end > allocator->total_fs_blocks ||
        !add_u64(extent->logical_block, extent->block_count, &logical_end) ||
        !mul_u64(logical_end, AURORA_FS_V2_FS_BLOCK_SIZE, &logical_bytes) ||
        !mul_u64(extent->block_count, AURORA_FS_V2_FS_BLOCK_SIZE, &allocated_add)) return false;
    struct fl_inode inode;
    if (!read_inode(allocator->device, geometry, inode_index, &inode) || inode.object_id == 0u ||
        inode.type != FL_INODE_FILE || inode.extent_tree_root == 0u ||
        inode.extent_count < (2u * FL_CAPACITY) || inode.extent_count == UINT32_MAX ||
        allocated_add > UINT64_MAX - inode.allocated_bytes) return false;
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_append_level1_full_leaf_cow(
            allocator, inode.extent_tree_root, extent, &new_root)) return false;
    inode.extent_tree_root = new_root;
    inode.extent_count++;
    inode.allocated_bytes += allocated_add;
    if (logical_bytes > inode.size) inode.size = logical_bytes;
    inode.generation++;
    return write_inode(allocator->device, geometry, inode_index, &inode);
}

static void bitmap_set(uint64_t block) {
    test_ctx.bitmap[block >> 3] |= (uint8_t)(1u << (block & 7u));
}

static int slot_for(uint64_t block, bool create) {
    for (uint32_t i = 0; i < FL_TEST_SLOT_COUNT; ++i) if (test_ctx.slot_block[i] == block) return (int)i;
    if (!create) return -1;
    for (uint32_t i = 0; i < FL_TEST_SLOT_COUNT; ++i) {
        if (test_ctx.slot_block[i] == UINT64_MAX) {
            test_ctx.slot_block[i] = block;
            zero_bytes(test_ctx.slot_data[i], AURORA_FS_V2_FS_BLOCK_SIZE);
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
    uint8_t *out = buffer; zero_bytes(out, (size_t)length);
    uint64_t bitmap_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES + FL_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset) {
        for (uint32_t i = 0; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) out[i] = test_ctx.bitmap[i];
        return true;
    }
    if (offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE != 0u) return false;
    uint64_t block = (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    int slot = slot_for(block, false);
    if (slot < 0) return true;
    for (uint32_t i = 0; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) out[i] = test_ctx.slot_data[(uint32_t)slot][i];
    return true;
}

static bool sparse_write(struct aurora_block_device *device, uint64_t lba, uint32_t count, const void *buffer) {
    uint64_t offset, length;
    if (buffer == NULL || device == NULL || device->read_only ||
        !transfer_geometry(device, lba, count, &offset, &length) || length != AURORA_FS_V2_FS_BLOCK_SIZE) return false;
    const uint8_t *src = buffer;
    uint64_t bitmap_offset = AURORA_FS_V2_DEFAULT_BASE_BYTES + FL_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    if (offset == bitmap_offset) {
        for (uint32_t i = 0; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) test_ctx.bitmap[i] = src[i];
        return true;
    }
    if (offset < AURORA_FS_V2_DEFAULT_BASE_BYTES ||
        (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) % AURORA_FS_V2_FS_BLOCK_SIZE != 0u) return false;
    uint64_t block = (offset - AURORA_FS_V2_DEFAULT_BASE_BYTES) / AURORA_FS_V2_FS_BLOCK_SIZE;
    int slot = slot_for(block, true); if (slot < 0) return false;
    for (uint32_t i = 0; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) test_ctx.slot_data[(uint32_t)slot][i] = src[i];
    return true;
}

static bool sparse_flush(struct aurora_block_device *device) { return device != NULL; }

static bool store_seed(uint64_t block, struct fl_node *node) {
    int slot = slot_for(block, true); if (slot < 0) return false;
    node->header.checksum = node_checksum(node);
    const uint8_t *src = (const uint8_t *)node;
    for (uint32_t i = 0; i < AURORA_FS_V2_FS_BLOCK_SIZE; ++i) test_ctx.slot_data[(uint32_t)slot][i] = src[i];
    return true;
}

static bool seed_full_tree(void) {
    struct fl_node a, b, root;
    init_node(&a, FL_LEVEL_LEAF, FL_CAPACITY, 40u, 0u, FL_CAPACITY);
    init_node(&b, FL_LEVEL_LEAF, FL_CAPACITY, 40u, FL_CAPACITY, 2u * FL_CAPACITY);
    for (uint32_t i = 0; i < FL_CAPACITY; ++i) {
        a.entries[i].logical_block = i;
        a.entries[i].physical_or_child = FL_TEST_FIRST_DATA + i;
        a.entries[i].block_count_or_span = 1u;
        b.entries[i].logical_block = FL_CAPACITY + i;
        b.entries[i].physical_or_child = FL_TEST_FIRST_DATA + FL_CAPACITY + i;
        b.entries[i].block_count_or_span = 1u;
    }
    init_node(&root, FL_LEVEL_ROOT, 2u, 40u, 0u, 2u * FL_CAPACITY);
    root.entries[0] = (struct fl_entry){0u, FL_TEST_LEAF_A, FL_CAPACITY, 0u};
    root.entries[1] = (struct fl_entry){FL_CAPACITY, FL_TEST_LEAF_B, FL_CAPACITY, 0u};
    return store_seed(FL_TEST_LEAF_A, &a) && store_seed(FL_TEST_LEAF_B, &b) && store_seed(FL_TEST_ROOT, &root);
}

static bool setup_test(uint32_t block_size, struct aurora_block_device *device,
                       struct aurora_fs_v2_format_geometry *geometry,
                       struct aurora_fs_v2_allocator *allocator) {
    zero_bytes(&test_ctx, sizeof(test_ctx));
    for (uint32_t i = 0; i < FL_TEST_SLOT_COUNT; ++i) test_ctx.slot_block[i] = UINT64_MAX;
    for (uint64_t b = 0; b < FL_TEST_DATA_START; ++b) bitmap_set(b);
    bitmap_set(FL_TEST_ROOT); bitmap_set(FL_TEST_LEAF_A); bitmap_set(FL_TEST_LEAF_B);
    for (uint64_t b = FL_TEST_FIRST_DATA; b < FL_TEST_FIRST_DATA + 2u * FL_CAPACITY + 1u; ++b) bitmap_set(b);
    uint64_t bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES + FL_TEST_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    *device = (struct aurora_block_device){
        .name="aurorafs-v2-level1-full-leaf-test", .block_size=block_size,
        .block_count=bytes / block_size, .read_only=false, .context=&test_ctx,
        .read_blocks=sparse_read, .write_blocks=sparse_write, .flush=sparse_flush};
    *geometry = (struct aurora_fs_v2_format_geometry){
        .base_bytes=AURORA_FS_V2_DEFAULT_BASE_BYTES, .total_fs_blocks=FL_TEST_TOTAL_BLOCKS,
        .bitmap_start=FL_TEST_BITMAP_START, .bitmap_blocks=FL_TEST_BITMAP_BLOCKS,
        .inode_start=FL_TEST_INODE_START, .inode_blocks=FL_TEST_INODE_BLOCKS,
        .data_start=FL_TEST_DATA_START};
    return aurora_fs_v2_allocator_init(allocator, device, geometry->base_bytes,
        geometry->total_fs_blocks, geometry->bitmap_start, geometry->bitmap_blocks,
        geometry->data_start) && seed_full_tree();
}

static bool run_tree(uint32_t block_size) {
    struct aurora_block_device device; struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!setup_test(block_size, &device, &geometry, &allocator)) return false;
    struct fl_node old_root, old_leaf;
    if (!read_node(&allocator, FL_TEST_ROOT, &old_root) || !read_node(&allocator, FL_TEST_LEAF_B, &old_leaf)) return false;
    struct aurora_fs_v2_extent next = {2u * FL_CAPACITY, FL_TEST_FIRST_DATA + 2u * FL_CAPACITY, 1u};
    uint64_t new_root = 0u;
    if (!aurora_fs_v2_extent_tree_append_level1_full_leaf_cow(&allocator, FL_TEST_ROOT, &next, &new_root) ||
        new_root == FL_TEST_ROOT) return false;
    for (uint64_t logical = 0; logical < 2u * FL_CAPACITY + 1u; ++logical) {
        uint64_t physical=0, contiguous=0;
        if (!aurora_fs_v2_extent_tree_lookup(&allocator, new_root, logical, &physical, &contiguous) ||
            physical != FL_TEST_FIRST_DATA + logical || contiguous != 1u) return false;
    }
    struct fl_node root_after, leaf_after, new_root_node;
    if (!read_node(&allocator, FL_TEST_ROOT, &root_after) || !read_node(&allocator, FL_TEST_LEAF_B, &leaf_after) ||
        !read_node(&allocator, new_root, &new_root_node) || new_root_node.header.entry_count != 3u ||
        root_after.header.generation != old_root.header.generation || root_after.header.entry_count != old_root.header.entry_count ||
        leaf_after.header.generation != old_leaf.header.generation || leaf_after.header.entry_count != old_leaf.header.entry_count) return false;
    return true;
}

static bool run_inode(uint32_t block_size) {
    struct aurora_block_device device; struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
    if (!setup_test(block_size, &device, &geometry, &allocator)) return false;
    struct fl_inode inode; zero_bytes(&inode, sizeof(inode));
    inode.object_id=2u; inode.parent_object_id=1u;
    inode.size=(uint64_t)(2u * FL_CAPACITY) * AURORA_FS_V2_FS_BLOCK_SIZE;
    inode.allocated_bytes=inode.size; inode.generation=50u; inode.extent_tree_root=FL_TEST_ROOT;
    inode.type=FL_INODE_FILE; inode.extent_count=2u * FL_CAPACITY;
    if (!write_inode(&device, &geometry, 1u, &inode)) return false;
    struct aurora_fs_v2_extent next={2u * FL_CAPACITY, FL_TEST_FIRST_DATA + 2u * FL_CAPACITY, 1u};
    if (!aurora_fs_v2_inode_extent_append_level1_full_leaf_cow(&allocator, &geometry, 1u, &next)) return false;
    struct fl_inode persisted;
    if (!read_inode(&device, &geometry, 1u, &persisted) || persisted.extent_count != 2u * FL_CAPACITY + 1u ||
        persisted.extent_tree_root == FL_TEST_ROOT || persisted.generation != 51u ||
        persisted.size != (uint64_t)(2u * FL_CAPACITY + 1u) * AURORA_FS_V2_FS_BLOCK_SIZE ||
        persisted.allocated_bytes != persisted.size) return false;
    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(&reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
        geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) return false;
    for (uint64_t logical=0; logical < 2u * FL_CAPACITY + 1u; ++logical) {
        uint64_t physical=0, contiguous=0;
        if (!aurora_fs_v2_inode_extent_lookup(&reopened, &geometry, 1u, logical, &physical, &contiguous) ||
            physical != FL_TEST_FIRST_DATA + logical || contiguous != 1u) return false;
    }
    return true;
}

bool aurora_fs_v2_extent_tree_level1_full_leaf_self_test(void) {
    return run_tree(512u) && run_tree(4096u);
}

bool aurora_fs_v2_inode_level1_full_leaf_self_test(void) {
    return run_inode(512u) && run_inode(4096u);
}
