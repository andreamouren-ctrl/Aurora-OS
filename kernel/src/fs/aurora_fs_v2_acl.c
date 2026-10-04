#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2_acl.h>
#include <aurora/aurora_fs_v2_metadata.h>
#include <aurora/aurora_fs_v2_objects.h>
#include <aurora/block_device.h>

#define V2A_INODE_SIZE 256u
#define V2A_INODES_PER_BLOCK (AURORA_FS_V2_FS_BLOCK_SIZE / V2A_INODE_SIZE)
#define V2A_ACL_OFFSET 64u
#define V2A_ACL_SIZE 32u
#define V2A_ACL_MAGIC 0x324C4341u /* ACL2 */
#define V2A_ACL_VERSION 1u
#define V2A_TEST_STORAGE_BYTES (2u * 1024u * 1024u)

struct v2a_extent_disk {
    uint64_t logical_block;
    uint64_t physical_block;
    uint64_t block_count;
} __attribute__((packed));

struct v2a_acl_entry_disk {
    uint8_t subject_type;
    uint8_t permissions;
    uint16_t reserved;
    uint32_t subject_id;
} __attribute__((packed));

struct v2a_acl_disk {
    uint32_t magic;
    uint8_t version;
    uint8_t entry_count;
    uint16_t flags;
    uint32_t checksum;
    struct v2a_acl_entry_disk entries[AURORA_FS_V2_ACL_MAX_ENTRIES];
    uint32_t reserved;
} __attribute__((packed));

struct v2a_inode_disk {
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
    struct v2a_extent_disk extents[AURORA_FS_V2_INLINE_EXTENT_COUNT];
    uint8_t reserved1[96];
} __attribute__((packed));

struct v2a_test_context {
    uint8_t *storage;
    uint64_t storage_bytes;
};

static uint8_t v2a_io_block[AURORA_FS_V2_FS_BLOCK_SIZE];
static uint8_t v2a_test_storage[V2A_TEST_STORAGE_BYTES];

_Static_assert(sizeof(struct v2a_acl_entry_disk) == 8u,
               "AuroraFS v2 ACL entry layout drifted");
_Static_assert(sizeof(struct v2a_acl_disk) == V2A_ACL_SIZE,
               "AuroraFS v2 ACL layout drifted");
_Static_assert(sizeof(struct v2a_inode_disk) == V2A_INODE_SIZE,
               "AuroraFS v2 ACL inode layout drifted");

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

static uint32_t acl_checksum(struct v2a_acl_disk *acl) {
    uint32_t saved = acl->checksum;
    acl->checksum = 0u;
    uint32_t checksum = crc32_ieee((const uint8_t *)acl, sizeof(*acl));
    acl->checksum = saved;
    return checksum;
}

static bool subject_type_valid(uint8_t type) {
    return type == (uint8_t)AURORA_FS_V2_ACL_SUBJECT_USER ||
        type == (uint8_t)AURORA_FS_V2_ACL_SUBJECT_GROUP;
}

static bool acl_valid(const struct aurora_fs_v2_acl *acl) {
    if (acl == NULL || acl->entry_count > AURORA_FS_V2_ACL_MAX_ENTRIES) return false;
    for (uint8_t i = 0u; i < acl->entry_count; ++i) {
        const struct aurora_fs_v2_acl_entry *entry = &acl->entries[i];
        if (!subject_type_valid((uint8_t)entry->subject_type) ||
            (entry->permissions & ~AURORA_FS_V2_ACL_PERM_MASK) != 0u) return false;
        for (uint8_t j = 0u; j < i; ++j) {
            if (acl->entries[j].subject_type == entry->subject_type &&
                acl->entries[j].subject_id == entry->subject_id) return false;
        }
    }
    return true;
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
        inode_index >= geometry->inode_blocks * V2A_INODES_PER_BLOCK) return false;
    *out_block = geometry->inode_start + inode_index / V2A_INODES_PER_BLOCK;
    *out_slot = (uint32_t)(inode_index % V2A_INODES_PER_BLOCK);
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
    struct v2a_inode_disk **out_inode
) {
    uint64_t block;
    uint32_t slot;
    if (out_inode == NULL || !inode_location(geometry, inode_index, &block, &slot) ||
        !fs_block_io(device, geometry, block, v2a_io_block, false)) return false;
    struct v2a_inode_disk *inodes = (struct v2a_inode_disk *)v2a_io_block;
    if (inodes[slot].object_id == 0u) return false;
    if (out_block != NULL) *out_block = block;
    *out_inode = &inodes[slot];
    return true;
}

bool aurora_fs_v2_acl_read(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    struct aurora_fs_v2_acl *out_acl
) {
    struct v2a_inode_disk *inode;
    if (out_acl == NULL || !load_inode(device, geometry, inode_index, NULL, &inode))
        return false;
    zero_bytes(out_acl, sizeof(*out_acl));
    const struct v2a_acl_disk *disk =
        (const struct v2a_acl_disk *)(inode->reserved1 + V2A_ACL_OFFSET);
    if (all_zero(disk, sizeof(*disk))) return true;

    struct v2a_acl_disk copy = *disk;
    if (copy.magic != V2A_ACL_MAGIC || copy.version != V2A_ACL_VERSION ||
        copy.entry_count > AURORA_FS_V2_ACL_MAX_ENTRIES ||
        copy.checksum != acl_checksum(&copy)) return false;
    out_acl->entry_count = copy.entry_count;
    for (uint8_t i = 0u; i < copy.entry_count; ++i) {
        if (!subject_type_valid(copy.entries[i].subject_type) ||
            (copy.entries[i].permissions & ~AURORA_FS_V2_ACL_PERM_MASK) != 0u) return false;
        out_acl->entries[i].subject_type =
            (enum aurora_fs_v2_acl_subject_type)copy.entries[i].subject_type;
        out_acl->entries[i].permissions = copy.entries[i].permissions;
        out_acl->entries[i].subject_id = copy.entries[i].subject_id;
    }
    return acl_valid(out_acl);
}

bool aurora_fs_v2_acl_write(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    const struct aurora_fs_v2_acl *acl
) {
    uint64_t block;
    struct v2a_inode_disk *inode;
    if (device == NULL || device->read_only || !acl_valid(acl) ||
        !load_inode(device, geometry, inode_index, &block, &inode)) return false;

    struct v2a_acl_disk disk;
    zero_bytes(&disk, sizeof(disk));
    if (acl->entry_count != 0u) {
        disk.magic = V2A_ACL_MAGIC;
        disk.version = V2A_ACL_VERSION;
        disk.entry_count = acl->entry_count;
        for (uint8_t i = 0u; i < acl->entry_count; ++i) {
            disk.entries[i].subject_type = (uint8_t)acl->entries[i].subject_type;
            disk.entries[i].permissions = acl->entries[i].permissions;
            disk.entries[i].subject_id = acl->entries[i].subject_id;
        }
        disk.checksum = acl_checksum(&disk);
    }
    copy_bytes(inode->reserved1 + V2A_ACL_OFFSET, &disk, sizeof(disk));
    return fs_block_io(device, geometry, block, v2a_io_block, true);
}

bool aurora_fs_v2_acl_clear(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index
) {
    struct aurora_fs_v2_acl acl;
    zero_bytes(&acl, sizeof(acl));
    return aurora_fs_v2_acl_write(device, geometry, inode_index, &acl);
}

bool aurora_fs_v2_acl_check_access(
    struct aurora_block_device *device,
    const struct aurora_fs_v2_format_geometry *geometry,
    uint64_t inode_index,
    uint32_t effective_uid,
    uint32_t effective_gid,
    uint8_t requested_permissions
) {
    if ((requested_permissions & ~AURORA_FS_V2_ACL_PERM_MASK) != 0u) return false;
    if (effective_uid == 0u) return true;

    struct aurora_fs_v2_acl acl;
    struct aurora_fs_v2_metadata metadata;
    if (!aurora_fs_v2_acl_read(device, geometry, inode_index, &acl) ||
        !aurora_fs_v2_metadata_read(device, geometry, inode_index, &metadata)) return false;

    for (uint8_t i = 0u; i < acl.entry_count; ++i) {
        const struct aurora_fs_v2_acl_entry *entry = &acl.entries[i];
        if (entry->subject_type == AURORA_FS_V2_ACL_SUBJECT_USER &&
            entry->subject_id == effective_uid)
            return (entry->permissions & requested_permissions) == requested_permissions;
    }
    for (uint8_t i = 0u; i < acl.entry_count; ++i) {
        const struct aurora_fs_v2_acl_entry *entry = &acl.entries[i];
        if (entry->subject_type == AURORA_FS_V2_ACL_SUBJECT_GROUP &&
            entry->subject_id == effective_gid)
            return (entry->permissions & requested_permissions) == requested_permissions;
    }

    uint8_t permissions;
    if (effective_uid == metadata.uid)
        permissions = (uint8_t)((metadata.mode >> 6u) & AURORA_FS_V2_ACL_PERM_MASK);
    else if (effective_gid == metadata.gid)
        permissions = (uint8_t)((metadata.mode >> 3u) & AURORA_FS_V2_ACL_PERM_MASK);
    else
        permissions = (uint8_t)(metadata.mode & AURORA_FS_V2_ACL_PERM_MASK);
    return (permissions & requested_permissions) == requested_permissions;
}

static bool test_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t count,
    void *buffer
) {
    struct v2a_test_context *context = device != NULL ? device->context : NULL;
    if (context == NULL || buffer == NULL || lba >= device->block_count ||
        count > device->block_count - lba) return false;
    uint64_t offset = lba * device->block_size;
    uint64_t length = (uint64_t)count * device->block_size;
    if (offset > context->storage_bytes || length > context->storage_bytes - offset) return false;
    copy_bytes(buffer, context->storage + offset, (size_t)length);
    return true;
}

static bool test_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t count,
    const void *buffer
) {
    struct v2a_test_context *context = device != NULL ? device->context : NULL;
    if (context == NULL || buffer == NULL || device->read_only ||
        lba >= device->block_count || count > device->block_count - lba) return false;
    uint64_t offset = lba * device->block_size;
    uint64_t length = (uint64_t)count * device->block_size;
    if (offset > context->storage_bytes || length > context->storage_bytes - offset) return false;
    copy_bytes(context->storage + offset, buffer, (size_t)length);
    return true;
}

static bool test_flush(struct aurora_block_device *device) { return device != NULL; }

static bool run_test(uint32_t block_size) {
    zero_bytes(v2a_test_storage, sizeof(v2a_test_storage));
    struct v2a_test_context context = {
        .storage = v2a_test_storage,
        .storage_bytes = sizeof(v2a_test_storage)
    };
    struct aurora_block_device device = {
        .name = "aurorafs-v2-acl-test",
        .block_size = block_size,
        .block_count = sizeof(v2a_test_storage) / block_size,
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
            &device, &geometry, 1u, 2u, 1u, AURORA_FS_V2_OBJECT_FILE) ||
        !aurora_fs_v2_metadata_set_owner_mode(
            &device, &geometry, 1u, 1000u, 100u, 0640u, 0u)) return false;

    struct aurora_fs_v2_acl acl;
    if (!aurora_fs_v2_acl_read(&device, &geometry, 1u, &acl) || acl.entry_count != 0u)
        return false;
    if (!aurora_fs_v2_acl_check_access(
            &device, &geometry, 1u, 1000u, 100u, AURORA_FS_V2_ACL_PERM_WRITE) ||
        aurora_fs_v2_acl_check_access(
            &device, &geometry, 1u, 2000u, 200u, AURORA_FS_V2_ACL_PERM_READ))
        return false;

    zero_bytes(&acl, sizeof(acl));
    acl.entry_count = 2u;
    acl.entries[0].subject_type = AURORA_FS_V2_ACL_SUBJECT_USER;
    acl.entries[0].subject_id = 2000u;
    acl.entries[0].permissions = AURORA_FS_V2_ACL_PERM_READ;
    acl.entries[1].subject_type = AURORA_FS_V2_ACL_SUBJECT_GROUP;
    acl.entries[1].subject_id = 300u;
    acl.entries[1].permissions = AURORA_FS_V2_ACL_PERM_READ | AURORA_FS_V2_ACL_PERM_WRITE;
    if (!aurora_fs_v2_acl_write(&device, &geometry, 1u, &acl)) return false;

    struct aurora_fs_v2_acl reopened;
    if (!aurora_fs_v2_acl_read(&device, &geometry, 1u, &reopened) ||
        reopened.entry_count != 2u || reopened.entries[0].subject_id != 2000u ||
        reopened.entries[1].subject_id != 300u) return false;
    if (!aurora_fs_v2_acl_check_access(
            &device, &geometry, 1u, 2000u, 999u, AURORA_FS_V2_ACL_PERM_READ) ||
        aurora_fs_v2_acl_check_access(
            &device, &geometry, 1u, 2000u, 999u, AURORA_FS_V2_ACL_PERM_WRITE) ||
        !aurora_fs_v2_acl_check_access(
            &device, &geometry, 1u, 9999u, 300u,
            AURORA_FS_V2_ACL_PERM_READ | AURORA_FS_V2_ACL_PERM_WRITE)) return false;

    if (!aurora_fs_v2_acl_clear(&device, &geometry, 1u) ||
        !aurora_fs_v2_acl_read(&device, &geometry, 1u, &reopened) ||
        reopened.entry_count != 0u) return false;
    return aurora_fs_v2_acl_check_access(
        &device, &geometry, 1u, 1000u, 100u, AURORA_FS_V2_ACL_PERM_READ);
}

bool aurora_fs_v2_acl_self_test(void) {
    return run_test(512u) && run_test(4096u);
}
