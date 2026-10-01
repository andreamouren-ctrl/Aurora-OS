#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs.h>
#include <aurora/block_device.h>

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

    if (!magic_valid(superblock) ||
        superblock->version != AURORA_FS_VERSION ||
        superblock->block_size != AURORA_FS_BLOCK_SIZE ||
        superblock->root_lba != AURORA_FS_ROOT_LBA ||
        superblock->data_lba != AURORA_FS_DATA_LBA) {
        return false;
    }

    return superblock_checksum(superblock) == superblock->metadata_checksum;
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
