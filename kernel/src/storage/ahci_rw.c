#include <stddef.h>
#include <stdint.h>

#include <aurora/ahci.h>
#include <aurora/block_device.h>
#include <aurora/log.h>
#include <aurora/pci.h>
#include <aurora/pmm.h>
#include <aurora/vmm.h>

#define PCI_CLASS_MASS_STORAGE 0x01u
#define PCI_SUBCLASS_SATA      0x06u
#define PCI_PROGIF_AHCI        0x01u

#define AHCI_RW_MMIO_VIRTUAL    0xFFFFFFFFB0204000ull
#define AHCI_RW_MMIO_PAGE_COUNT 3u
#define AHCI_PAGE_SIZE          4096ull

#define AHCI_PORT_BASE          0x100u
#define AHCI_PORT_STRIDE        0x80u
#define AHCI_PORT_CLB           0x00u
#define AHCI_PORT_CLBU          0x04u
#define AHCI_PORT_FB            0x08u
#define AHCI_PORT_FBU           0x0Cu
#define AHCI_PORT_IS            0x10u
#define AHCI_PORT_CMD           0x18u
#define AHCI_PORT_TFD           0x20u
#define AHCI_PORT_SERR          0x30u
#define AHCI_PORT_CI            0x38u

#define AHCI_CMD_ST             (1u << 0)
#define AHCI_CMD_FRE            (1u << 4)
#define AHCI_CMD_FR             (1u << 14)
#define AHCI_CMD_CR             (1u << 15)
#define AHCI_PORT_IS_TFES       (1u << 30)
#define AHCI_TFD_ERR            (1u << 0)
#define AHCI_TFD_DRQ            (1u << 3)
#define AHCI_TFD_BSY            (1u << 7)

#define ATA_CMD_WRITE_DMA_EXT   0x35u
#define ATA_CMD_FLUSH_CACHE_EXT 0xEAu
#define ATA_DEVICE_LBA          0x40u
#define FIS_TYPE_REG_H2D        0x27u
#define AHCI_WAIT_LIMIT         1000000u

struct ahci_command_header {
    uint16_t flags;
    uint16_t prdt_length;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed));

struct ahci_prdt_entry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc_i;
} __attribute__((packed));

struct ahci_command_table_one_prdt {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    struct ahci_prdt_entry prdt[1];
} __attribute__((packed));

struct ahci_rw_context {
    uint8_t port;
    struct aurora_block_device *read_device;
};

static volatile uint8_t *rw_mmio;
static struct ahci_rw_context rw_context;
static struct aurora_block_device rw_device;
static bool rw_ready;

static uint8_t probe_original[4096];
static uint8_t probe_pattern[4096];
static uint8_t probe_readback[4096];

static uint32_t mmio_read32(uint32_t offset) {
    volatile uint32_t *reg = (volatile uint32_t *)(rw_mmio + offset);
    return *reg;
}

static void mmio_write32(uint32_t offset, uint32_t value) {
    volatile uint32_t *reg = (volatile uint32_t *)(rw_mmio + offset);
    *reg = value;
    (void)*reg;
}

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0u; i < length; ++i) out[i] = in[i];
}

static bool bytes_equal(const void *a, const void *b, size_t length) {
    const uint8_t *left = (const uint8_t *)a;
    const uint8_t *right = (const uint8_t *)b;
    for (size_t i = 0u; i < length; ++i) {
        if (left[i] != right[i]) return false;
    }
    return true;
}

static bool map_abar(void) {
    struct aurora_pci_device pci;
    if (!pci_find_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_SATA,
                        PCI_PROGIF_AHCI, &pci)) {
        return false;
    }

    uint32_t bar5 = pci_read_bar32(&pci, 5u);
    if ((bar5 & 0x1u) != 0u || (bar5 & 0x6u) != 0u ||
        !pci_enable_memory_bus_master(&pci)) {
        return false;
    }

    uint64_t abar = (uint64_t)(bar5 & ~0xFu);
    uint64_t physical_base = abar & ~(AHCI_PAGE_SIZE - 1u);
    uint64_t page_offset = abar & (AHCI_PAGE_SIZE - 1u);

    for (uint64_t page = 0u; page < AHCI_RW_MMIO_PAGE_COUNT; ++page) {
        uint64_t virtual_address = AHCI_RW_MMIO_VIRTUAL + page * AHCI_PAGE_SIZE;
        uint64_t physical_address = physical_base + page * AHCI_PAGE_SIZE;
        if (!vmm_map_page(virtual_address, physical_address,
                          VMM_FLAG_WRITE | VMM_FLAG_NO_CACHE)) {
            uint64_t existing = 0u;
            if (!vmm_translate(virtual_address, &existing) ||
                (existing & ~(AHCI_PAGE_SIZE - 1u)) != physical_address) {
                return false;
            }
        }
    }

    rw_mmio = (volatile uint8_t *)(uintptr_t)(AHCI_RW_MMIO_VIRTUAL + page_offset);
    return true;
}

static bool wait_cmd_clear(uint32_t base, uint32_t mask) {
    for (uint32_t i = 0u; i < AHCI_WAIT_LIMIT; ++i) {
        if ((mmio_read32(base + AHCI_PORT_CMD) & mask) == 0u) return true;
    }
    return false;
}

static bool stop_port(uint32_t base) {
    uint32_t cmd = mmio_read32(base + AHCI_PORT_CMD);
    cmd &= ~AHCI_CMD_ST;
    mmio_write32(base + AHCI_PORT_CMD, cmd);
    if (!wait_cmd_clear(base, AHCI_CMD_CR)) return false;

    cmd = mmio_read32(base + AHCI_PORT_CMD);
    cmd &= ~AHCI_CMD_FRE;
    mmio_write32(base + AHCI_PORT_CMD, cmd);
    return wait_cmd_clear(base, AHCI_CMD_FR);
}

static void start_port(uint32_t base) {
    uint32_t cmd = mmio_read32(base + AHCI_PORT_CMD);
    cmd |= AHCI_CMD_FRE;
    mmio_write32(base + AHCI_PORT_CMD, cmd);
    cmd |= AHCI_CMD_ST;
    mmio_write32(base + AHCI_PORT_CMD, cmd);
}

static bool wait_port_ready(uint32_t base) {
    for (uint32_t i = 0u; i < AHCI_WAIT_LIMIT; ++i) {
        uint32_t tfd = mmio_read32(base + AHCI_PORT_TFD);
        if ((tfd & (AHCI_TFD_BSY | AHCI_TFD_DRQ)) == 0u) return true;
    }
    return false;
}

static bool wait_slot_complete(uint32_t base) {
    for (uint32_t i = 0u; i < AHCI_WAIT_LIMIT; ++i) {
        uint32_t is = mmio_read32(base + AHCI_PORT_IS);
        uint32_t tfd = mmio_read32(base + AHCI_PORT_TFD);
        if ((is & AHCI_PORT_IS_TFES) != 0u || (tfd & AHCI_TFD_ERR) != 0u) {
            return false;
        }
        if ((mmio_read32(base + AHCI_PORT_CI) & 1u) == 0u) return true;
    }
    return false;
}

static bool setup_command(uint8_t port,
                          uint64_t cl_phys,
                          uint64_t fis_phys,
                          uint64_t table_phys,
                          uint32_t *out_base,
                          struct ahci_command_header **out_header,
                          struct ahci_command_table_one_prdt **out_table) {
    uint32_t base = AHCI_PORT_BASE + (uint32_t)port * AHCI_PORT_STRIDE;
    void *cl = pmm_phys_to_virt(cl_phys);
    void *fis = pmm_phys_to_virt(fis_phys);
    void *table_memory = pmm_phys_to_virt(table_phys);

    zero_bytes(cl, AURORA_PAGE_SIZE);
    zero_bytes(fis, AURORA_PAGE_SIZE);
    zero_bytes(table_memory, AURORA_PAGE_SIZE);

    if (!stop_port(base)) return false;

    mmio_write32(base + AHCI_PORT_CLB, (uint32_t)cl_phys);
    mmio_write32(base + AHCI_PORT_CLBU, (uint32_t)(cl_phys >> 32));
    mmio_write32(base + AHCI_PORT_FB, (uint32_t)fis_phys);
    mmio_write32(base + AHCI_PORT_FBU, (uint32_t)(fis_phys >> 32));
    mmio_write32(base + AHCI_PORT_SERR, 0xFFFFFFFFu);
    mmio_write32(base + AHCI_PORT_IS, 0xFFFFFFFFu);

    struct ahci_command_header *header = (struct ahci_command_header *)cl;
    header[0].flags = 5u;
    header[0].ctba = (uint32_t)table_phys;
    header[0].ctbau = (uint32_t)(table_phys >> 32);

    *out_base = base;
    *out_header = header;
    *out_table = (struct ahci_command_table_one_prdt *)table_memory;
    return true;
}

static bool issue_write_blocks(
    uint8_t port,
    uint64_t lba,
    uint32_t sector_size,
    uint16_t sector_count,
    const void *buffer
) {
    if (buffer == NULL ||
        sector_size == 0u ||
        sector_count == 0u ||
        sector_size > AURORA_PAGE_SIZE ||
        (uint64_t)sector_size * sector_count > AURORA_PAGE_SIZE ||
        lba > 0x0000FFFFFFFFFFFFull ||
        (uint64_t)sector_count - 1u > 0x0000FFFFFFFFFFFFull - lba) {
        return false;
    }

    uint64_t cl_phys = pmm_alloc_page();
    uint64_t fis_phys = pmm_alloc_page();
    uint64_t table_phys = pmm_alloc_page();
    uint64_t data_phys = pmm_alloc_page();
    if (cl_phys == 0u || fis_phys == 0u || table_phys == 0u || data_phys == 0u) {
        if (cl_phys) pmm_free_page(cl_phys);
        if (fis_phys) pmm_free_page(fis_phys);
        if (table_phys) pmm_free_page(table_phys);
        if (data_phys) pmm_free_page(data_phys);
        return false;
    }

    uint32_t byte_count = sector_size * (uint32_t)sector_count;
    copy_bytes(pmm_phys_to_virt(data_phys), buffer, byte_count);

    uint32_t base;
    struct ahci_command_header *header;
    struct ahci_command_table_one_prdt *table;
    if (!setup_command(port, cl_phys, fis_phys, table_phys,
                       &base, &header, &table)) {
        goto fail;
    }

    header[0].flags = 5u | (1u << 6); /* Register H2D + write. */
    header[0].prdt_length = 1u;

    table->prdt[0].dba = (uint32_t)data_phys;
    table->prdt[0].dbau = (uint32_t)(data_phys >> 32);
    table->prdt[0].dbc_i = byte_count - 1u;

    uint8_t *cfis = table->cfis;
    cfis[0] = FIS_TYPE_REG_H2D;
    cfis[1] = 0x80u;
    cfis[2] = ATA_CMD_WRITE_DMA_EXT;
    cfis[4] = (uint8_t)lba;
    cfis[5] = (uint8_t)(lba >> 8);
    cfis[6] = (uint8_t)(lba >> 16);
    cfis[7] = ATA_DEVICE_LBA;
    cfis[8] = (uint8_t)(lba >> 24);
    cfis[9] = (uint8_t)(lba >> 32);
    cfis[10] = (uint8_t)(lba >> 40);
    cfis[12] = (uint8_t)sector_count;
    cfis[13] = (uint8_t)(sector_count >> 8);

    start_port(base);
    if (!wait_port_ready(base)) goto fail_running;
    mmio_write32(base + AHCI_PORT_CI, 1u);
    if (!wait_slot_complete(base)) goto fail_running;

    (void)stop_port(base);
    pmm_free_page(cl_phys);
    pmm_free_page(fis_phys);
    pmm_free_page(table_phys);
    pmm_free_page(data_phys);
    return true;

fail_running:
    (void)stop_port(base);
fail:
    pmm_free_page(cl_phys);
    pmm_free_page(fis_phys);
    pmm_free_page(table_phys);
    pmm_free_page(data_phys);
    return false;
}

static bool issue_flush(uint8_t port) {
    uint64_t cl_phys = pmm_alloc_page();
    uint64_t fis_phys = pmm_alloc_page();
    uint64_t table_phys = pmm_alloc_page();
    if (cl_phys == 0u || fis_phys == 0u || table_phys == 0u) {
        if (cl_phys) pmm_free_page(cl_phys);
        if (fis_phys) pmm_free_page(fis_phys);
        if (table_phys) pmm_free_page(table_phys);
        return false;
    }

    uint32_t base;
    struct ahci_command_header *header;
    struct ahci_command_table_one_prdt *table;
    if (!setup_command(port, cl_phys, fis_phys, table_phys,
                       &base, &header, &table)) {
        goto fail;
    }

    header[0].prdt_length = 0u;
    uint8_t *cfis = table->cfis;
    cfis[0] = FIS_TYPE_REG_H2D;
    cfis[1] = 0x80u;
    cfis[2] = ATA_CMD_FLUSH_CACHE_EXT;

    start_port(base);
    if (!wait_port_ready(base)) goto fail_running;
    mmio_write32(base + AHCI_PORT_CI, 1u);
    if (!wait_slot_complete(base)) goto fail_running;

    (void)stop_port(base);
    pmm_free_page(cl_phys);
    pmm_free_page(fis_phys);
    pmm_free_page(table_phys);
    return true;

fail_running:
    (void)stop_port(base);
fail:
    pmm_free_page(cl_phys);
    pmm_free_page(fis_phys);
    pmm_free_page(table_phys);
    return false;
}

static bool rw_read(struct aurora_block_device *device,
                    uint64_t lba,
                    uint32_t block_count,
                    void *buffer) {
    (void)device;
    return block_device_read(rw_context.read_device, lba, block_count, buffer);
}

static bool rw_write(struct aurora_block_device *device,
                     uint64_t lba,
                     uint32_t block_count,
                     const void *buffer) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count ||
        (uint64_t)block_count > device->block_count - lba) {
        return false;
    }

    const uint8_t *source = (const uint8_t *)buffer;
    uint32_t blocks_per_command =
        AURORA_PAGE_SIZE / device->block_size;

    if (blocks_per_command == 0u) {
        return false;
    }

    uint32_t written = 0u;

    while (written < block_count) {
        uint32_t remaining = block_count - written;
        uint32_t chunk =
            remaining < blocks_per_command
                ? remaining
                : blocks_per_command;

        if (chunk > UINT16_MAX ||
            !issue_write_blocks(
                rw_context.port,
                lba + written,
                device->block_size,
                (uint16_t)chunk,
                source +
                    (size_t)written * device->block_size)) {
            return false;
        }

        written += chunk;
    }

    return true;
}

static bool rw_flush(struct aurora_block_device *device) {
    (void)device;
    return issue_flush(rw_context.port);
}

bool ahci_rw_block_device_init(void) {
    if (!ahci_primary_block_device_init()) return false;

    struct aurora_block_device *read_device = ahci_primary_block_device();
    struct aurora_ahci_identify_result identify;
    if (read_device == NULL || !ahci_identify_first(&identify) || !map_abar()) {
        rw_ready = false;
        return false;
    }

    rw_context.port = identify.port;
    rw_context.read_device = read_device;

    rw_device.name = "ahci-sata0";
    rw_device.block_size = read_device->block_size;
    rw_device.block_count = read_device->block_count;
    rw_device.read_only = false;
    rw_device.context = &rw_context;
    rw_device.read_blocks = rw_read;
    rw_device.write_blocks = rw_write;
    rw_device.flush = rw_flush;
    rw_ready = true;
    return true;
}

struct aurora_block_device *ahci_rw_block_device(void) {
    return rw_ready ? &rw_device : NULL;
}

bool ahci_rw_signed_probe(void) {
    static const uint8_t signature[] = "AURORA-AHCI-RW-TEST-V1";
    struct aurora_block_device *device = ahci_rw_block_device();
    if (device == NULL || device->block_size > sizeof(probe_original) ||
        device->block_count == 0u) {
        return false;
    }

    uint64_t lba = device->block_count - 1u;
    zero_bytes(probe_original, sizeof(probe_original));
    zero_bytes(probe_pattern, sizeof(probe_pattern));
    zero_bytes(probe_readback, sizeof(probe_readback));

    if (!block_device_read(device, lba, 1u, probe_original)) {
        log_line("[ahci-rw] probe failed: last-sector read");
        return false;
    }

    bool test_media =
        bytes_equal(probe_original, signature, sizeof(signature) - 1u);

    if (!test_media) {
        static const uint8_t filesystem_test_signature[] =
            "AURORA-AHCI-FS-TEST-V1";

        zero_bytes(probe_readback, sizeof(probe_readback));

        if (!block_device_read(device, 0u, 1u, probe_readback)) {
            log_line("[ahci-rw] probe failed: LBA0 authorization read");
            return false;
        }

        if (!bytes_equal(
                probe_readback,
                filesystem_test_signature,
                sizeof(filesystem_test_signature) - 1u)) {
            log_line("[ahci-rw] probe failed: CI authorization signature absent");
            return false;
        }

        test_media = true;
    }

    if (!test_media) return false;

    for (uint32_t i = 0u; i < device->block_size; ++i) {
        probe_pattern[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
    }

    bool wrote = block_device_write(device, lba, 1u, probe_pattern);
    if (!wrote) {
        log_line("[ahci-rw] probe failed: WRITE DMA EXT");
        return false;
    }

    if (!block_device_flush(device)) {
        log_line("[ahci-rw] probe failed: FLUSH CACHE EXT");
        return false;
    }

    if (!block_device_read(device, lba, 1u, probe_readback)) {
        log_line("[ahci-rw] probe failed: write readback");
        return false;
    }

    bool ok =
        bytes_equal(probe_pattern, probe_readback, device->block_size);

    if (!ok) {
        log_line("[ahci-rw] probe failed: write readback mismatch");
    }

    bool restored =
        block_device_write(device, lba, 1u, probe_original) &&
        block_device_flush(device);
    if (!restored) {
        log_line("[ahci-rw] probe failed: restore write/flush");
        return false;
    }

    zero_bytes(probe_readback, sizeof(probe_readback));
    if (!block_device_read(device, lba, 1u, probe_readback)) {
        log_line("[ahci-rw] probe failed: restore readback");
        return false;
    }

    if (!bytes_equal(probe_original, probe_readback, device->block_size)) {
        log_line("[ahci-rw] probe failed: restore mismatch");
        return false;
    }

    return ok;
}
