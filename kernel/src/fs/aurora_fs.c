#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs.h>
#include <aurora/block_device.h>
#include <aurora/heap.h>
#include <aurora/partition.h>

#define AURORA_FS_MAGIC_0 'A'
#define AURORA_FS_MAGIC_1 'U'
#define AURORA_FS_MAGIC_2 'R'
#define AURORA_FS_MAGIC_3 'A'
#define AURORA_FS_MAGIC_4 'F'
#define AURORA_FS_MAGIC_5 'S'
#define AURORA_FS_MAGIC_6 '1'
#define AURORA_FS_MAGIC_7 '\0'

#define AURORA_FS_VERSION 1u
#define AURORA_FS_BLOCK_SIZE 512u
#define AURORA_FS_ROOT_ENTRY_COUNT 8u
#define AURORA_FS_ROOT_LBA 1u
#define AURORA_FS_DATA_LBA 2u

struct aurora_fs_superblock_disk {
    uint8_t magic[8];
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t root_lba;
    uint64_t data_lba;
    uint64_t generation;
    uint32_t metadata_checksum;
    uint8_t reserved[460];
} __attribute__((packed));

struct aurora_fs_dir_entry_disk {
    uint8_t in_use;
    uint8_t reserved0[3];
    char name[32];
    uint32_t start_block;
    uint32_t size;
    uint32_t data_checksum;
    uint8_t reserved1[16];
} __attribute__((packed));

struct aurora_fs_driver_context {
    struct aurora_partition partition;
    struct aurora_fs_superblock_disk superblock;
};

_Static_assert(sizeof(struct aurora_fs_superblock_disk) == 512u,
               "AuroraFS superblock must fill one block");
_Static_assert(sizeof(struct aurora_fs_dir_entry_disk) == 64u,
               "AuroraFS directory entry must be 64 bytes");

static uint32_t fnv1a32(const uint8_t *data, size_t length) {
    uint32_t hash = 2166136261u;

    for (size_t i = 0u; i < length; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }

    return hash;
}

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    for (size_t i = 0u; i < length; ++i) {
        bytes[i] = 0u;
    }
}

static bool string_equal(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }

    size_t i = 0u;
    while (a[i] != '\0' || b[i] != '\0') {
        if (a[i] != b[i]) {
            return false;
        }
        ++i;
    }
    return true;
}

static bool names_equal(const char *entry_name, const char *name) {
    for (size_t i = 0u; i < 32u; ++i) {
        char a = entry_name[i];
        char b = name[i];

        if (a != b) {
            return false;
        }

        if (a == '\0') {
            return true;
        }
    }

    return true;
}

static void copy_name(char destination[32], const char *name) {
    size_t i = 0u;
    for (; i < AURORA_FS_BOOTSTRAP_NAME_MAX && name[i] != '\0'; ++i) {
        destination[i] = name[i];
    }

    destination[i] = '\0';
    for (++i; i < 32u; ++i) {
        destination[i] = '\0';
    }
}

static void copy_fs_name(char destination[AURORA_FS_NAME_MAX], const char *source) {
    size_t i = 0u;
    for (; i + 1u < AURORA_FS_NAME_MAX && source[i] != '\0'; ++i) {
        destination[i] = source[i];
    }
    destination[i] = '\0';
}

static void set_magic(struct aurora_fs_superblock_disk *superblock) {
    const uint8_t magic[8] = {
        AURORA_FS_MAGIC_0, AURORA_FS_MAGIC_1,
        AURORA_FS_MAGIC_2, AURORA_FS_MAGIC_3,
        AURORA_FS_MAGIC_4, AURORA_FS_MAGIC_5,
        AURORA_FS_MAGIC_6, AURORA_FS_MAGIC_7
    };

    for (size_t i = 0u; i < sizeof(magic); ++i) {
        superblock->magic[i] = magic[i];
    }
}

static bool magic_valid(const struct aurora_fs_superblock_disk *superblock) {
    const uint8_t magic[8] = {
        AURORA_FS_MAGIC_0, AURORA_FS_MAGIC_1,
        AURORA_FS_MAGIC_2, AURORA_FS_MAGIC_3,
        AURORA_FS_MAGIC_4, AURORA_FS_MAGIC_5,
        AURORA_FS_MAGIC_6, AURORA_FS_MAGIC_7
    };

    for (size_t i = 0u; i < sizeof(magic); ++i) {
        if (superblock->magic[i] != magic[i]) {
            return false;
        }
    }

    return true;
}

static uint32_t superblock_checksum(struct aurora_fs_superblock_disk *superblock) {
    uint32_t saved = superblock->metadata_checksum;
    superblock->metadata_checksum = 0u;
    uint32_t checksum = fnv1a32((const uint8_t *)superblock, sizeof(*superblock));
    superblock->metadata_checksum = saved;
    return checksum;
}

static bool superblock_valid(
    struct aurora_fs_superblock_disk *superblock,
    uint64_t available_blocks
) {
    if (!magic_valid(superblock) ||
        superblock->version != AURORA_FS_VERSION ||
        superblock->block_size != AURORA_FS_BLOCK_SIZE ||
        superblock->root_lba != AURORA_FS_ROOT_LBA ||
        superblock->data_lba != AURORA_FS_DATA_LBA ||
        superblock->total_blocks == 0u ||
        superblock->total_blocks > available_blocks) {
        return false;
    }

    return superblock_checksum(superblock) == superblock->metadata_checksum;
}

static bool write_superblock(
    struct aurora_block_device *device,
    struct aurora_fs_superblock_disk *superblock
) {
    superblock->metadata_checksum = 0u;
    superblock->metadata_checksum = superblock_checksum(superblock);
    return block_device_write(
        device,
        AURORA_FS_BOOTSTRAP_BASE_LBA,
        1u,
        superblock
    );
}

static bool format_filesystem(
    struct aurora_block_device *device,
    struct aurora_fs_superblock_disk *superblock
) {
    if (device == NULL || device->block_size != AURORA_FS_BLOCK_SIZE ||
        device->block_count <= AURORA_FS_BOOTSTRAP_BASE_LBA + AURORA_FS_DATA_LBA +
            AURORA_FS_ROOT_ENTRY_COUNT) {
        return false;
    }

    zero_bytes(superblock, sizeof(*superblock));
    set_magic(superblock);
    superblock->version = AURORA_FS_VERSION;
    superblock->block_size = AURORA_FS_BLOCK_SIZE;
    superblock->total_blocks = device->block_count - AURORA_FS_BOOTSTRAP_BASE_LBA;
    superblock->root_lba = AURORA_FS_ROOT_LBA;
    superblock->data_lba = AURORA_FS_DATA_LBA;
    superblock->generation = 1u;

    struct aurora_fs_dir_entry_disk directory[AURORA_FS_ROOT_ENTRY_COUNT];
    zero_bytes(directory, sizeof(directory));

    if (!write_superblock(device, superblock)) {
        return false;
    }

    return block_device_write(
        device,
        AURORA_FS_BOOTSTRAP_BASE_LBA + AURORA_FS_ROOT_LBA,
        1u,
        directory
    );
}

static bool load_superblock(
    struct aurora_block_device *device,
    struct aurora_fs_superblock_disk *superblock
) {
    if (!block_device_read(
            device,
            AURORA_FS_BOOTSTRAP_BASE_LBA,
            1u,
            superblock)) {
        return false;
    }

    if (device->block_count <= AURORA_FS_BOOTSTRAP_BASE_LBA) {
        return false;
    }

    return superblock_valid(
        superblock,
        device->block_count - AURORA_FS_BOOTSTRAP_BASE_LBA
    );
}

static bool read_directory(
    struct aurora_block_device *device,
    struct aurora_fs_dir_entry_disk directory[AURORA_FS_ROOT_ENTRY_COUNT]
) {
    return block_device_read(
        device,
        AURORA_FS_BOOTSTRAP_BASE_LBA + AURORA_FS_ROOT_LBA,
        1u,
        directory
    );
}

static bool write_directory(
    struct aurora_block_device *device,
    const struct aurora_fs_dir_entry_disk directory[AURORA_FS_ROOT_ENTRY_COUNT]
) {
    return block_device_write(
        device,
        AURORA_FS_BOOTSTRAP_BASE_LBA + AURORA_FS_ROOT_LBA,
        1u,
        directory
    );
}

static bool driver_read_superblock(
    const struct aurora_partition *partition,
    struct aurora_fs_superblock_disk *superblock
) {
    if (partition == NULL || partition->device == NULL ||
        partition->device->block_size != AURORA_FS_BLOCK_SIZE ||
        partition->block_count <= AURORA_FS_BOOTSTRAP_BASE_LBA) {
        return false;
    }

    if (!partition_read(
            partition,
            AURORA_FS_BOOTSTRAP_BASE_LBA,
            1u,
            superblock)) {
        return false;
    }

    return superblock_valid(
        superblock,
        partition->block_count - AURORA_FS_BOOTSTRAP_BASE_LBA
    );
}

static bool driver_read_directory(
    const struct aurora_fs_driver_context *context,
    struct aurora_fs_dir_entry_disk directory[AURORA_FS_ROOT_ENTRY_COUNT]
) {
    return partition_read(
        &context->partition,
        AURORA_FS_BOOTSTRAP_BASE_LBA + context->superblock.root_lba,
        1u,
        directory
    );
}

static bool driver_write_directory(
    const struct aurora_fs_driver_context *context,
    const struct aurora_fs_dir_entry_disk directory[AURORA_FS_ROOT_ENTRY_COUNT]
) {
    return partition_write(
        &context->partition,
        AURORA_FS_BOOTSTRAP_BASE_LBA + context->superblock.root_lba,
        1u,
        directory
    );
}

static int driver_find_entry(
    const struct aurora_fs_driver_context *context,
    const char *path,
    struct aurora_fs_dir_entry_disk directory[AURORA_FS_ROOT_ENTRY_COUNT]
) {
    if (path == NULL || path[0] != '/' || path[1] == '\0') {
        return -1;
    }

    const char *name = path + 1u;
    for (size_t i = 0u; name[i] != '\0'; ++i) {
        if (name[i] == '/') {
            return -1;
        }
    }

    if (!driver_read_directory(context, directory)) {
        return -1;
    }

    for (size_t i = 0u; i < AURORA_FS_ROOT_ENTRY_COUNT; ++i) {
        if (directory[i].in_use != 0u && names_equal(directory[i].name, name)) {
            return (int)i;
        }
    }

    return -1;
}

static enum aurora_fs_probe_result aurora_fs_probe_driver(
    const struct aurora_partition *partition
) {
    struct aurora_fs_superblock_disk superblock;
    if (!driver_read_superblock(partition, &superblock)) {
        return AURORA_FS_PROBE_NO_MATCH;
    }

    if (partition->device != NULL && !partition->device->read_only) {
        return AURORA_FS_PROBE_MATCH_READ_WRITE;
    }

    return AURORA_FS_PROBE_MATCH_READ_ONLY;
}

static bool aurora_fs_mount_driver(
    const struct aurora_partition *partition,
    void **out_context
) {
    if (partition == NULL || out_context == NULL) {
        return false;
    }

    struct aurora_fs_superblock_disk superblock;
    if (!driver_read_superblock(partition, &superblock)) {
        return false;
    }

    struct aurora_fs_driver_context *context = kheap_alloc(
        sizeof(*context),
        _Alignof(struct aurora_fs_driver_context)
    );
    if (context == NULL) {
        return false;
    }

    context->partition = *partition;
    context->superblock = superblock;
    *out_context = context;
    return true;
}

static void aurora_fs_unmount_driver(void *context) {
    (void)context;
    /* Current bootstrap heap is monotonic and has no free operation yet. */
}

static bool aurora_fs_stat_driver(
    void *opaque_context,
    const char *path,
    struct aurora_fs_stat *out_stat
) {
    if (opaque_context == NULL || path == NULL || out_stat == NULL) {
        return false;
    }

    zero_bytes(out_stat, sizeof(*out_stat));

    if (string_equal(path, "/")) {
        out_stat->type = AURORA_FS_ENTRY_DIRECTORY;
        return true;
    }

    struct aurora_fs_driver_context *context = opaque_context;
    struct aurora_fs_dir_entry_disk directory[AURORA_FS_ROOT_ENTRY_COUNT];
    int index = driver_find_entry(context, path, directory);
    if (index < 0) {
        return false;
    }

    const struct aurora_fs_dir_entry_disk *entry = &directory[index];
    out_stat->type = AURORA_FS_ENTRY_FILE;
    out_stat->size = entry->size;
    out_stat->allocated_size = AURORA_FS_BLOCK_SIZE;
    out_stat->filesystem_id = (uint64_t)(uint32_t)index + 1u;
    return true;
}

static bool aurora_fs_readdir_driver(
    void *opaque_context,
    const char *path,
    uint64_t index,
    struct aurora_fs_dirent *out_entry
) {
    if (opaque_context == NULL || path == NULL || out_entry == NULL ||
        !string_equal(path, "/")) {
        return false;
    }

    struct aurora_fs_driver_context *context = opaque_context;
    struct aurora_fs_dir_entry_disk directory[AURORA_FS_ROOT_ENTRY_COUNT];
    if (!driver_read_directory(context, directory)) {
        return false;
    }

    uint64_t seen = 0u;
    for (size_t i = 0u; i < AURORA_FS_ROOT_ENTRY_COUNT; ++i) {
        if (directory[i].in_use == 0u) {
            continue;
        }

        if (seen == index) {
            zero_bytes(out_entry, sizeof(*out_entry));
            copy_fs_name(out_entry->name, directory[i].name);
            out_entry->type = AURORA_FS_ENTRY_FILE;
            out_entry->size = directory[i].size;
            out_entry->filesystem_id = (uint64_t)i + 1u;
            return true;
        }
        ++seen;
    }

    return false;
}

static bool aurora_fs_read_driver(
    void *opaque_context,
    const char *path,
    uint64_t offset,
    void *buffer,
    size_t length,
    size_t *out_read
) {
    if (opaque_context == NULL || path == NULL || buffer == NULL) {
        return false;
    }

    struct aurora_fs_driver_context *context = opaque_context;
    struct aurora_fs_dir_entry_disk directory[AURORA_FS_ROOT_ENTRY_COUNT];
    int index = driver_find_entry(context, path, directory);
    if (index < 0) {
        return false;
    }

    const struct aurora_fs_dir_entry_disk *entry = &directory[index];
    if (entry->size > AURORA_FS_BLOCK_SIZE ||
        entry->start_block < context->superblock.data_lba ||
        entry->start_block >= context->superblock.total_blocks) {
        return false;
    }

    if (offset >= entry->size) {
        if (out_read != NULL) {
            *out_read = 0u;
        }
        return true;
    }

    uint8_t block[AURORA_FS_BLOCK_SIZE];
    if (!partition_read(
            &context->partition,
            AURORA_FS_BOOTSTRAP_BASE_LBA + entry->start_block,
            1u,
            block)) {
        return false;
    }

    if (fnv1a32(block, entry->size) != entry->data_checksum) {
        return false;
    }

    size_t available = (size_t)entry->size - (size_t)offset;
    size_t to_copy = length < available ? length : available;
    uint8_t *destination = buffer;
    for (size_t i = 0u; i < to_copy; ++i) {
        destination[i] = block[(size_t)offset + i];
    }

    if (out_read != NULL) {
        *out_read = to_copy;
    }
    return true;
}

static bool aurora_fs_write_driver(
    void *opaque_context,
    const char *path,
    uint64_t offset,
    const void *buffer,
    size_t length,
    size_t *out_written
) {
    if (opaque_context == NULL || path == NULL || buffer == NULL) {
        return false;
    }

    struct aurora_fs_driver_context *context = opaque_context;
    if (context->partition.device == NULL || context->partition.device->read_only) {
        return false;
    }

    struct aurora_fs_dir_entry_disk directory[AURORA_FS_ROOT_ENTRY_COUNT];
    int index = driver_find_entry(context, path, directory);
    if (index < 0) {
        return false;
    }

    struct aurora_fs_dir_entry_disk *entry = &directory[index];
    if (entry->size > AURORA_FS_BLOCK_SIZE ||
        entry->start_block < context->superblock.data_lba ||
        entry->start_block >= context->superblock.total_blocks ||
        offset > AURORA_FS_BLOCK_SIZE ||
        length > AURORA_FS_BLOCK_SIZE - (size_t)offset) {
        return false;
    }

    uint8_t block[AURORA_FS_BLOCK_SIZE];
    zero_bytes(block, sizeof(block));
    if (entry->size != 0u) {
        if (!partition_read(
                &context->partition,
                AURORA_FS_BOOTSTRAP_BASE_LBA + entry->start_block,
                1u,
                block)) {
            return false;
        }
        if (fnv1a32(block, entry->size) != entry->data_checksum) {
            return false;
        }
    }

    const uint8_t *source = buffer;
    for (size_t i = 0u; i < length; ++i) {
        block[(size_t)offset + i] = source[i];
    }

    uint32_t new_size = entry->size;
    uint64_t write_end = offset + length;
    if (write_end > new_size) {
        new_size = (uint32_t)write_end;
    }

    if (!partition_write(
            &context->partition,
            AURORA_FS_BOOTSTRAP_BASE_LBA + entry->start_block,
            1u,
            block)) {
        return false;
    }

    entry->size = new_size;
    entry->data_checksum = fnv1a32(block, new_size);
    if (!driver_write_directory(context, directory)) {
        return false;
    }

    if (out_written != NULL) {
        *out_written = length;
    }
    return true;
}

static const struct aurora_fs_driver aurora_driver = {
    .name = "AuroraFS",
    .probe = aurora_fs_probe_driver,
    .mount = aurora_fs_mount_driver,
    .unmount = aurora_fs_unmount_driver,
    .stat = aurora_fs_stat_driver,
    .readdir = aurora_fs_readdir_driver,
    .read = aurora_fs_read_driver,
    .write = aurora_fs_write_driver
};

const struct aurora_fs_driver *aurora_fs_driver(void) {
    return &aurora_driver;
}

bool aurora_fs_bootstrap_probe(
    struct aurora_block_device *device,
    struct aurora_fs_bootstrap_result *out_result
) {
    if (device == NULL || device->read_only ||
        device->block_size != AURORA_FS_BLOCK_SIZE) {
        return false;
    }

    struct aurora_fs_bootstrap_result result = { 0 };
    struct aurora_fs_superblock_disk superblock;

    if (!load_superblock(device, &superblock)) {
        if (!format_filesystem(device, &superblock)) {
            return false;
        }
        result.formatted = true;
    }

    struct aurora_fs_dir_entry_disk directory[AURORA_FS_ROOT_ENTRY_COUNT];
    if (!read_directory(device, directory)) {
        return false;
    }

    static const char probe_name[] = "aurora.boot-probe";
    static const uint8_t probe_data[] = {
        'A','U','R','O','R','A','-','F','S','-','P','E','R','S','I','S','T'
    };

    int found_index = -1;
    int free_index = -1;

    for (size_t i = 0u; i < AURORA_FS_ROOT_ENTRY_COUNT; ++i) {
        if (directory[i].in_use != 0u) {
            if (names_equal(directory[i].name, probe_name)) {
                found_index = (int)i;
                break;
            }
        } else if (free_index < 0) {
            free_index = (int)i;
        }
    }

    if (found_index >= 0) {
        struct aurora_fs_dir_entry_disk *entry = &directory[found_index];
        if (entry->size != sizeof(probe_data) ||
            entry->start_block < AURORA_FS_DATA_LBA ||
            entry->start_block >= superblock.total_blocks) {
            return false;
        }

        uint8_t block[AURORA_FS_BLOCK_SIZE];
        if (!block_device_read(
                device,
                AURORA_FS_BOOTSTRAP_BASE_LBA + entry->start_block,
                1u,
                block)) {
            return false;
        }

        if (fnv1a32(block, entry->size) != entry->data_checksum) {
            return false;
        }

        for (size_t i = 0u; i < sizeof(probe_data); ++i) {
            if (block[i] != probe_data[i]) {
                return false;
            }
        }

        result.reopened_existing_file = true;
    } else {
        if (free_index < 0) {
            return false;
        }

        uint32_t start_block = AURORA_FS_DATA_LBA + (uint32_t)free_index;
        if ((uint64_t)start_block >= superblock.total_blocks) {
            return false;
        }

        uint8_t block[AURORA_FS_BLOCK_SIZE];
        zero_bytes(block, sizeof(block));
        for (size_t i = 0u; i < sizeof(probe_data); ++i) {
            block[i] = probe_data[i];
        }

        if (!block_device_write(
                device,
                AURORA_FS_BOOTSTRAP_BASE_LBA + start_block,
                1u,
                block)) {
            return false;
        }

        struct aurora_fs_dir_entry_disk *entry = &directory[free_index];
        zero_bytes(entry, sizeof(*entry));
        entry->in_use = 1u;
        copy_name(entry->name, probe_name);
        entry->start_block = start_block;
        entry->size = (uint32_t)sizeof(probe_data);
        entry->data_checksum = fnv1a32(block, sizeof(probe_data));

        if (!write_directory(device, directory)) {
            return false;
        }
    }

    ++superblock.generation;
    if (!write_superblock(device, &superblock)) {
        return false;
    }

    result.generation = superblock.generation;

    if (out_result != NULL) {
        *out_result = result;
    }

    return true;
}
