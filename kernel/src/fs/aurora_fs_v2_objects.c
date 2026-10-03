#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/block_device.h>

#define V2O_INODE_SIZE 256u
#define V2O_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2O_INODE_SIZE)
#define V2O_DIRECTORY_RECORD_SIZE 128u
#define V2O_DIRECTORY_NAME_MAX 108u
#define V2O_TEST_STORAGE_BYTES (2u * 1024u * 1024u)

struct v2o_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2o_inode_disk {
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
    struct v2o_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct v2o_directory_record_disk {
    uint64_t object_id;
    uint32_t type;
    uint16_t name_length;
    uint16_t flags;
    uint32_t checksum;
    uint8_t name[V2O_DIRECTORY_NAME_MAX];
} __attribute__((packed));

struct v2o_test_context {
    uint8_t *storage;
    uint64_t storage_bytes;
};

static uint8_t v2o_io_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2o_directory_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2o_test_storage[V2O_TEST_STORAGE_BYTES];

_Static_assert(sizeof(struct v2o_inode_disk) == V2O_INODE_SIZE,
               "AuroraFS v2 object inode layout drifted");
_Static_assert(sizeof(struct v2o_directory_record_disk) == V2O_DIRECTORY_RECORD_SIZE,
               "AuroraFS v2 directory record layout drifted");

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

static bool object_type_valid(enum aurora_fs_v2_object_type type) {
    return type == AURORA_FS_V2_OBJECT_FILE ||
           type == AURORA_FS_V2_OBJECT_DIRECTORY;
}

static bool geometry_valid(
    const struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry
) {
    return device != NULL && geometry != NULL && device->block_size != 0u &&
        device->block_size <= AURORA_FS_V2_FS_BLOCK_SIZE &&
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) == 0u &&
        (geometry->base_bytes % device->block_size) == 0u &&
        geometry->inode_blocks != 0u &&
        geometry->inode_start < geometry->data_start &&
        geometry->data_start < geometry->total_fs_blocks &&
        geometry->inode_blocks <= geometry->data_start - geometry->inode_start;
}

static bool fs_block_geometry(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_count
) {
    if (!geometry_valid(device, geometry) || out_lba == NULL || out_count == NULL ||
        fs_block >= geometry->total_fs_blocks) return false;

    uint64_t fs_bytes;
    uint64_t byte_offset;
    if (!mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_bytes) ||
        !add_u64(geometry->base_bytes, fs_bytes, &byte_offset)) return false;

    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    uint64_t lba = byte_offset / device->block_size;
    if (count == 0u || count > UINT32_MAX || lba >= device->block_count ||
        count > device->block_count - lba) return false;

    *out_lba = lba;
    *out_count = (uint32_t)count;
    return true;
}

static bool read_fs_block(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    void *buffer
) {
    uint64_t lba;
    uint32_t count;
    return buffer != NULL &&
        fs_block_geometry(device, geometry, fs_block, &lba, &count) &&
        block_device_read(device, lba, count, buffer);
}

static bool write_fs_block(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    const void *buffer
) {
    uint64_t lba;
    uint32_t count;
    return buffer != NULL && device != NULL && !device->read_only &&
        fs_block_geometry(device, geometry, fs_block, &lba, &count) &&
        block_device_write(device, lba, count, buffer);
}

static bool inode_location(
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t *out_block,
    uint32_t *out_slot
) {
    if (geometry == NULL || out_block == NULL || out_slot == NULL ||
        inode_index >= geometry->inode_blocks * V2O_INODES_PER_BLOCK) return false;
    *out_block = geometry->inode_start + inode_index / V2O_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(inode_index % V2O_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct v2o_inode_disk *out_inode
) {
    uint64_t block;
    uint32_t slot;
    if (out_inode == NULL || !inode_location(geometry, inode_index, &block, &slot) ||
        !read_fs_block(device, geometry, block, v2o_io_block)) return false;
    const struct v2o_inode_disk *inodes = (const struct v2o_inode_disk *)v2o_io_block;
    *out_inode = inodes[slot];
    return true;
}

static bool write_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct v2o_inode_disk *inode
) {
    uint64_t block;
    uint32_t slot;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_location(geometry, inode_index, &block, &slot) ||
        !read_fs_block(device, geometry, block, v2o_io_block)) return false;
    struct v2o_inode_disk *inodes = (struct v2o_inode_disk *)v2o_io_block;
    inodes[slot] = *inode;
    return write_fs_block(device, geometry, block, v2o_io_block) &&
        block_device_flush(device);
}

static bool clear_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index
) {
    struct v2o_inode_disk empty;
    zero_bytes(&empty, sizeof(empty));
    return write_inode(device, geometry, inode_index, &empty);
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

static uint32_t directory_record_checksum(struct v2o_directory_record_disk *record) {
    uint32_t saved = record->checksum;
    record->checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)record, sizeof(*record));
    record->checksum = saved;
    return checksum;
}

static bool set_record(
    struct v2o_directory_record_disk *record,
    uint64_t object_id,
    enum aurora_fs_v2_object_type type,
    const char *name
) {
    if (record == NULL || object_id == 0u || !object_type_valid(type) || name == NULL)
        return false;

    size_t length = 0u;
    while (name[length] != '\0') {
        if (length >= V2O_DIRECTORY_NAME_MAX) return false;
        ++length;
    }
    if (length == 0u) return false;

    zero_bytes(record, sizeof(*record));
    record->object_id = object_id;
    record->type = (uint32_t)type;
    record->name_length = (uint16_t)length;
    for (size_t i = 0u; i < length; ++i) record->name[i] = (uint8_t)name[i];
    record->checksum = directory_record_checksum(record);
    return true;
}

static bool record_name_equals(
    const struct v2o_directory_record_disk *record,
    const char *name
) {
    if (record == NULL || name == NULL || record->name_length == 0u ||
        record->name_length > V2O_DIRECTORY_NAME_MAX) return false;
    size_t length = 0u;
    while (name[length] != '\0') ++length;
    if (length != record->name_length) return false;
    for (size_t i = 0u; i < length; ++i)
        if (record->name[i] != (uint8_t)name[i]) return false;
    return true;
}

static bool resolve_inline_block(
    const struct v2o_inode_disk *inode,
    uint64_t logical_block,
    uint64_t *out_physical
) {
    if (inode == NULL || out_physical == NULL ||
        inode->extent_tree_root != 0u ||
        inode->extent_count > AURORA_FS_V2_INLINE_EXTENT_COUNT) return false;

    for (uint32_t i = 0u; i < inode->extent_count; ++i) {
        uint64_t end;
        if (inode->extents[i].block_count == 0u ||
            !add_u64(inode->extents[i].logical_block, inode->extents[i].block_count, &end))
            return false;
        if (logical_block >= inode->extents[i].logical_block && logical_block < end) {
            *out_physical = inode->extents[i].physical_block +
                (logical_block - inode->extents[i].logical_block);
            return true;
        }
    }
    return false;
}

bool aurora_fs_v2_object_init(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t object_id,
    uint64_t parent_object_id,
    enum aurora_fs_v2_object_type type
) {
    if (!geometry_valid(device, geometry) || device->read_only || object_id == 0u ||
        parent_object_id == 0u || !object_type_valid(type)) return false;

    struct v2o_inode_disk current;
    if (!read_inode(device, geometry, inode_index, &current) || current.object_id != 0u)
        return false;

    struct v2o_inode_disk inode;
    zero_bytes(&inode, sizeof(inode));
    inode.object_id = object_id;
    inode.parent_object_id = parent_object_id;
    inode.generation = 1u;
    inode.type = (uint32_t)type;
    return write_inode(device, geometry, inode_index, &inode);
}

bool aurora_fs_v2_directory_lookup_entry(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t directory_inode_index,
    const char *name,
    struct aurora_fs_v2_directory_entry *out_entry
) {
    if (allocator == NULL || allocator->device == NULL || name == NULL || out_entry == NULL)
        return false;

    struct v2o_inode_disk inode;
    if (!read_inode(allocator->device, geometry, directory_inode_index, &inode) ||
        inode.object_id == 0u || inode.type != AURORA_FS_V2_OBJECT_DIRECTORY ||
        (inode.size % V2O_DIRECTORY_RECORD_SIZE) != 0u) return false;

    uint64_t record_count = inode.size / V2O_DIRECTORY_RECORD_SIZE;
    uint64_t current_block = UINT64_MAX;
    for (uint64_t index = 0u; index < record_count; ++index) {
        uint64_t byte_offset = index * V2O_DIRECTORY_RECORD_SIZE;
        uint64_t logical = byte_offset / AURORA_FS_V2_FS_BLOCK_SIZE;
        uint64_t within = byte_offset % AURORA_FS_V2_FS_BLOCK_SIZE;
        uint64_t physical;

        if (inode.extent_tree_root != 0u) {
            uint64_t contiguous;
            if (!aurora_fs_v2_extent_tree_lookup_unified(
                    allocator, inode.extent_tree_root, logical, &physical, &contiguous))
                return false;
        } else if (!resolve_inline_block(&inode, logical, &physical)) {
            return false;
        }

        if (physical != current_block) {
            if (!read_fs_block(allocator->device, geometry, physical, v2o_directory_block))
                return false;
            current_block = physical;
        }

        struct v2o_directory_record_disk *record =
            (struct v2o_directory_record_disk *)(v2o_directory_block + within);
        if (record->object_id == 0u || !object_type_valid((enum aurora_fs_v2_object_type)record->type) ||
            record->name_length == 0u || record->name_length > V2O_DIRECTORY_NAME_MAX ||
            record->checksum != directory_record_checksum(record)) return false;

        if (record_name_equals(record, name)) {
            out_entry->object_id = record->object_id;
            out_entry->type = (enum aurora_fs_v2_object_type)record->type;
            return true;
        }
    }
    return false;
}

bool aurora_fs_v2_directory_append_entry(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t directory_inode_index,
    uint64_t child_object_id,
    enum aurora_fs_v2_object_type child_type,
    const char *name
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        child_object_id == 0u || !object_type_valid(child_type) || name == NULL) return false;

    struct v2o_directory_record_disk candidate;
    if (!set_record(&candidate, child_object_id, child_type, name)) return false;

    struct aurora_fs_v2_directory_entry duplicate;
    if (aurora_fs_v2_directory_lookup_entry(
            allocator, geometry, directory_inode_index, name, &duplicate)) return false;

    struct v2o_inode_disk inode;
    if (!read_inode(allocator->device, geometry, directory_inode_index, &inode) ||
        inode.object_id == 0u || inode.type != AURORA_FS_V2_OBJECT_DIRECTORY ||
        inode.extent_tree_root != 0u ||
        inode.extent_count > AURORA_FS_V2_INLINE_EXTENT_COUNT ||
        (inode.size % V2O_DIRECTORY_RECORD_SIZE) != 0u) return false;

    uint64_t byte_offset = inode.size;
    uint64_t logical = byte_offset / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t within = byte_offset % AURORA_FS_V2_FS_BLOCK_SIZE;
    if (within + V2O_DIRECTORY_RECORD_SIZE > AURORA_FS_V2_FS_BLOCK_SIZE) return false;

    uint64_t physical;
    bool allocated_new = false;
    if (!resolve_inline_block(&inode, logical, &physical)) {
        if (inode.extent_count >= AURORA_FS_V2_INLINE_EXTENT_COUNT) return false;
        if (!aurora_fs_v2_allocator_allocate_range(allocator, 1u, &physical)) return false;
        allocated_new = true;
        zero_bytes(v2o_directory_block, sizeof(v2o_directory_block));
        if (!write_fs_block(allocator->device, geometry, physical, v2o_directory_block) ||
            !block_device_flush(allocator->device)) {
            (void)aurora_fs_v2_allocator_free_range(allocator, physical, 1u);
            return false;
        }
    } else if (!read_fs_block(allocator->device, geometry, physical, v2o_directory_block)) {
        return false;
    }

    struct v2o_directory_record_disk *record =
        (struct v2o_directory_record_disk *)(v2o_directory_block + within);
    if (record->object_id != 0u) {
        if (allocated_new) (void)aurora_fs_v2_allocator_free_range(allocator, physical, 1u);
        return false;
    }
    *record = candidate;

    if (!write_fs_block(allocator->device, geometry, physical, v2o_directory_block) ||
        !block_device_flush(allocator->device)) {
        if (allocated_new) (void)aurora_fs_v2_allocator_free_range(allocator, physical, 1u);
        return false;
    }

    if (allocated_new) {
        uint32_t slot = inode.extent_count;
        inode.extents[slot].logical_block = logical;
        inode.extents[slot].physical_block = physical;
        inode.extents[slot].block_count = 1u;
        inode.extent_count++;
        if (!add_u64(inode.allocated_bytes, AURORA_FS_V2_FS_BLOCK_SIZE, &inode.allocated_bytes)) {
            (void)aurora_fs_v2_allocator_free_range(allocator, physical, 1u);
            return false;
        }
    }

    if (!add_u64(inode.size, V2O_DIRECTORY_RECORD_SIZE, &inode.size)) {
        if (allocated_new) (void)aurora_fs_v2_allocator_free_range(allocator, physical, 1u);
        return false;
    }
    inode.generation++;
    if (!write_inode(allocator->device, geometry, directory_inode_index, &inode)) {
        if (allocated_new) (void)aurora_fs_v2_allocator_free_range(allocator, physical, 1u);
        return false;
    }
    return true;
}

bool aurora_fs_v2_create_child(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    uint64_t child_inode_index,
    uint64_t child_object_id,
    enum aurora_fs_v2_object_type child_type,
    const char *name
) {
    if (allocator == NULL || allocator->device == NULL || name == NULL ||
        !object_type_valid(child_type)) return false;

    struct v2o_inode_disk parent;
    if (!read_inode(allocator->device, geometry, parent_inode_index, &parent) ||
        parent.object_id == 0u || parent.type != AURORA_FS_V2_OBJECT_DIRECTORY) return false;

    if (!aurora_fs_v2_object_init(
            allocator->device, geometry, child_inode_index, child_object_id,
            parent.object_id, child_type)) return false;

    if (!aurora_fs_v2_directory_append_entry(
            allocator, geometry, parent_inode_index, child_object_id, child_type, name)) {
        (void)clear_inode(allocator->device, geometry, child_inode_index);
        return false;
    }
    return true;
}

static bool test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t count,
    void *buffer
) {
    if (device == NULL || buffer == NULL || count == 0u || lba >= device->block_count ||
        (uint64_t)count > device->block_count - lba) return false;
    struct v2o_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset) return false;
    uint8_t *out = buffer;
    for (uint64_t i = 0u; i < length; ++i) out[i] = context->storage[offset + i];
    return true;
}

static bool test_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t count,
    const void *buffer
) {
    if (device == NULL || buffer == NULL || device->read_only || count == 0u ||
        lba >= device->block_count || (uint64_t)count > device->block_count - lba) return false;
    struct v2o_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset) return false;
    const uint8_t *src = buffer;
    for (uint64_t i = 0u; i < length; ++i) context->storage[offset + i] = src[i];
    return true;
}

static bool test_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool run_create_mkdir_test(uint32_t block_size) {
    zero_bytes(v2o_test_storage, sizeof(v2o_test_storage));
    struct v2o_test_context context = {
        .storage = v2o_test_storage,
        .storage_bytes = sizeof(v2o_test_storage)
    };
    struct aurora_block_device device = {
        .name = "aurorafs-v2-create-mkdir-test",
        .block_size = block_size,
        .block_count = sizeof(v2o_test_storage) / block_size,
        .read_only = false,
        .context = &context,
        .read_blocks = test_read,
        .write_blocks = test_write,
        .flush = test_flush
    };

    struct aurora_fs_v2_format_geometry geometry;
    if (!aurora_fs_v2_format_device(
            &device, AURORA_FS_V2_DEFAULT_BASE_BYTES, 0u, &geometry)) return false;

    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) return false;

    if (!aurora_fs_v2_object_init(
            &device, &geometry, 0u, 1u, 1u, AURORA_FS_V2_OBJECT_DIRECTORY) ||
        !aurora_fs_v2_create_child(
            &allocator, &geometry, 0u, 1u, 2u,
            AURORA_FS_V2_OBJECT_DIRECTORY, "docs") ||
        !aurora_fs_v2_create_child(
            &allocator, &geometry, 0u, 2u, 3u,
            AURORA_FS_V2_OBJECT_FILE, "hello.txt") ||
        !aurora_fs_v2_create_child(
            &allocator, &geometry, 1u, 3u, 4u,
            AURORA_FS_V2_OBJECT_FILE, "note.txt")) return false;

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) return false;

    struct aurora_fs_v2_directory_entry entry;
    if (!aurora_fs_v2_directory_lookup_entry(
            &reopened, &geometry, 0u, "docs", &entry) ||
        entry.object_id != 2u || entry.type != AURORA_FS_V2_OBJECT_DIRECTORY) return false;
    if (!aurora_fs_v2_directory_lookup_entry(
            &reopened, &geometry, 0u, "hello.txt", &entry) ||
        entry.object_id != 3u || entry.type != AURORA_FS_V2_OBJECT_FILE) return false;
    if (!aurora_fs_v2_directory_lookup_entry(
            &reopened, &geometry, 1u, "note.txt", &entry) ||
        entry.object_id != 4u || entry.type != AURORA_FS_V2_OBJECT_FILE) return false;

    struct v2o_inode_disk docs;
    struct v2o_inode_disk note;
    if (!read_inode(&device, &geometry, 1u, &docs) ||
        !read_inode(&device, &geometry, 3u, &note) ||
        docs.object_id != 2u || docs.parent_object_id != 1u ||
        docs.type != AURORA_FS_V2_OBJECT_DIRECTORY ||
        note.object_id != 4u || note.parent_object_id != 2u ||
        note.type != AURORA_FS_V2_OBJECT_FILE) return false;

    return true;
}

bool aurora_fs_v2_create_mkdir_self_test(void) {
    return run_create_mkdir_test(512u) && run_create_mkdir_test(4096u);
}
