#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_file_io.h>
#include <aurora/aurora_fs_v2_mutation.h>
#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/block_device.h>

#define V2M_INODE_SIZE 256u
#define V2M_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2M_INODE_SIZE)
#define V2M_DIRECTORY_RECORD_SIZE 128u
#define V2M_DIRECTORY_NAME_MAX 108u
#define V2M_TEST_STORAGE_BYTES (2u * 1024u * 1024u)

struct v2m_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2m_inode_disk {
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
    struct v2m_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct v2m_directory_record_disk {
    uint64_t object_id;
    uint32_t type;
    uint16_t name_length;
    uint16_t flags;
    uint32_t checksum;
    uint8_t name[V2M_DIRECTORY_NAME_MAX];
} __attribute__((packed));

struct v2m_test_context {
    uint8_t *storage;
    uint64_t storage_bytes;
};

static uint8_t v2m_io_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2m_dir_block_a[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2m_dir_block_b[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2m_test_storage[V2M_TEST_STORAGE_BYTES];
static uint8_t v2m_test_payload[6000u];

_Static_assert(sizeof(struct v2m_inode_disk) == V2M_INODE_SIZE,
               "AuroraFS v2 mutation inode layout drifted");
_Static_assert(sizeof(struct v2m_directory_record_disk) == V2M_DIRECTORY_RECORD_SIZE,
               "AuroraFS v2 mutation directory layout drifted");

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

static bool valid_type(uint32_t type) {
    return type == (uint32_t)AURORA_FS_V2_OBJECT_FILE ||
        type == (uint32_t)AURORA_FS_V2_OBJECT_DIRECTORY;
}

static bool geometry_valid(
    const struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry
) {
    return device != NULL && geometry != NULL && device->block_size != 0u &&
        device->block_size <= AURORA_FS_V2_FS_BLOCK_SIZE &&
        (AURORA_FS_V2_FS_BLOCK_SIZE % device->block_size) == 0u &&
        (geometry->base_bytes % device->block_size) == 0u &&
        geometry->inode_blocks != 0u && geometry->inode_start < geometry->data_start &&
        geometry->data_start < geometry->total_fs_blocks;
}

static bool fs_block_geometry(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    uint64_t *out_lba,
    uint32_t *out_count
) {
    uint64_t fs_bytes;
    uint64_t byte_offset;
    if (!geometry_valid(device, geometry) || out_lba == NULL || out_count == NULL ||
        fs_block >= geometry->total_fs_blocks ||
        !mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &fs_bytes) ||
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
    return buffer != NULL && fs_block_geometry(device, geometry, fs_block, &lba, &count) &&
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
    return device != NULL && !device->read_only && buffer != NULL &&
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
        inode_index >= geometry->inode_blocks * V2M_INODES_PER_BLOCK) return false;
    *out_block = geometry->inode_start + inode_index / V2M_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(inode_index % V2M_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct v2m_inode_disk *out_inode
) {
    uint64_t block;
    uint32_t slot;
    if (out_inode == NULL || !inode_location(geometry, inode_index, &block, &slot) ||
        !read_fs_block(device, geometry, block, v2m_io_block)) return false;
    *out_inode = ((const struct v2m_inode_disk *)v2m_io_block)[slot];
    return true;
}

static bool write_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct v2m_inode_disk *inode
) {
    uint64_t block;
    uint32_t slot;
    if (device == NULL || device->read_only || inode == NULL ||
        !inode_location(geometry, inode_index, &block, &slot) ||
        !read_fs_block(device, geometry, block, v2m_io_block)) return false;
    ((struct v2m_inode_disk *)v2m_io_block)[slot] = *inode;
    return write_fs_block(device, geometry, block, v2m_io_block) && block_device_flush(device);
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

static uint32_t record_checksum(struct v2m_directory_record_disk *record) {
    uint32_t saved = record->checksum;
    record->checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)record, sizeof(*record));
    record->checksum = saved;
    return checksum;
}

static bool name_valid(const char *name, size_t *out_length) {
    if (name == NULL) return false;
    size_t length = 0u;
    while (name[length] != '\0') {
        if (length >= V2M_DIRECTORY_NAME_MAX) return false;
        ++length;
    }
    if (length == 0u) return false;
    if (out_length != NULL) *out_length = length;
    return true;
}

static bool record_name_equals(const struct v2m_directory_record_disk *record, const char *name) {
    size_t length;
    if (record == NULL || !name_valid(name, &length) || length != record->name_length) return false;
    for (size_t i = 0u; i < length; ++i)
        if (record->name[i] != (uint8_t)name[i]) return false;
    return true;
}

static bool make_record(
    struct v2m_directory_record_disk *record,
    uint64_t object_id,
    uint32_t type,
    const char *name
) {
    size_t length;
    if (record == NULL || object_id == 0u || !valid_type(type) || !name_valid(name, &length))
        return false;
    zero_bytes(record, sizeof(*record));
    record->object_id = object_id;
    record->type = type;
    record->name_length = (uint16_t)length;
    for (size_t i = 0u; i < length; ++i) record->name[i] = (uint8_t)name[i];
    record->checksum = record_checksum(record);
    return true;
}

static bool inline_physical(
    const struct v2m_inode_disk *inode,
    uint64_t logical,
    uint64_t *out_physical
) {
    if (inode == NULL || out_physical == NULL || inode->extent_tree_root != 0u ||
        inode->extent_count > AURORA_FS_V2_INLINE_EXTENT_COUNT) return false;
    for (uint32_t i = 0u; i < inode->extent_count; ++i) {
        uint64_t end;
        if (inode->extents[i].block_count == 0u ||
            !add_u64(inode->extents[i].logical_block, inode->extents[i].block_count, &end))
            return false;
        if (logical >= inode->extents[i].logical_block && logical < end) {
            *out_physical = inode->extents[i].physical_block +
                (logical - inode->extents[i].logical_block);
            return true;
        }
    }
    return false;
}

static bool load_record(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    const struct v2m_inode_disk *directory,
    uint64_t index,
    uint8_t *block_buffer,
    uint64_t *out_physical,
    struct v2m_directory_record_disk **out_record
) {
    if (device == NULL || geometry == NULL || directory == NULL || block_buffer == NULL ||
        out_physical == NULL || out_record == NULL || directory->extent_tree_root != 0u ||
        index >= directory->size / V2M_DIRECTORY_RECORD_SIZE) return false;
    uint64_t byte_offset = index * V2M_DIRECTORY_RECORD_SIZE;
    uint64_t logical = byte_offset / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t within = byte_offset % AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t physical;
    if (within + V2M_DIRECTORY_RECORD_SIZE > AURORA_FS_V2_FS_BLOCK_SIZE ||
        !inline_physical(directory, logical, &physical) ||
        !read_fs_block(device, geometry, physical, block_buffer)) return false;
    *out_physical = physical;
    *out_record = (struct v2m_directory_record_disk *)(block_buffer + within);
    return true;
}

static bool find_record(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    const struct v2m_inode_disk *directory,
    const char *name,
    uint64_t *out_index,
    struct v2m_directory_record_disk *out_record
) {
    if (directory == NULL || name == NULL || out_index == NULL || out_record == NULL ||
        directory->type != AURORA_FS_V2_OBJECT_DIRECTORY ||
        directory->extent_tree_root != 0u ||
        (directory->size % V2M_DIRECTORY_RECORD_SIZE) != 0u) return false;
    uint64_t count = directory->size / V2M_DIRECTORY_RECORD_SIZE;
    for (uint64_t index = 0u; index < count; ++index) {
        uint64_t physical;
        struct v2m_directory_record_disk *record;
        if (!load_record(device, geometry, directory, index, v2m_dir_block_a,
                         &physical, &record) || record->object_id == 0u ||
            !valid_type(record->type) || record->name_length == 0u ||
            record->name_length > V2M_DIRECTORY_NAME_MAX ||
            record->checksum != record_checksum(record)) return false;
        if (record_name_equals(record, name)) {
            *out_index = index;
            *out_record = *record;
            return true;
        }
    }
    return false;
}

bool aurora_fs_v2_rename_child(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    const char *old_name,
    const char *new_name
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        !name_valid(old_name, NULL) || !name_valid(new_name, NULL)) return false;

    struct v2m_inode_disk parent;
    if (!read_inode(allocator->device, geometry, parent_inode_index, &parent) ||
        parent.object_id == 0u || parent.type != AURORA_FS_V2_OBJECT_DIRECTORY ||
        parent.extent_tree_root != 0u) return false;

    uint64_t source_index;
    struct v2m_directory_record_disk source;
    if (!find_record(allocator->device, geometry, &parent, old_name, &source_index, &source))
        return false;
    if (record_name_equals(&source, new_name)) return true;

    uint64_t duplicate_index;
    struct v2m_directory_record_disk duplicate;
    if (find_record(allocator->device, geometry, &parent, new_name, &duplicate_index, &duplicate))
        return false;

    uint64_t physical;
    struct v2m_directory_record_disk *record;
    if (!load_record(allocator->device, geometry, &parent, source_index, v2m_dir_block_a,
                     &physical, &record) ||
        !make_record(record, source.object_id, source.type, new_name) ||
        !write_fs_block(allocator->device, geometry, physical, v2m_dir_block_a) ||
        !block_device_flush(allocator->device)) return false;

    parent.generation++;
    return write_inode(allocator->device, geometry, parent_inode_index, &parent);
}

static bool compact_remove_record(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    struct v2m_inode_disk *parent,
    uint64_t remove_index
) {
    uint64_t count = parent->size / V2M_DIRECTORY_RECORD_SIZE;
    if (count == 0u || remove_index >= count) return false;
    uint64_t last_index = count - 1u;

    uint64_t remove_physical;
    struct v2m_directory_record_disk *remove_record;
    if (!load_record(device, geometry, parent, remove_index, v2m_dir_block_a,
                     &remove_physical, &remove_record)) return false;

    uint64_t last_physical = remove_physical;
    struct v2m_directory_record_disk last_copy = *remove_record;
    if (remove_index != last_index) {
        struct v2m_directory_record_disk *last_record;
        if (!load_record(device, geometry, parent, last_index, v2m_dir_block_b,
                         &last_physical, &last_record)) return false;
        last_copy = *last_record;
        *remove_record = last_copy;
        if (!write_fs_block(device, geometry, remove_physical, v2m_dir_block_a) ||
            !block_device_flush(device)) return false;
    }

    parent->size -= V2M_DIRECTORY_RECORD_SIZE;
    parent->generation++;
    if (!write_inode(device, geometry, parent_inode_index, parent)) return false;

    uint8_t *last_buffer = remove_index == last_index ? v2m_dir_block_a : v2m_dir_block_b;
    struct v2m_directory_record_disk *last_record;
    uint64_t ignored_physical;
    if (!load_record(device, geometry, parent, last_index, last_buffer,
                     &ignored_physical, &last_record)) {
        /* last_index is now beyond EOF; address it directly using the pre-shrink mapping. */
        uint64_t byte_offset = last_index * V2M_DIRECTORY_RECORD_SIZE;
        uint64_t logical = byte_offset / AURORA_FS_V2_FS_BLOCK_SIZE;
        uint64_t within = byte_offset % AURORA_FS_V2_FS_BLOCK_SIZE;
        if (!inline_physical(parent, logical, &last_physical) ||
            !read_fs_block(device, geometry, last_physical, last_buffer)) return false;
        last_record = (struct v2m_directory_record_disk *)(last_buffer + within);
    }
    zero_bytes(last_record, sizeof(*last_record));
    return write_fs_block(device, geometry, last_physical, last_buffer) && block_device_flush(device);
}

bool aurora_fs_v2_remove_child(
    struct aurora_fs_v2_allocator *allocator,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t parent_inode_index,
    uint64_t child_inode_index,
    const char *name
) {
    if (allocator == NULL || allocator->device == NULL || allocator->device->read_only ||
        !name_valid(name, NULL)) return false;

    struct v2m_inode_disk parent;
    struct v2m_inode_disk child;
    if (!read_inode(allocator->device, geometry, parent_inode_index, &parent) ||
        parent.object_id == 0u || parent.type != AURORA_FS_V2_OBJECT_DIRECTORY ||
        parent.extent_tree_root != 0u ||
        !read_inode(allocator->device, geometry, child_inode_index, &child) ||
        child.object_id == 0u || child.parent_object_id != parent.object_id ||
        !valid_type(child.type) || child.extent_tree_root != 0u ||
        child.extent_count > AURORA_FS_V2_INLINE_EXTENT_COUNT) return false;

    uint64_t record_index;
    struct v2m_directory_record_disk record;
    if (!find_record(allocator->device, geometry, &parent, name, &record_index, &record) ||
        record.object_id != child.object_id || record.type != child.type) return false;
    if (child.type == AURORA_FS_V2_OBJECT_DIRECTORY && child.size != 0u) return false;

    struct v2m_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint32_t extent_count = child.extent_count;
    for (uint32_t i = 0u; i < extent_count; ++i) extents[i] = child.extents[i];

    if (!compact_remove_record(allocator->device, geometry, parent_inode_index,
                               &parent, record_index)) return false;

    struct v2m_inode_disk empty;
    zero_bytes(&empty, sizeof(empty));
    if (!write_inode(allocator->device, geometry, child_inode_index, &empty)) return false;

    for (uint32_t i = 0u; i < extent_count; ++i) {
        if (extents[i].block_count == 0u ||
            !aurora_fs_v2_allocator_free_range(
                allocator, extents[i].physical_block, extents[i].block_count)) return false;
    }
    return true;
}

static bool test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba)
        return false;
    struct v2m_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset) return false;
    uint8_t *out = buffer;
    for (uint64_t i = 0u; i < length; ++i) out[i] = context->storage[offset + i];
    return true;
}

static bool test_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    if (device == NULL || buffer == NULL || device->read_only || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba)
        return false;
    struct v2m_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length) ||
        offset > context->storage_bytes || length > context->storage_bytes - offset) return false;
    const uint8_t *source = buffer;
    for (uint64_t i = 0u; i < length; ++i) context->storage[offset + i] = source[i];
    return true;
}

static bool test_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool run_geometry(uint32_t block_size) {
    zero_bytes(v2m_test_storage, sizeof(v2m_test_storage));
    struct v2m_test_context context = {
        .storage = v2m_test_storage,
        .storage_bytes = sizeof(v2m_test_storage)
    };
    struct aurora_block_device device = {
        .name = "aurorafs-v2-mutation-test",
        .block_size = block_size,
        .block_count = sizeof(v2m_test_storage) / block_size,
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
            &allocator, &geometry, 0u, 1u, 2u, AURORA_FS_V2_OBJECT_FILE, "payload.bin") ||
        !aurora_fs_v2_create_child(
            &allocator, &geometry, 0u, 2u, 3u, AURORA_FS_V2_OBJECT_DIRECTORY, "empty"))
        return false;

    for (size_t i = 0u; i < sizeof(v2m_test_payload); ++i)
        v2m_test_payload[i] = (uint8_t)((i * 19u + 7u) & 0xFFu);
    size_t written = 0u;
    if (!aurora_fs_v2_file_write(
            &allocator, &geometry, 1u, 0u, v2m_test_payload,
            sizeof(v2m_test_payload), &written) || written != sizeof(v2m_test_payload)) return false;

    struct v2m_inode_disk file_before;
    if (!read_inode(&device, &geometry, 1u, &file_before) || file_before.extent_count == 0u ||
        file_before.extent_tree_root != 0u) return false;
    struct v2m_extent_disk reclaimed[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint32_t reclaimed_count = file_before.extent_count;
    for (uint32_t i = 0u; i < reclaimed_count; ++i) reclaimed[i] = file_before.extents[i];

    if (!aurora_fs_v2_rename_child(&allocator, &geometry, 0u, "payload.bin", "renamed.bin"))
        return false;
    struct aurora_fs_v2_directory_entry entry;
    if (aurora_fs_v2_directory_lookup_entry(
            &allocator, &geometry, 0u, "payload.bin", &entry) ||
        !aurora_fs_v2_directory_lookup_entry(
            &allocator, &geometry, 0u, "renamed.bin", &entry) || entry.object_id != 2u)
        return false;

    if (!aurora_fs_v2_remove_child(&allocator, &geometry, 0u, 1u, "renamed.bin")) return false;
    if (aurora_fs_v2_directory_lookup_entry(
            &allocator, &geometry, 0u, "renamed.bin", &entry)) return false;
    struct v2m_inode_disk cleared;
    if (!read_inode(&device, &geometry, 1u, &cleared) || cleared.object_id != 0u) return false;
    for (uint32_t i = 0u; i < reclaimed_count; ++i) {
        for (uint64_t block = 0u; block < reclaimed[i].block_count; ++block) {
            bool allocated = true;
            if (!aurora_fs_v2_allocator_is_allocated(
                    &allocator, reclaimed[i].physical_block + block, &allocated) || allocated)
                return false;
        }
    }

    if (!aurora_fs_v2_create_child(
            &allocator, &geometry, 2u, 3u, 4u, AURORA_FS_V2_OBJECT_FILE, "nested") ||
        aurora_fs_v2_remove_child(&allocator, &geometry, 0u, 2u, "empty") ||
        !aurora_fs_v2_remove_child(&allocator, &geometry, 2u, 3u, "nested") ||
        !aurora_fs_v2_remove_child(&allocator, &geometry, 0u, 2u, "empty"))
        return false;

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened, &device, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) return false;
    return !aurora_fs_v2_directory_lookup_entry(
        &reopened, &geometry, 0u, "empty", &entry);
}

bool aurora_fs_v2_reclaim_remove_rename_self_test(void) {
    return run_geometry(512u) && run_geometry(4096u);
}
