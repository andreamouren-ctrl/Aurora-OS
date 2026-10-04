#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/ata_pio.h>
#include <aurora/block_device.h>
#include <aurora/log.h>

#define ATA_PRIMARY_IO       0x1F0u
#define ATA_PRIMARY_CTRL     0x3F6u

#define ATA_REG_DATA         0u
#define ATA_REG_ERROR        1u
#define ATA_REG_SECTOR_COUNT 2u
#define ATA_REG_LBA_LOW      3u
#define ATA_REG_LBA_MID      4u
#define ATA_REG_LBA_HIGH     5u
#define ATA_REG_DRIVE        6u
#define ATA_REG_STATUS       7u
#define ATA_REG_COMMAND      7u

#define ATA_STATUS_ERR 0x01u
#define ATA_STATUS_DRQ 0x08u
#define ATA_STATUS_DF  0x20u
#define ATA_STATUS_BSY 0x80u

#define ATA_CMD_IDENTIFY      0xECu
#define ATA_CMD_READ_SECTORS  0x20u
#define ATA_CMD_WRITE_SECTORS 0x30u
#define ATA_CMD_CACHE_FLUSH   0xE7u

#define ATA_SECTOR_SIZE 512u
#define ATA_POLL_LIMIT  1000000u
#define ATA_LBA28_MAX   0x0FFFFFFFull

static struct aurora_block_device primary_master;
static bool primary_ready;

static void ata_delay_400ns(void) {
    (void)arch_in8(ATA_PRIMARY_CTRL);
    (void)arch_in8(ATA_PRIMARY_CTRL);
    (void)arch_in8(ATA_PRIMARY_CTRL);
    (void)arch_in8(ATA_PRIMARY_CTRL);
}

static bool ata_wait_not_busy(void) {
    for (uint32_t i = 0u; i < ATA_POLL_LIMIT; ++i) {
        uint8_t status = arch_in8(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if ((status & ATA_STATUS_BSY) == 0u) {
            return true;
        }
    }

    return false;
}

static bool ata_wait_drq(void) {
    for (uint32_t i = 0u; i < ATA_POLL_LIMIT; ++i) {
        uint8_t status = arch_in8(ATA_PRIMARY_IO + ATA_REG_STATUS);

        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0u) {
            return false;
        }

        if ((status & ATA_STATUS_BSY) == 0u &&
            (status & ATA_STATUS_DRQ) != 0u) {
            return true;
        }
    }

    return false;
}

static bool ata_wait_write_complete(void) {
    for (uint32_t i = 0u; i < ATA_POLL_LIMIT; ++i) {
        uint8_t status = arch_in8(ATA_PRIMARY_IO + ATA_REG_STATUS);

        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0u) {
            return false;
        }

        if ((status & (ATA_STATUS_BSY | ATA_STATUS_DRQ)) == 0u) {
            return true;
        }
    }

    return false;
}

static bool ata_cache_flush(void) {
    arch_out8(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);

    if (!ata_wait_not_busy()) {
        return false;
    }

    uint8_t status = arch_in8(ATA_PRIMARY_IO + ATA_REG_STATUS);
    return (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) == 0u;
}

static bool ata_select_lba28(uint32_t lba) {
    if ((uint64_t)lba > ATA_LBA28_MAX) {
        return false;
    }

    if (!ata_wait_not_busy()) {
        return false;
    }

    arch_out8(
        ATA_PRIMARY_IO + ATA_REG_DRIVE,
        (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu))
    );
    ata_delay_400ns();
    return true;
}

static bool ata_read_one(uint32_t lba, uint8_t *buffer) {
    if (!ata_select_lba28(lba)) {
        return false;
    }

    arch_out8(ATA_PRIMARY_IO + ATA_REG_SECTOR_COUNT, 1u);
    arch_out8(ATA_PRIMARY_IO + ATA_REG_LBA_LOW, (uint8_t)(lba & 0xFFu));
    arch_out8(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFFu));
    arch_out8(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFFu));
    arch_out8(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

    if (!ata_wait_drq()) {
        return false;
    }

    for (size_t i = 0u; i < ATA_SECTOR_SIZE / 2u; ++i) {
        uint16_t word = arch_in16(ATA_PRIMARY_IO + ATA_REG_DATA);
        buffer[i * 2u] = (uint8_t)(word & 0xFFu);
        buffer[i * 2u + 1u] = (uint8_t)(word >> 8);
    }

    ata_delay_400ns();
    return true;
}

static bool ata_write_one(uint32_t lba, const uint8_t *buffer) {
    if (!ata_select_lba28(lba)) {
        return false;
    }

    arch_out8(ATA_PRIMARY_IO + ATA_REG_SECTOR_COUNT, 1u);
    arch_out8(ATA_PRIMARY_IO + ATA_REG_LBA_LOW, (uint8_t)(lba & 0xFFu));
    arch_out8(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFFu));
    arch_out8(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFFu));
    arch_out8(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

    if (!ata_wait_drq()) {
        return false;
    }

    for (size_t i = 0u; i < ATA_SECTOR_SIZE / 2u; ++i) {
        uint16_t word = (uint16_t)buffer[i * 2u]
            | ((uint16_t)buffer[i * 2u + 1u] << 8);
        arch_out16(ATA_PRIMARY_IO + ATA_REG_DATA, word);
    }

    /* PIO WRITE SECTORS is complete only after the device clears both BSY and
       DRQ. The caller may then submit the next sector or issue one cache flush
       for the completed batch. */
    ata_delay_400ns();
    return ata_wait_write_complete();
}

static bool ata_block_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    (void)device;

    uint8_t *out = (uint8_t *)buffer;

    for (uint32_t i = 0u; i < block_count; ++i) {
        uint64_t current = lba + i;
        if (current > ATA_LBA28_MAX ||
            !ata_read_one((uint32_t)current, out + (size_t)i * ATA_SECTOR_SIZE)) {
            return false;
        }
    }

    return true;
}

static bool ata_block_flush(struct aurora_block_device *device) {
    (void)device;
    return primary_ready && ata_cache_flush();
}

static bool ata_block_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    (void)device;
    const uint8_t *in = (const uint8_t *)buffer;

    for (uint32_t i = 0u; i < block_count; ++i) {
        uint64_t current = lba + i;
        if (current > ATA_LBA28_MAX ||
            !ata_write_one((uint32_t)current, in + (size_t)i * ATA_SECTOR_SIZE)) {
            return false;
        }
    }

    /* One durable flush is sufficient for all sectors submitted in this block
       layer request and avoids thousands of redundant flush commands while
       AuroraFS writes 4 KiB metadata blocks. */
    return ata_cache_flush();
}

bool ata_pio_primary_master_init(void) {
    primary_ready = false;

    arch_out8(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0u);
    ata_delay_400ns();

    arch_out8(ATA_PRIMARY_IO + ATA_REG_SECTOR_COUNT, 0u);
    arch_out8(ATA_PRIMARY_IO + ATA_REG_LBA_LOW, 0u);
    arch_out8(ATA_PRIMARY_IO + ATA_REG_LBA_MID, 0u);
    arch_out8(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH, 0u);
    arch_out8(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = arch_in8(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status == 0u) {
        return false;
    }

    if (!ata_wait_not_busy()) {
        return false;
    }

    if (arch_in8(ATA_PRIMARY_IO + ATA_REG_LBA_MID) != 0u ||
        arch_in8(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH) != 0u) {
        return false;
    }

    if (!ata_wait_drq()) {
        return false;
    }

    uint16_t identify[256];
    for (size_t i = 0u; i < 256u; ++i) {
        identify[i] = arch_in16(ATA_PRIMARY_IO + ATA_REG_DATA);
    }

    uint64_t sectors = (uint64_t)identify[60]
        | ((uint64_t)identify[61] << 16);

    if (sectors == 0u) {
        return false;
    }

    primary_master.name = "ata-primary-master";
    primary_master.block_size = ATA_SECTOR_SIZE;
    primary_master.block_count = sectors;
    primary_master.read_only = false;
    primary_master.context = NULL;
    primary_master.read_blocks = ata_block_read;
    primary_master.write_blocks = ata_block_write;
    primary_master.flush = ata_block_flush;

    primary_ready = true;
    return true;
}

struct aurora_block_device *ata_pio_primary_master_device(void) {
    return primary_ready ? &primary_master : NULL;
}

static bool buffer_starts_with_test_signature(const uint8_t *buffer) {
    static const char signature[] = "AURORA-STORAGE-TEST-V1";

    for (size_t i = 0u; i < sizeof(signature) - 1u; ++i) {
        if (buffer[i] != (uint8_t)signature[i]) {
            return false;
        }
    }

    return true;
}

bool ata_pio_ci_probe(void) {
    if (!primary_ready || primary_master.block_count < 3u) {
        log_line("[ata] probe failure: device not ready");
        return false;
    }

    uint8_t header[ATA_SECTOR_SIZE];
    if (!block_device_read(&primary_master, 0u, 1u, header)) {
        log_line("[ata] probe failure: signature sector read");
        return false;
    }

    if (!buffer_starts_with_test_signature(header)) {
        log_line("[ata] probe failure: signature mismatch");
        return false;
    }

    log_line("[ata] probe stage: signature verified");

    uint8_t write_buffer[ATA_SECTOR_SIZE];
    uint8_t read_buffer[ATA_SECTOR_SIZE];

    for (size_t i = 0u; i < ATA_SECTOR_SIZE; ++i) {
        write_buffer[i] = (uint8_t)((i * 29u + 0x5Au) & 0xFFu);
        read_buffer[i] = 0u;
    }

    if (!block_device_write(&primary_master, 1u, 1u, write_buffer)) {
        log_line("[ata] probe failure: write/flush batch");
        return false;
    }

    log_line("[ata] probe stage: write/flush batch completed");

    if (!block_device_read(&primary_master, 1u, 1u, read_buffer)) {
        log_line("[ata] probe failure: readback");
        return false;
    }

    log_line("[ata] probe stage: readback completed");

    for (size_t i = 0u; i < ATA_SECTOR_SIZE; ++i) {
        if (read_buffer[i] != write_buffer[i]) {
            log_line("[ata] probe failure: readback mismatch");
            return false;
        }
    }

    log_line("[ata] probe stage: readback matched");
    return true;
}
