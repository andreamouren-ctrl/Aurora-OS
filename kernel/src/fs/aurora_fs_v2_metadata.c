#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2_acl.h>
#include <aurora/aurora_fs_v2_metadata.h>
#include <aurora/block_device.h>

#define V2M_INODE_SIZE 256u
#define V2M_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2M_INODE_SIZE)
#define V2M_METADATA_MAGIC 0x324D4441u /* ADM2 */
#define V2M_METADATA_VERSION 1u
#define V2M_METADATA_SIZE 64u
#define V2M_TEST_STORAGE_BYTES (2u * 1024u * 1024u)

struct v2m_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2m_metadata_disk {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t checksum;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t link_count;
    uint64_t created_time_ns;
    uint64_t changed_time_ns;
    uint64_t modified_time_ns;
    uint64_t accessed_time_ns;
    uint32_t flags;
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

struct v2m_test_context {
    uint8_t *storage;
    uint64_t storage_bytes;
};

static uint8_t v2m_io_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2m_test_storage[V2M_TEST_STORAGE_BYTES];

_Static_assert(sizeof(struct v2m_metadata_disk) == V2M_METADATA_SIZE,
               "AuroraFS v2 metadata layout drifted");
_Static_assert(sizeof(struct v2m_inode_disk) == V2M_INODE_SIZE,
               "AuroraFS v2 metadata inode layout drifted");

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *dst = destination;
    const uint8_t *src = source;
    for (size_t i = 0u; i < length; ++i) dst[i] = src[i];
}

static bool all_zero(const void *buffer, size_t length) {
    const uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i)
        if (bytes[i] != 0u) return false;
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

static uint32_t metadata_checksum(struct v2m_metadata_disk *metadata) {
    uint32_t saved = metadata->checksum;
    metadata->checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)metadata, sizeof(*metadata));
    metadata->checksum = saved;
    return checksum;
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

static bool fs_block_io(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t fs_block,
    void *buffer,
    bool write
) {
    if (!geometry_valid(device, geometry) || buffer == NULL ||
        fs_block >= geometry->total_fs_blocks) return false;
    uint64_t byte_offset = geometry->base_bytes +
        fs_block * (uint64_t)AURORA_FS_V2_FS_BLOCK_SIZE;
    uint64_t lba = byte_offset / device->block_size;
    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / device->block_size;
    if (count == 0u || count > UINT32_MAX || lba >= device->block_count ||
        count > device->block_count - lba) return false;
    if (write) {
        if (device->read_only || !block_device_write(device, lba, (uint32_t)count, buffer))
            return false;
        return block_device_flush(device);
    }
    return block_device_read(device, lba, (uint32_t)count, buffer);
}

static bool load_inode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t *out_block,
    uint32_t *out_slot,
    struct v2m_inode_disk **out_inode
) {
    uint64_t block;
    uint32_t slot;
    if (out_inode == NULL || !inode_location(geometry, inode_index, &block, &slot) ||
        !fs_block_io(device, geometry, block, v2m_io_block, false)) return false;
    struct v2m_inode_disk *inodes = (struct v2m_inode_disk *)v2m_io_block;
    if (inodes[slot].object_id == 0u) return false;
    if (out_block != NULL) *out_block = block;
    if (out_slot != NULL) *out_slot = slot;
    *out_inode = &inodes[slot];
    return true;
}

static uint32_t default_mode_for_type(uint32_t type) {
    if (type == AURORA_FS_V2_OBJECT_DIRECTORY)
        return AURORA_FS_V2_MODE_DIRECTORY_DEFAULT;
    return AURORA_FS_V2_MODE_FILE_DEFAULT;
}

static void synthesize_legacy_metadata(
    uint32_t inode_type,
    struct aurora_fs_v2_metadata *out_metadata
) {
    zero_bytes(out_metadata, sizeof(*out_metadata));
    out_metadata->mode = default_mode_for_type(inode_type);
    out_metadata->link_count = 1u;
}

static bool decode_metadata(
    const struct v2m_inode_disk *inode,
    struct aurora_fs_v2_metadata *out_metadata
) {
    if (inode == NULL || out_metadata == NULL) return false;
    const struct v2m_metadata_disk *disk =
        (const struct v2m_metadata_disk *)inode->reserved1;
    if (all_zero(disk, sizeof(*disk))) {
        synthesize_legacy_metadata(inode->type, out_metadata);
        return true;
    }
    struct v2m_metadata_disk copy = *disk;
    if (copy.magic != V2M_METADATA_MAGIC || copy.version != V2M_METADATA_VERSION ||
        copy.size != V2M_METADATA_SIZE || copy.link_count == 0u ||
        (copy.mode & ~AURORA_FS_V2_MODE_PERMISSION_MASK) != 0u ||
        copy.checksum != metadata_checksum(&copy)) return false;
    out_metadata->mode = copy.mode;
    out_metadata->uid = copy.uid;
    out_metadata->gid = copy.gid;
    out_metadata->link_count = copy.link_count;
    out_metadata->created_time_ns = copy.created_time_ns;
    out_metadata->changed_time_ns = copy.changed_time_ns;
    out_metadata->modified_time_ns = copy.modified_time_ns;
    out_metadata->accessed_time_ns = copy.accessed_time_ns;
    out_metadata->flags = copy.flags;
    return true;
}

static bool metadata_values_valid(const struct aurora_fs_v2_metadata *metadata) {
    return metadata != NULL && metadata->link_count != 0u &&
        (metadata->mode & ~AURORA_FS_V2_MODE_PERMISSION_MASK) == 0u;
}

bool aurora_fs_v2_metadata_read(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct aurora_fs_v2_metadata *out_metadata
) {
    struct v2m_inode_disk *inode;
    if (out_metadata == NULL ||
        !load_inode(device, geometry, inode_index, NULL, NULL, &inode) ||
        !decode_metadata(inode, out_metadata)) return false;

    /* Security metadata is one logical integrity unit.  ACL parsing lives in the
       ACL module, but every normal metadata read verifies that the companion
       ACL tail is either absent (all zero) or structurally/CRC valid. */
    struct aurora_fs_v2_acl acl;
    return aurora_fs_v2_acl_read(device, geometry, inode_index, &acl);
}

bool aurora_fs_v2_metadata_write(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct aurora_fs_v2_metadata *metadata
) {
    uint64_t block;
    uint32_t slot;
    struct v2m_inode_disk *inode;
    if (device == NULL || device->read_only || !metadata_values_valid(metadata) ||
        !load_inode(device, geometry, inode_index, &block, &slot, &inode)) return false;
    (void)slot;

    struct v2m_metadata_disk disk;
    zero_bytes(&disk, sizeof(disk));
    disk.magic = V2M_METADATA_MAGIC;
    disk.version = V2M_METADATA_VERSION;
    disk.size = V2M_METADATA_SIZE;
    disk.mode = metadata->mode;
    disk.uid = metadata->uid;
    disk.gid = metadata->gid;
    disk.link_count = metadata->link_count;
    disk.created_time_ns = metadata->created_time_ns;
    disk.changed_time_ns = metadata->changed_time_ns;
    disk.modified_time_ns = metadata->modified_time_ns;
    disk.accessed_time_ns = metadata->accessed_time_ns;
    disk.flags = metadata->flags;
    disk.checksum = metadata_checksum(&disk);
    copy_bytes(inode->reserved1, &disk, sizeof(disk));
    return fs_block_io(device, geometry, block, v2m_io_block, true);
}

bool aurora_fs_v2_metadata_initialize(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    enum aurora_fs_v2_object_type type,
    uint32_t uid,
    uint32_t gid,
    uint32_t mode,
    uint64_t now_ns
) {
    if (type != AURORA_FS_V2_OBJECT_FILE && type != AURORA_FS_V2_OBJECT_DIRECTORY)
        return false;
    struct aurora_fs_v2_metadata metadata;
    zero_bytes(&metadata, sizeof(metadata));
    metadata.mode = mode;
    metadata.uid = uid;
    metadata.gid = gid;
    metadata.link_count = 1u;
    metadata.created_time_ns = now_ns;
    metadata.changed_time_ns = now_ns;
    metadata.modified_time_ns = now_ns;
    metadata.accessed_time_ns = now_ns;
    return aurora_fs_v2_metadata_write(device, geometry, inode_index, &metadata);
}

bool aurora_fs_v2_metadata_set_owner_mode(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint32_t uid,
    uint32_t gid,
    uint32_t mode,
    uint64_t changed_time_ns
) {
    struct aurora_fs_v2_metadata metadata;
    if (!aurora_fs_v2_metadata_read(device, geometry, inode_index, &metadata)) return false;
    metadata.uid = uid;
    metadata.gid = gid;
    metadata.mode = mode;
    metadata.changed_time_ns = changed_time_ns;
    return aurora_fs_v2_metadata_write(device, geometry, inode_index, &metadata);
}

bool aurora_fs_v2_metadata_set_times(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint64_t accessed_time_ns,
    uint64_t modified_time_ns,
    uint64_t changed_time_ns
) {
    struct aurora_fs_v2_metadata metadata;
    if (!aurora_fs_v2_metadata_read(device, geometry, inode_index, &metadata)) return false;
    metadata.accessed_time_ns = accessed_time_ns;
    metadata.modified_time_ns = modified_time_ns;
    metadata.changed_time_ns = changed_time_ns;
    return aurora_fs_v2_metadata_write(device, geometry, inode_index, &metadata);
}

static bool test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    struct v2m_test_context *context = device != NULL ? device->context : NULL;
    if (context == NULL || buffer == NULL || device->block_size == 0u ||
        lba >= device->block_count || block_count > device->block_count - lba) return false;
    uint64_t offset = lba * device->block_size;
    uint64_t length = (uint64_t)block_count * device->block_size;
    if (offset > context->storage_bytes || length > context->storage_bytes - offset) return false;
    copy_bytes(buffer, context->storage + offset, (size_t)length);
    return true;
}

static bool test_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    struct v2m_test_context *context = device != NULL ? device->context : NULL;
    if (context == NULL || buffer == NULL || device->read_only || device->block_size == 0u ||
        lba >= device->block_count || block_count > device->block_count - lba) return false;
    uint64_t offset = lba * device->block_size;
    uint64_t length = (uint64_t)block_count * device->block_size;
    if (offset > context->storage_bytes || length > context->storage_bytes - offset) return false;
    copy_bytes(context->storage + offset, buffer, (size_t)length);
    return true;
}

static bool test_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static bool run_test(uint32_t block_size) {
    zero_bytes(v2m_test_storage, sizeof(v2m_test_storage));
    struct v2m_test_context context = {
        .storage = v2m_test_storage,
        .storage_bytes = sizeof(v2m_test_storage)
    };
    struct aurora_block_device device = {
        .name = "aurorafs-v2-metadata-test",
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
            &device, AURORA_FS_V2_DEFAULT_BASE_BYTES, 0u, &geometry) ||
        !aurora_fs_v2_object_init(
            &device, &geometry, 0u, 1u, 1u, AURORA_FS_V2_OBJECT_DIRECTORY) ||
        !aurora_fs_v2_object_init(
            &device, &geometry, 1u, 2u, 1u, AURORA_FS_V2_OBJECT_FILE)) return false;

    struct aurora_fs_v2_metadata metadata;
    if (!aurora_fs_v2_metadata_read(&device, &geometry, 1u, &metadata) ||
        metadata.mode != AURORA_FS_V2_MODE_FILE_DEFAULT || metadata.uid != 0u ||
        metadata.gid != 0u || metadata.link_count != 1u) return false;

    if (!aurora_fs_v2_metadata_set_owner_mode(
            &device, &geometry, 1u, 1000u, 100u, 0640u, 555u) ||
        !aurora_fs_v2_metadata_set_times(
            &device, &geometry, 1u, 111u, 222u, 333u) ||
        !aurora_fs_v2_metadata_read(&device, &geometry, 1u, &metadata)) return false;

    return metadata.mode == 0640u && metadata.uid == 1000u && metadata.gid == 100u &&
        metadata.link_count == 1u && metadata.created_time_ns == 0u &&
        metadata.accessed_time_ns == 111u && metadata.modified_time_ns == 222u &&
        metadata.changed_time_ns == 333u;
}

bool aurora_fs_v2_metadata_self_test(void) {
    return run_test(512u) && run_test(4096u);
}
