#include <stddef.h>
#include <stdint.h>

#include <aurora/partition.h>

#define PARTITION_MIN_BLOCK_SIZE 512u
#define PARTITION_MAX_BLOCK_SIZE 4096u
#define MBR_SIGNATURE_OFFSET 510u
#define MBR_ENTRY_OFFSET 446u
#define MBR_ENTRY_SIZE 16u
#define MBR_ENTRY_COUNT 4u
#define GPT_PRIMARY_HEADER_LBA 1u
#define GPT_HEADER_MIN_SIZE 92u
#define GPT_REVISION_1_0 0x00010000u
#define GPT_HEADER_CRC_OFFSET 16u
#define GPT_ENTRY_MIN_SIZE 128u
#define GPT_ENTRY_MAX_SIZE 4096u
#define GPT_ENTRY_READ_LIMIT 128u
#define GPT_ENTRY_ARRAY_MAX_BYTES (16u * 1024u * 1024u)

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p) {
    return (uint64_t)read_le32(p)
        | ((uint64_t)read_le32(p + 4) << 32);
}

static bool supported_logical_block_size(uint32_t block_size) {
    return block_size == 512u ||
        block_size == 1024u ||
        block_size == 2048u ||
        block_size == 4096u;
}

static uint32_t crc32_update(
    uint32_t crc,
    const uint8_t *data,
    size_t length
) {
    for (size_t i = 0u; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
    return ~crc32_update(0xFFFFFFFFu, data, length);
}

static bool guid_is_zero(const uint8_t guid[16]) {
    for (size_t i = 0u; i < 16u; ++i) {
        if (guid[i] != 0u) {
            return false;
        }
    }
    return true;
}

static void copy_guid(uint8_t destination[16], const uint8_t source[16]) {
    for (size_t i = 0u; i < 16u; ++i) {
        destination[i] = source[i];
    }
}

static void clear_partition(struct aurora_partition *partition) {
    uint8_t *bytes = (uint8_t *)partition;
    for (size_t i = 0u; i < sizeof(*partition); ++i) {
        bytes[i] = 0u;
    }
}

static void decode_gpt_name(
    char destination[AURORA_PARTITION_NAME_MAX],
    const uint8_t *utf16le
) {
    size_t out = 0u;
    for (size_t i = 0u; i < 36u && out + 1u < AURORA_PARTITION_NAME_MAX; ++i) {
        uint16_t ch = (uint16_t)utf16le[i * 2u]
            | ((uint16_t)utf16le[i * 2u + 1u] << 8);
        if (ch == 0u) {
            break;
        }
        destination[out++] = ch <= 0x7Fu ? (char)ch : '?';
    }
    destination[out] = '\0';
}

static bool gpt_signature_valid(const uint8_t *header) {
    static const uint8_t signature[8] = {'E','F','I',' ','P','A','R','T'};
    for (size_t i = 0u; i < sizeof(signature); ++i) {
        if (header[i] != signature[i]) {
            return false;
        }
    }
    return true;
}

static bool gpt_header_crc_valid(uint8_t *header, uint32_t header_size) {
    uint32_t expected = read_le32(header + GPT_HEADER_CRC_OFFSET);
    uint8_t saved[4] = {
        header[GPT_HEADER_CRC_OFFSET + 0u],
        header[GPT_HEADER_CRC_OFFSET + 1u],
        header[GPT_HEADER_CRC_OFFSET + 2u],
        header[GPT_HEADER_CRC_OFFSET + 3u]
    };

    for (size_t i = 0u; i < 4u; ++i) {
        header[GPT_HEADER_CRC_OFFSET + i] = 0u;
    }

    uint32_t actual = crc32_bytes(header, header_size);

    for (size_t i = 0u; i < 4u; ++i) {
        header[GPT_HEADER_CRC_OFFSET + i] = saved[i];
    }

    return actual == expected;
}

static bool gpt_entry_array_crc_valid(
    struct aurora_block_device *device,
    uint64_t entries_lba,
    uint32_t entry_count,
    uint32_t entry_size,
    uint32_t expected_crc
) {
    if (entry_count == 0u || entry_size == 0u) {
        return false;
    }

    uint64_t total_bytes = (uint64_t)entry_count * (uint64_t)entry_size;
    if (total_bytes / entry_size != entry_count ||
        total_bytes > GPT_ENTRY_ARRAY_MAX_BYTES) {
        return false;
    }

    uint32_t block_size = device->block_size;
    uint64_t blocks_needed =
        (total_bytes + (uint64_t)block_size - 1u) / block_size;

    if (entries_lba >= device->block_count ||
        blocks_needed > device->block_count - entries_lba) {
        return false;
    }

    uint8_t block[PARTITION_MAX_BLOCK_SIZE];
    uint64_t remaining = total_bytes;
    uint32_t crc = 0xFFFFFFFFu;

    for (uint64_t block_index = 0u;
         block_index < blocks_needed;
         ++block_index) {
        if (!block_device_read(
                device,
                entries_lba + block_index,
                1u,
                block)) {
            return false;
        }

        size_t chunk = remaining < block_size
            ? (size_t)remaining
            : (size_t)block_size;
        crc = crc32_update(crc, block, chunk);
        remaining -= chunk;
    }

    return (~crc) == expected_crc && remaining == 0u;
}

static bool scan_gpt_at(
    struct aurora_block_device *device,
    uint64_t header_lba,
    struct aurora_partition *out,
    size_t capacity,
    size_t *out_found
) {
    if (out_found == NULL || header_lba >= device->block_count) {
        return false;
    }
    *out_found = 0u;

    uint32_t block_size = device->block_size;
    uint8_t header[PARTITION_MAX_BLOCK_SIZE];

    if (!block_device_read(device, header_lba, 1u, header) ||
        !gpt_signature_valid(header)) {
        return false;
    }

    uint32_t revision = read_le32(header + 8u);
    uint32_t header_size = read_le32(header + 12u);
    uint64_t current_lba = read_le64(header + 24u);
    uint64_t backup_lba = read_le64(header + 32u);
    uint64_t first_usable_lba = read_le64(header + 40u);
    uint64_t last_usable_lba = read_le64(header + 48u);
    uint64_t entries_lba = read_le64(header + 72u);
    uint32_t entry_count = read_le32(header + 80u);
    uint32_t entry_size = read_le32(header + 84u);
    uint32_t entry_array_crc = read_le32(header + 88u);

    if (revision != GPT_REVISION_1_0 ||
        header_size < GPT_HEADER_MIN_SIZE ||
        header_size > block_size ||
        current_lba != header_lba ||
        backup_lba >= device->block_count ||
        backup_lba == current_lba ||
        first_usable_lba > last_usable_lba ||
        last_usable_lba >= device->block_count ||
        entries_lba >= device->block_count ||
        entry_count == 0u ||
        entry_size < GPT_ENTRY_MIN_SIZE ||
        entry_size > GPT_ENTRY_MAX_SIZE ||
        (entry_size % GPT_ENTRY_MIN_SIZE) != 0u) {
        return false;
    }

    if (!gpt_header_crc_valid(header, header_size) ||
        !gpt_entry_array_crc_valid(
            device,
            entries_lba,
            entry_count,
            entry_size,
            entry_array_crc)) {
        return false;
    }

    uint32_t scan_count = entry_count;
    if (scan_count > GPT_ENTRY_READ_LIMIT) {
        scan_count = GPT_ENTRY_READ_LIMIT;
    }

    uint8_t entry_window[PARTITION_MAX_BLOCK_SIZE * 2u];
    size_t found = 0u;

    for (uint32_t index = 0u; index < scan_count && found < capacity; ++index) {
        uint64_t byte_offset = (uint64_t)index * entry_size;
        uint64_t block_delta = byte_offset / block_size;
        uint32_t offset = (uint32_t)(byte_offset % block_size);

        if (block_delta > UINT64_MAX - entries_lba) {
            break;
        }

        uint64_t lba = entries_lba + block_delta;
        if (lba >= device->block_count) {
            break;
        }

        uint32_t blocks_needed =
            offset + GPT_ENTRY_MIN_SIZE > block_size ? 2u : 1u;
        if ((uint64_t)blocks_needed > device->block_count - lba) {
            break;
        }

        if (!block_device_read(device, lba, blocks_needed, entry_window)) {
            break;
        }

        const uint8_t *entry = entry_window + offset;
        if (guid_is_zero(entry)) {
            continue;
        }

        uint64_t first_lba = read_le64(entry + 32u);
        uint64_t last_lba = read_le64(entry + 40u);
        if (first_lba > last_lba ||
            first_lba < first_usable_lba ||
            last_lba > last_usable_lba) {
            continue;
        }

        struct aurora_partition *partition = &out[found];
        clear_partition(partition);
        partition->device = device;
        partition->scheme = AURORA_PARTITION_SCHEME_GPT;
        partition->index = index + 1u;
        partition->first_lba = first_lba;
        partition->block_count = last_lba - first_lba + 1u;
        copy_guid(partition->type_guid, entry);
        copy_guid(partition->unique_guid, entry + 16u);
        decode_gpt_name(partition->name, entry + 56u);
        ++found;
    }

    *out_found = found;
    return true;
}

static size_t scan_mbr(
    struct aurora_block_device *device,
    const uint8_t *sector,
    struct aurora_partition *out,
    size_t capacity
) {
    size_t found = 0u;

    for (uint32_t index = 0u; index < MBR_ENTRY_COUNT && found < capacity; ++index) {
        const uint8_t *entry = sector + MBR_ENTRY_OFFSET + index * MBR_ENTRY_SIZE;
        uint8_t type = entry[4];
        uint32_t first_lba = read_le32(entry + 8u);
        uint32_t block_count = read_le32(entry + 12u);

        if (type == 0u || block_count == 0u || type == 0xEEu) {
            continue;
        }

        uint64_t end = (uint64_t)first_lba + block_count;
        if (end > device->block_count) {
            continue;
        }

        struct aurora_partition *partition = &out[found];
        clear_partition(partition);
        partition->device = device;
        partition->scheme = AURORA_PARTITION_SCHEME_MBR;
        partition->index = index + 1u;
        partition->first_lba = first_lba;
        partition->block_count = block_count;
        partition->mbr_type = type;
        ++found;
    }

    return found;
}

size_t partition_scan(
    struct aurora_block_device *device,
    struct aurora_partition *out_partitions,
    size_t capacity
) {
    if (device == NULL || out_partitions == NULL || capacity == 0u ||
        !supported_logical_block_size(device->block_size) ||
        device->block_size < PARTITION_MIN_BLOCK_SIZE ||
        device->block_size > PARTITION_MAX_BLOCK_SIZE ||
        device->block_count == 0u) {
        return 0u;
    }

    uint8_t first_block[PARTITION_MAX_BLOCK_SIZE];
    if (!block_device_read(device, 0u, 1u, first_block)) {
        return 0u;
    }

    bool has_mbr_signature = first_block[MBR_SIGNATURE_OFFSET] == 0x55u &&
        first_block[MBR_SIGNATURE_OFFSET + 1u] == 0xAAu;

    if (has_mbr_signature) {
        bool protective_mbr = false;
        for (uint32_t index = 0u; index < MBR_ENTRY_COUNT; ++index) {
            const uint8_t *entry =
                first_block + MBR_ENTRY_OFFSET + index * MBR_ENTRY_SIZE;
            if (entry[4] == 0xEEu) {
                protective_mbr = true;
                break;
            }
        }

        if (protective_mbr) {
            size_t gpt_count = 0u;
            if (scan_gpt_at(
                    device,
                    GPT_PRIMARY_HEADER_LBA,
                    out_partitions,
                    capacity,
                    &gpt_count)) {
                if (gpt_count != 0u) {
                    return gpt_count;
                }
            } else if (device->block_count > 1u) {
                uint64_t backup_header_lba = device->block_count - 1u;
                if (scan_gpt_at(
                        device,
                        backup_header_lba,
                        out_partitions,
                        capacity,
                        &gpt_count) &&
                    gpt_count != 0u) {
                    return gpt_count;
                }
            }
        }

        size_t mbr_count = scan_mbr(
            device,
            first_block,
            out_partitions,
            capacity
        );
        if (mbr_count != 0u) {
            return mbr_count;
        }
    }

    clear_partition(&out_partitions[0]);
    out_partitions[0].device = device;
    out_partitions[0].scheme = AURORA_PARTITION_SCHEME_WHOLE_DEVICE;
    out_partitions[0].index = 0u;
    out_partitions[0].first_lba = 0u;
    out_partitions[0].block_count = device->block_count;
    return 1u;
}

bool partition_read(
    const struct aurora_partition *partition,
    uint64_t relative_lba,
    uint32_t block_count,
    void *buffer
) {
    if (partition == NULL || partition->device == NULL || buffer == NULL ||
        block_count == 0u || relative_lba >= partition->block_count ||
        (uint64_t)block_count > partition->block_count - relative_lba) {
        return false;
    }

    return block_device_read(
        partition->device,
        partition->first_lba + relative_lba,
        block_count,
        buffer
    );
}

bool partition_write(
    const struct aurora_partition *partition,
    uint64_t relative_lba,
    uint32_t block_count,
    const void *buffer
) {
    if (partition == NULL || partition->device == NULL || buffer == NULL ||
        block_count == 0u || relative_lba >= partition->block_count ||
        (uint64_t)block_count > partition->block_count - relative_lba) {
        return false;
    }

    return block_device_write(
        partition->device,
        partition->first_lba + relative_lba,
        block_count,
        buffer
    );
}
