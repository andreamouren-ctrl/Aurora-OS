#include "machine_secret_codec.h"

#include <string.h>

static const uint8_t disk_magic[8u] = {
    'A', 'U', 'R', 'M', 'S', 'V', '1', 0
};

static void store_u32_le(uint8_t out[4u], uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
    out[2] = (uint8_t)(value >> 16u);
    out[3] = (uint8_t)(value >> 24u);
}

static uint32_t load_u32_le(const uint8_t in[4u]) {
    return (uint32_t)in[0] |
        ((uint32_t)in[1] << 8u) |
        ((uint32_t)in[2] << 16u) |
        ((uint32_t)in[3] << 24u);
}

static void store_u64_le(uint8_t out[8u], uint64_t value) {
    for (size_t i = 0u; i < 8u; ++i) {
        out[i] = (uint8_t)(value >> (i * 8u));
    }
}

static uint64_t load_u64_le(const uint8_t in[8u]) {
    uint64_t value = 0u;
    for (size_t i = 0u; i < 8u; ++i) {
        value |= ((uint64_t)in[i]) << (i * 8u);
    }
    return value;
}

bool aurora_identity_machine_secret_record_encode(
    const struct aurora_identity_machine_secret_record *record,
    uint8_t out[AURORA_IDENTITY_MACHINE_SECRET_DISK_SIZE]) {
    size_t offset = 0u;
    if (record == NULL || out == NULL) return false;

    memcpy(out + offset, disk_magic, sizeof(disk_magic));
    offset += sizeof(disk_magic);
    store_u32_le(out + offset, record->record_version);
    offset += 4u;
    store_u64_le(out + offset, record->generation);
    offset += 8u;
    memcpy(out + offset, record->secret.bytes, sizeof(record->secret.bytes));
    offset += sizeof(record->secret.bytes);
    memcpy(out + offset, record->checksum, sizeof(record->checksum));
    return true;
}

bool aurora_identity_machine_secret_record_decode(
    const uint8_t *input,
    size_t input_size,
    struct aurora_identity_machine_secret_record *out_record) {
    size_t offset = 0u;
    if (input == NULL || out_record == NULL ||
        input_size != AURORA_IDENTITY_MACHINE_SECRET_DISK_SIZE ||
        memcmp(input, disk_magic, sizeof(disk_magic)) != 0) {
        return false;
    }

    memset(out_record, 0, sizeof(*out_record));
    offset += sizeof(disk_magic);
    out_record->record_version = load_u32_le(input + offset);
    offset += 4u;
    out_record->generation = load_u64_le(input + offset);
    offset += 8u;
    memcpy(out_record->secret.bytes, input + offset, sizeof(out_record->secret.bytes));
    offset += sizeof(out_record->secret.bytes);
    memcpy(out_record->checksum, input + offset, sizeof(out_record->checksum));
    return true;
}
