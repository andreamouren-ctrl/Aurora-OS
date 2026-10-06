#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_driver.h>
#include <aurora/aurora_fs_v2_file_io.h>
#include <aurora/aurora_fs_v2_integrity.h>
#include <aurora/aurora_fs_v2_metadata.h>
#include <aurora/aurora_fs_v2_namespace_txn.h>
#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/aurora_fs_v2_recovery.h>
#include <aurora/block_device.h>
#include <aurora/heap.h>
#include <aurora/log.h>
#include <aurora/partition.h>

#define V2D_INODE_SIZE 256u
#define V2D_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2D_INODE_SIZE)
#define V2D_DIRECTORY_RECORD_SIZE 128u
#define V2D_DIRECTORY_NAME_MAX 108u
#define V2D_BOOT_PROBE_INODE 1u
#define V2D_BOOT_PROBE_OBJECT 2u

struct v2d_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2d_inode_disk {
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
    struct v2d_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct v2d_directory_record_disk {
    uint64_t object_id;
    uint32_t type;
    uint16_t name_length;
    uint16_t flags;
    uint32_t checksum;
    uint8_t name[V2D_DIRECTORY_NAME_MAX];
} __attribute__((packed));

struct v2d_partition_view {
    struct aurora_partition partition;
};

struct v2d_context {
    struct aurora_partition partition;
    struct v2d_partition_view view_backing;
    struct aurora_block_device view;
    struct aurora_fs_v2_format_geometry geometry;
    struct aurora_fs_v2_allocator allocator;
};

static uint8_t v2d_io_block[AURORA_FS_V2_FS_BLOCK_SIZE];

_Static_assert(sizeof(struct v2d_inode_disk) == V2D_INODE_SIZE,
               "AuroraFS v2 driver inode layout drifted");
_Static_assert(sizeof(struct v2d_directory_record_disk) == V2D_DIRECTORY_RECORD_SIZE,
               "AuroraFS v2 driver directory record layout drifted");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
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

static uint32_t record_checksum(struct v2d_directory_record_disk *record) {
    uint32_t saved = record->checksum;
    record->checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)record, sizeof(*record));
    record->checksum = saved;
    return checksum;
}

static bool view_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t count,
    void *buffer
) {
    struct v2d_partition_view *backing = device != NULL ? device->context : NULL;
    return backing != NULL && partition_read(&backing->partition, lba, count, buffer);
}

static bool view_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t count,
    const void *buffer
) {
    struct v2d_partition_view *backing = device != NULL ? device->context : NULL;
    return backing != NULL && !device->read_only &&
        partition_write(&backing->partition, lba, count, buffer);
}

static bool view_flush(struct aurora_block_device *device) {
    struct v2d_partition_view *backing = device != NULL ? device->context : NULL;
    return backing != NULL && block_device_flush(backing->partition.device);
}

static bool init_view(
    const struct aurora_partition *partition,
    struct v2d_partition_view *backing,
    struct aurora_block_device *view
) {
    if (partition == NULL || partition->device == NULL || backing == NULL || view == NULL ||
        partition->block_count == 0u) return false;
    backing->partition = *partition;
    zero_bytes(view, sizeof(*view));
    view->name = "aurorafs-v2-partition-view";
    view->block_size = partition->device->block_size;
    view->block_count = partition->block_count;
    view->read_only = partition->device->read_only;
    view->context = backing;
    view->read_blocks = view_read;
    view->write_blocks = view_write;
    view->flush = view_flush;
    return true;
}

static bool fs_block_io(
    struct v2d_context *context,
    uint64_t fs_block,
    void *buffer,
    bool write
) {
    if (context == NULL || buffer == NULL || fs_block >= context->geometry.total_fs_blocks)
        return false;
    uint64_t byte_offset = context->geometry.base_bytes +
        fs_block * (uint64_t)AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t lba = byte_offset / context->view.block_size;
    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / context->view.block_size;
    if (count == 0u || count > UINT32_MAX) return false;
    if (write)
        return block_device_write(&context->view, lba, (uint32_t)count, buffer) &&
            block_device_flush(&context->view);
    return block_device_read(&context->view, lba, (uint32_t)count, buffer);
}

static bool inode_location(
    const struct v2d_context *context,
    uint64_t inode_index,
    uint64_t *out_block,
    uint32_t *out_slot
) {
    if (context == NULL || out_block == NULL || out_slot == NULL ||
        inode_index >= context->geometry.inode_blocks * V2D_INODES_PER_BLOCK) return false;
    *out_block = context->geometry.inode_start + inode_index / V2D_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(inode_index % V2D_INODES_PER_BLOCK);
    return true;
}

static bool read_inode(
    struct v2d_context *context,
    uint64_t inode_index,
    struct v2d_inode_disk *out_inode
) {
    uint64_t block;
    uint32_t slot;
    if (out_inode == NULL || !inode_location(context, inode_index, &block, &slot) ||
        !fs_block_io(context, block, v2d_io_block, false)) return false;
    *out_inode = ((const struct v2d_inode_disk *)v2d_io_block)[slot];
    return true;
}

static bool find_inode_by_object(
    struct v2d_context *context,
    uint64_t object_id,
    uint64_t *out_inode_index,
    struct v2d_inode_disk *out_inode
) {
    if (context == NULL || object_id == 0u || out_inode_index == NULL) return false;
    uint64_t capacity = context->geometry.inode_blocks * V2D_INODES_PER_BLOCK;
    for (uint64_t i = 0u; i < capacity; ++i) {
        struct v2d_inode_disk inode;
        if (!read_inode(context, i, &inode)) return false;
        if (inode.object_id == object_id) {
            *out_inode_index = i;
            if (out_inode != NULL) *out_inode = inode;
            return true;
        }
    }
    return false;
}

static bool find_free_inode_and_object(
    struct v2d_context *context,
    uint64_t *out_inode_index,
    uint64_t *out_object_id
) {
    if (context == NULL || out_inode_index == NULL || out_object_id == NULL) return false;
    uint64_t capacity = context->geometry.inode_blocks * V2D_INODES_PER_BLOCK;
    uint64_t free_inode = UINT64_MAX;
    uint64_t max_object = 1u;
    for (uint64_t i = 0u; i < capacity; ++i) {
        struct v2d_inode_disk inode;
        if (!read_inode(context, i, &inode)) return false;
        if (inode.object_id == 0u && free_inode == UINT64_MAX) free_inode = i;
        if (inode.object_id > max_object) max_object = inode.object_id;
    }
    if (free_inode == UINT64_MAX || max_object == UINT64_MAX) return false;
    *out_inode_index = free_inode;
    *out_object_id = max_object + 1u;
    return true;
}

static bool copy_component(
    const char *path,
    size_t *inout_offset,
    char name[V2D_DIRECTORY_NAME_MAX + 1u],
    bool *out_last
) {
    if (path == NULL || inout_offset == NULL || name == NULL || out_last == NULL) return false;
    size_t offset = *inout_offset;
    while (path[offset] == '/') ++offset;
    if (path[offset] == '\0') return false;
    size_t length = 0u;
    while (path[offset] != '\0' && path[offset] != '/') {
        if (length >= V2D_DIRECTORY_NAME_MAX) return false;
        name[length++] = path[offset++];
    }
    if (length == 0u || (length == 1u && name[0] == '.') ||
        (length == 2u && name[0] == '.' && name[1] == '.')) return false;
    name[length] = '\0';
    while (path[offset] == '/') ++offset;
    *out_last = path[offset] == '\0';
    *inout_offset = offset;
    return true;
}

static enum aurora_fs_lookup_result resolve_path_result(
    struct v2d_context *context,
    const char *path,
    uint64_t *out_inode_index,
    struct v2d_inode_disk *out_inode
) {
    if (context == NULL || path == NULL || path[0] != '/' || out_inode_index == NULL)
        return AURORA_FS_LOOKUP_ERROR;

    struct v2d_inode_disk current;
    if (!read_inode(context, 0u, &current) || current.object_id != 1u ||
        current.type != AURORA_FS_V2_OBJECT_DIRECTORY) {
        return AURORA_FS_LOOKUP_ERROR;
    }

    uint64_t current_index = 0u;
    size_t offset = 0u;
    while (path[offset] == '/') ++offset;
    if (path[offset] == '\0') {
        *out_inode_index = current_index;
        if (out_inode != NULL) *out_inode = current;
        return AURORA_FS_LOOKUP_FOUND;
    }

    offset = 0u;
    for (;;) {
        char component[V2D_DIRECTORY_NAME_MAX + 1u];
        bool last;
        if (!copy_component(path, &offset, component, &last) ||
            current.type != AURORA_FS_V2_OBJECT_DIRECTORY) {
            return AURORA_FS_LOOKUP_ERROR;
        }

        struct aurora_fs_v2_directory_entry entry;
        enum aurora_fs_v2_lookup_result lookup =
            aurora_fs_v2_directory_lookup_entry_result(
                &context->allocator, &context->geometry, current_index,
                component, &entry);
        if (lookup == AURORA_FS_V2_LOOKUP_NOT_FOUND)
            return AURORA_FS_LOOKUP_NOT_FOUND;
        if (lookup != AURORA_FS_V2_LOOKUP_FOUND)
            return AURORA_FS_LOOKUP_ERROR;

        if (!find_inode_by_object(context, entry.object_id, &current_index, &current) ||
            current.type != (uint32_t)entry.type) {
            return AURORA_FS_LOOKUP_ERROR;
        }

        if (last) {
            *out_inode_index = current_index;
            if (out_inode != NULL) *out_inode = current;
            return AURORA_FS_LOOKUP_FOUND;
        }
    }
}

static bool resolve_path(
    struct v2d_context *context,
    const char *path,
    uint64_t *out_inode_index,
    struct v2d_inode_disk *out_inode
) {
    return resolve_path_result(context, path, out_inode_index, out_inode) ==
        AURORA_FS_LOOKUP_FOUND;
}

static bool resolve_parent(
    struct v2d_context *context,
    const char *path,
    uint64_t *out_parent_inode,
    char name[V2D_DIRECTORY_NAME_MAX + 1u]
) {
    if (context == NULL || path == NULL || path[0] != '/' ||
        out_parent_inode == NULL || name == NULL) return false;
    struct v2d_inode_disk current;
    if (!read_inode(context, 0u, &current) || current.object_id != 1u ||
        current.type != AURORA_FS_V2_OBJECT_DIRECTORY) return false;
    uint64_t current_index = 0u;
    size_t offset = 0u;
    for (;;) {
        char component[V2D_DIRECTORY_NAME_MAX + 1u];
        bool last;
        if (!copy_component(path, &offset, component, &last)) return false;
        if (last) {
            size_t i = 0u;
            while (component[i] != '\0') { name[i] = component[i]; ++i; }
            name[i] = '\0';
            *out_parent_inode = current_index;
            return current.type == AURORA_FS_V2_OBJECT_DIRECTORY;
        }
        struct aurora_fs_v2_directory_entry entry;
        if (!aurora_fs_v2_directory_lookup_entry(
                &context->allocator, &context->geometry, current_index,
                component, &entry) || entry.type != AURORA_FS_V2_OBJECT_DIRECTORY ||
            !find_inode_by_object(context, entry.object_id, &current_index, &current)) return false;
    }
}

static bool resolve_inline_block(
    const struct v2d_inode_disk *inode,
    uint64_t logical,
    uint64_t *out_physical
) {
    if (inode == NULL || out_physical == NULL || inode->extent_tree_root != 0u ||
        inode->extent_count > AURORA_FS_V2_INLINE_EXTENT_COUNT) return false;
    for (uint32_t i = 0u; i < inode->extent_count; ++i) {
        uint64_t start = inode->extents[i].logical_block;
        uint64_t count = inode->extents[i].block_count;
        if (count != 0u && logical >= start && logical - start < count) {
            *out_physical = inode->extents[i].physical_block + (logical - start);
            return true;
        }
    }
    return false;
}

static enum aurora_fs_probe_result v2d_probe(const struct aurora_partition *partition) {
    struct v2d_partition_view backing;
    struct aurora_block_device view;
    if (!init_view(partition, &backing, &view)) return AURORA_FS_PROBE_NO_MATCH;
    struct aurora_fs_v2_integrity_report report;
    if (!aurora_fs_v2_integrity_check_core(
            &view, AURORA_FS_V2_DEFAULT_BASE_BYTES, &report)) return AURORA_FS_PROBE_NO_MATCH;
    return view.read_only ? AURORA_FS_PROBE_MATCH_READ_ONLY : AURORA_FS_PROBE_MATCH_READ_WRITE;
}

static bool load_geometry(
    struct aurora_block_device *view,
    struct aurora_fs_v2_format_geometry *geometry,
    struct aurora_fs_v2_integrity_report *report
) {
    if (!aurora_fs_v2_integrity_check_core(
            view, AURORA_FS_V2_DEFAULT_BASE_BYTES, report)) return false;
    geometry->base_bytes = AURORA_FS_V2_DEFAULT_BASE_BYTES;
    geometry->total_fs_blocks = report->total_blocks;
    geometry->bitmap_start = report->bitmap_start;
    geometry->bitmap_blocks = report->bitmap_blocks;
    geometry->inode_start = report->inode_start;
    geometry->inode_blocks = report->inode_blocks;
    geometry->data_start = report->data_start;
    return true;
}

static bool v2d_mount(const struct aurora_partition *partition, void **out_context) {
    if (partition == NULL || out_context == NULL) return false;
    struct v2d_context *context = kheap_alloc(sizeof(*context), 16u);
    if (context == NULL) return false;
    zero_bytes(context, sizeof(*context));
    context->partition = *partition;
    if (!init_view(partition, &context->view_backing, &context->view)) return false;

    struct aurora_fs_v2_integrity_report report;
    if (!load_geometry(&context->view, &context->geometry, &report) ||
        !aurora_fs_v2_allocator_init(
            &context->allocator, &context->view, context->geometry.base_bytes,
            context->geometry.total_fs_blocks, context->geometry.bitmap_start,
            context->geometry.bitmap_blocks, context->geometry.data_start)) return false;

    if (!report.transaction_clean &&
        !aurora_fs_v2_recover_pending_transaction(&context->allocator, &context->geometry))
        return false;
    if (!aurora_fs_v2_integrity_check_full(
            &context->view, context->geometry.base_bytes, &report)) return false;

    *out_context = context;
    return true;
}

static void v2d_unmount(void *context) { (void)context; }

static enum aurora_fs_entry_type map_type(uint32_t type) {
    if (type == AURORA_FS_V2_OBJECT_FILE) return AURORA_FS_ENTRY_FILE;
    if (type == AURORA_FS_V2_OBJECT_DIRECTORY) return AURORA_FS_ENTRY_DIRECTORY;
    return AURORA_FS_ENTRY_UNKNOWN;
}

static enum aurora_fs_lookup_result v2d_stat_result(
    void *opaque,
    const char *path,
    struct aurora_fs_stat *out_stat
) {
    struct v2d_context *context = opaque;
    uint64_t inode_index;
    struct v2d_inode_disk inode;
    struct aurora_fs_v2_metadata metadata;
    if (out_stat == NULL) return AURORA_FS_LOOKUP_ERROR;

    enum aurora_fs_lookup_result lookup =
        resolve_path_result(context, path, &inode_index, &inode);
    if (lookup != AURORA_FS_LOOKUP_FOUND) return lookup;

    if (!aurora_fs_v2_metadata_read(
            &context->view, &context->geometry, inode_index, &metadata)) {
        return AURORA_FS_LOOKUP_ERROR;
    }

    zero_bytes(out_stat, sizeof(*out_stat));
    out_stat->type = map_type(inode.type);
    if (out_stat->type == AURORA_FS_ENTRY_UNKNOWN) return AURORA_FS_LOOKUP_ERROR;
    out_stat->size = inode.size;
    out_stat->allocated_size = inode.allocated_bytes;
    out_stat->created_time_ns = metadata.created_time_ns;
    out_stat->changed_time_ns = metadata.changed_time_ns;
    out_stat->modified_time_ns = metadata.modified_time_ns;
    out_stat->accessed_time_ns = metadata.accessed_time_ns;
    out_stat->filesystem_id = inode.object_id;
    out_stat->uid = metadata.uid;
    out_stat->gid = metadata.gid;
    out_stat->mode = metadata.mode;
    out_stat->link_count = metadata.link_count;
    return AURORA_FS_LOOKUP_FOUND;
}

static bool v2d_stat(void *opaque, const char *path, struct aurora_fs_stat *out_stat) {
    return v2d_stat_result(opaque, path, out_stat) == AURORA_FS_LOOKUP_FOUND;
}

static bool v2d_readdir(
    void *opaque,
    const char *path,
    uint64_t index,
    struct aurora_fs_dirent *out_entry
) {
    struct v2d_context *context = opaque;
    uint64_t inode_index;
    struct v2d_inode_disk inode;
    if (out_entry == NULL || !resolve_path(context, path, &inode_index, &inode) ||
        inode.type != AURORA_FS_V2_OBJECT_DIRECTORY ||
        (inode.size % V2D_DIRECTORY_RECORD_SIZE) != 0u ||
        index >= inode.size / V2D_DIRECTORY_RECORD_SIZE) return false;

    uint64_t byte_offset = index * V2D_DIRECTORY_RECORD_SIZE;
    uint64_t logical = byte_offset / AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t within = byte_offset % AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t physical;
    if (inode.extent_tree_root != 0u) {
        uint64_t contiguous;
        if (!aurora_fs_v2_extent_tree_lookup_unified(
                &context->allocator, inode.extent_tree_root, logical,
                &physical, &contiguous)) return false;
    } else if (!resolve_inline_block(&inode, logical, &physical)) return false;
    if (!fs_block_io(context, physical, v2d_io_block, false)) return false;

    struct v2d_directory_record_disk record =
        *(const struct v2d_directory_record_disk *)(v2d_io_block + within);
    if (record.object_id == 0u || record.name_length == 0u ||
        record.name_length > V2D_DIRECTORY_NAME_MAX ||
        record.checksum != record_checksum(&record)) return false;
    uint64_t child_index;
    struct v2d_inode_disk child;
    if (!find_inode_by_object(context, record.object_id, &child_index, &child)) return false;
    (void)child_index;
    zero_bytes(out_entry, sizeof(*out_entry));
    for (uint16_t i = 0u; i < record.name_length; ++i) out_entry->name[i] = (char)record.name[i];
    out_entry->name[record.name_length] = '\0';
    out_entry->type = map_type(record.type);
    out_entry->size = child.size;
    out_entry->filesystem_id = record.object_id;
    return out_entry->type != AURORA_FS_ENTRY_UNKNOWN;
}

static bool v2d_read(
    void *opaque,
    const char *path,
    uint64_t offset,
    void *buffer,
    size_t length,
    size_t *out_read
) {
    struct v2d_context *context = opaque;
    uint64_t inode_index;
    struct v2d_inode_disk inode;
    if (!resolve_path(context, path, &inode_index, &inode) ||
        inode.type != AURORA_FS_V2_OBJECT_FILE) return false;
    return aurora_fs_v2_file_read(
        &context->allocator, &context->geometry, inode_index,
        offset, buffer, length, out_read);
}

static bool v2d_write(
    void *opaque,
    const char *path,
    uint64_t offset,
    const void *buffer,
    size_t length,
    size_t *out_written
) {
    struct v2d_context *context = opaque;
    uint64_t inode_index;
    struct v2d_inode_disk inode;
    if (!resolve_path(context, path, &inode_index, &inode) ||
        inode.type != AURORA_FS_V2_OBJECT_FILE) return false;
    return aurora_fs_v2_file_write(
        &context->allocator, &context->geometry, inode_index,
        offset, buffer, length, out_written);
}

static bool v2d_create(void *opaque, const char *path, enum aurora_fs_entry_type type) {
    struct v2d_context *context = opaque;
    enum aurora_fs_v2_object_type object_type;
    if (type == AURORA_FS_ENTRY_FILE) object_type = AURORA_FS_V2_OBJECT_FILE;
    else if (type == AURORA_FS_ENTRY_DIRECTORY) object_type = AURORA_FS_V2_OBJECT_DIRECTORY;
    else return false;
    uint64_t parent_inode;
    char name[V2D_DIRECTORY_NAME_MAX + 1u];
    uint64_t child_inode;
    uint64_t object_id;
    struct v2d_inode_disk existing;
    uint64_t existing_index;
    enum aurora_fs_lookup_result existing_result =
        resolve_path_result(context, path, &existing_index, &existing);
    if (existing_result != AURORA_FS_LOOKUP_NOT_FOUND) return false;
    if (!resolve_parent(context, path, &parent_inode, name) ||
        !find_free_inode_and_object(context, &child_inode, &object_id)) return false;
    return aurora_fs_v2_create_child_txn(
        &context->allocator, &context->geometry, parent_inode, child_inode,
        object_id, object_type, name);
}

static bool v2d_remove(void *opaque, const char *path) {
    struct v2d_context *context = opaque;
    uint64_t parent_inode;
    uint64_t child_inode;
    char name[V2D_DIRECTORY_NAME_MAX + 1u];
    struct v2d_inode_disk child;
    if (!resolve_parent(context, path, &parent_inode, name) ||
        !resolve_path(context, path, &child_inode, &child) || child_inode == 0u) return false;
    return aurora_fs_v2_remove_child_txn(
        &context->allocator, &context->geometry, parent_inode, child_inode, name);
}

static bool v2d_rename(void *opaque, const char *old_path, const char *new_path) {
    struct v2d_context *context = opaque;
    uint64_t old_parent;
    uint64_t new_parent;
    char old_name[V2D_DIRECTORY_NAME_MAX + 1u];
    char new_name[V2D_DIRECTORY_NAME_MAX + 1u];
    struct v2d_inode_disk existing;
    uint64_t existing_index;
    enum aurora_fs_lookup_result new_result =
        resolve_path_result(context, new_path, &existing_index, &existing);
    if (new_result != AURORA_FS_LOOKUP_NOT_FOUND) return false;
    if (!resolve_parent(context, old_path, &old_parent, old_name) ||
        !resolve_parent(context, new_path, &new_parent, new_name) ||
        old_parent != new_parent) return false;
    return aurora_fs_v2_rename_child_txn(
        &context->allocator, &context->geometry, old_parent, old_name, new_name);
}

static bool v2d_truncate(void *opaque, const char *path, uint64_t size) {
    struct v2d_context *context = opaque;
    uint64_t inode_index;
    struct v2d_inode_disk inode;
    if (!resolve_path(context, path, &inode_index, &inode) ||
        inode.type != AURORA_FS_V2_OBJECT_FILE) return false;
    return aurora_fs_v2_file_truncate(
        &context->allocator, &context->geometry, inode_index, size);
}

static bool v2d_chmod(void *opaque, const char *path, uint32_t mode) {
    struct v2d_context *context = opaque;
    uint64_t inode_index;
    struct v2d_inode_disk inode;
    struct aurora_fs_v2_metadata metadata;
    if ((mode & ~AURORA_FS_V2_MODE_PERMISSION_MASK) != 0u ||
        !resolve_path(context, path, &inode_index, &inode) ||
        !aurora_fs_v2_metadata_read(
            &context->view, &context->geometry, inode_index, &metadata)) return false;
    metadata.mode = mode;
    return aurora_fs_v2_metadata_write(
        &context->view, &context->geometry, inode_index, &metadata);
}

static bool v2d_chown(void *opaque, const char *path, uint32_t uid, uint32_t gid) {
    struct v2d_context *context = opaque;
    uint64_t inode_index;
    struct v2d_inode_disk inode;
    struct aurora_fs_v2_metadata metadata;
    if (!resolve_path(context, path, &inode_index, &inode) ||
        !aurora_fs_v2_metadata_read(
            &context->view, &context->geometry, inode_index, &metadata)) return false;
    metadata.uid = uid;
    metadata.gid = gid;
    return aurora_fs_v2_metadata_write(
        &context->view, &context->geometry, inode_index, &metadata);
}

static const struct aurora_fs_driver v2d_driver = {
    .name = "AuroraFS v2",
    .probe = v2d_probe,
    .mount = v2d_mount,
    .unmount = v2d_unmount,
    .stat = v2d_stat,
    .stat_result = v2d_stat_result,
    .readdir = v2d_readdir,
    .read = v2d_read,
    .write = v2d_write,
    .create = v2d_create,
    .remove = v2d_remove,
    .rename = v2d_rename,
    .truncate = v2d_truncate,
    .chmod = v2d_chmod,
    .chown = v2d_chown
};

const struct aurora_fs_driver *aurora_fs_v2_driver(void) {
    return &v2d_driver;
}

bool aurora_fs_v2_prepare_system_partition(
    const struct aurora_partition *partition,
    bool *out_formatted
) {
    if (partition == NULL || out_formatted == NULL || partition->device == NULL ||
        partition->device->read_only) {
        log_line("[aurorafs-v2] prepare failed: invalid partition");
        return false;
    }
    *out_formatted = false;

    struct v2d_partition_view backing;
    struct aurora_block_device view;
    if (!init_view(partition, &backing, &view)) {
        log_line("[aurorafs-v2] prepare failed: init view");
        return false;
    }

    struct aurora_fs_v2_integrity_report report;
    struct aurora_fs_v2_format_geometry geometry;
    bool existing = load_geometry(&view, &geometry, &report);
    if (!existing) {
        if (!aurora_fs_v2_format_device(
                &view, AURORA_FS_V2_DEFAULT_BASE_BYTES, 0u, &geometry)) {
            log_line("[aurorafs-v2] prepare failed: format");
            return false;
        }
        if (!aurora_fs_v2_object_init(
                &view, &geometry, 0u, 1u, 1u, AURORA_FS_V2_OBJECT_DIRECTORY)) {
            log_line("[aurorafs-v2] prepare failed: root init");
            return false;
        }
        *out_formatted = true;
    }

    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator, &view, geometry.base_bytes, geometry.total_fs_blocks,
            geometry.bitmap_start, geometry.bitmap_blocks, geometry.data_start)) {
        log_line("[aurorafs-v2] prepare failed: allocator");
        return false;
    }

    if (existing && !report.transaction_clean &&
        !aurora_fs_v2_recover_pending_transaction(&allocator, &geometry)) {
        log_line("[aurorafs-v2] prepare failed: recovery");
        return false;
    }

    struct v2d_context temporary;
    zero_bytes(&temporary, sizeof(temporary));
    temporary.partition = *partition;
    temporary.view_backing = backing;
    temporary.view = view;
    temporary.view.context = &temporary.view_backing;
    temporary.geometry = geometry;
    temporary.allocator = allocator;
    temporary.allocator.device = &temporary.view;

    struct v2d_inode_disk probe;
    if (!read_inode(&temporary, V2D_BOOT_PROBE_INODE, &probe)) {
        log_line("[aurorafs-v2] prepare failed: boot probe inode read");
        return false;
    }
    if (probe.object_id == 0u) {
        if (!aurora_fs_v2_create_child_txn(
                &temporary.allocator, &temporary.geometry, 0u,
                V2D_BOOT_PROBE_INODE, V2D_BOOT_PROBE_OBJECT,
                AURORA_FS_V2_OBJECT_FILE, "aurora.boot-probe")) {
            log_line("[aurorafs-v2] prepare failed: boot probe create");
            return false;
        }
        static const uint8_t payload[] = "AURORA-FS-V2-PERSIST";
        size_t written = 0u;
        if (!aurora_fs_v2_file_write(
                &temporary.allocator, &temporary.geometry, V2D_BOOT_PROBE_INODE,
                0u, payload, sizeof(payload) - 1u, &written) ||
            written != sizeof(payload) - 1u) {
            log_line("[aurorafs-v2] prepare failed: boot probe write");
            return false;
        }
    } else if (probe.object_id != V2D_BOOT_PROBE_OBJECT ||
               probe.type != (uint32_t)AURORA_FS_V2_OBJECT_FILE ||
               probe.parent_object_id != 1u) {
        log_line("[aurorafs-v2] prepare failed: boot probe inode mismatch");
        return false;
    }

    if (!aurora_fs_v2_integrity_check_full(
            &temporary.view, temporary.geometry.base_bytes, &report)) {
        log_write("[aurorafs-v2] prepare failed: integrity code ");
        log_u64((uint64_t)report.error);
        log_line("");
        return false;
    }
    return true;
}
