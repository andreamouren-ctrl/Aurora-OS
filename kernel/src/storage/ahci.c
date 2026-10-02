#include <stddef.h>
#include <stdint.h>

#include <aurora/ahci.h>
#include <aurora/pci.h>
#include <aurora/pmm.h>
#include <aurora/vmm.h>

#define PCI_CLASS_MASS_STORAGE 0x01u
#define PCI_SUBCLASS_SATA      0x06u
#define PCI_PROGIF_AHCI        0x01u

#define AHCI_MMIO_VIRTUAL      0xFFFFFFFFB0200000ull
#define AHCI_MMIO_PAGE_COUNT   3u
#define AHCI_PAGE_SIZE         4096ull

#define AHCI_REG_CAP           0x00u
#define AHCI_REG_GHC           0x04u
#define AHCI_REG_PI            0x0Cu
#define AHCI_REG_VS            0x10u
#define AHCI_GHC_AE            (1u << 31)

#define AHCI_PORT_BASE         0x100u
#define AHCI_PORT_STRIDE       0x80u
#define AHCI_PORT_CLB          0x00u
#define AHCI_PORT_CLBU         0x04u
#define AHCI_PORT_FB           0x08u
#define AHCI_PORT_FBU          0x0Cu
#define AHCI_PORT_IS           0x10u
#define AHCI_PORT_CMD          0x18u
#define AHCI_PORT_TFD          0x20u
#define AHCI_PORT_SIG          0x24u
#define AHCI_PORT_SSTS         0x28u
#define AHCI_PORT_SERR         0x30u
#define AHCI_PORT_CI           0x38u

#define AHCI_CMD_ST            (1u << 0)
#define AHCI_CMD_FRE           (1u << 4)
#define AHCI_CMD_FR            (1u << 14)
#define AHCI_CMD_CR            (1u << 15)
#define AHCI_PORT_IS_TFES      (1u << 30)
#define AHCI_TFD_ERR           (1u << 0)
#define AHCI_TFD_DRQ           (1u << 3)
#define AHCI_TFD_BSY           (1u << 7)

#define AHCI_SSTS_DET_MASK     0x0Fu
#define AHCI_SSTS_IPM_MASK     0x0F00u
#define AHCI_SSTS_IPM_SHIFT    8u
#define AHCI_DET_PRESENT       3u
#define AHCI_IPM_ACTIVE        1u
#define AHCI_SIG_ATA           0x00000101u

#define ATA_CMD_IDENTIFY       0xECu
#define FIS_TYPE_REG_H2D       0x27u
#define AHCI_WAIT_LIMIT        1000000u

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

static volatile uint8_t *ahci_mmio;
static uint32_t ahci_active_ports;

static uint32_t mmio_read32(uint32_t offset) {
    volatile uint32_t *reg =
        (volatile uint32_t *)(ahci_mmio + offset);
    return *reg;
}

static void mmio_write32(uint32_t offset, uint32_t value) {
    volatile uint32_t *reg =
        (volatile uint32_t *)(ahci_mmio + offset);
    *reg = value;
    (void)*reg;
}

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    for (size_t i = 0u; i < length; ++i) {
        bytes[i] = 0u;
    }
}

static bool map_abar(uint64_t abar_physical) {
    uint64_t physical_base = abar_physical & ~(AHCI_PAGE_SIZE - 1u);
    uint64_t page_offset = abar_physical & (AHCI_PAGE_SIZE - 1u);

    for (uint64_t page = 0u; page < AHCI_MMIO_PAGE_COUNT; ++page) {
        uint64_t virtual_address = AHCI_MMIO_VIRTUAL + page * AHCI_PAGE_SIZE;
        uint64_t physical_address = physical_base + page * AHCI_PAGE_SIZE;

        if (!vmm_map_page(
                virtual_address,
                physical_address,
                VMM_FLAG_WRITE | VMM_FLAG_NO_CACHE)) {
            uint64_t existing = 0u;
            if (!vmm_translate(virtual_address, &existing) ||
                (existing & ~(AHCI_PAGE_SIZE - 1u)) != physical_address) {
                return false;
            }
        }
    }

    ahci_mmio = (volatile uint8_t *)(uintptr_t)(AHCI_MMIO_VIRTUAL + page_offset);
    return true;
}

static uint32_t active_sata_ports(uint32_t implemented) {
    uint32_t active = 0u;

    for (uint32_t port = 0u; port < 32u; ++port) {
        uint32_t bit = 1u << port;
        if ((implemented & bit) == 0u) {
            continue;
        }

        uint32_t base = AHCI_PORT_BASE + port * AHCI_PORT_STRIDE;
        uint32_t ssts = mmio_read32(base + AHCI_PORT_SSTS);
        uint32_t det = ssts & AHCI_SSTS_DET_MASK;
        uint32_t ipm = (ssts & AHCI_SSTS_IPM_MASK) >> AHCI_SSTS_IPM_SHIFT;
        uint32_t signature = mmio_read32(base + AHCI_PORT_SIG);

        if (det == AHCI_DET_PRESENT && ipm == AHCI_IPM_ACTIVE &&
            signature == AHCI_SIG_ATA) {
            active |= bit;
        }
    }

    return active;
}

static bool wait_cmd_clear(uint32_t base, uint32_t mask) {
    for (uint32_t i = 0u; i < AHCI_WAIT_LIMIT; ++i) {
        if ((mmio_read32(base + AHCI_PORT_CMD) & mask) == 0u) {
            return true;
        }
    }
    return false;
}

static bool stop_port(uint32_t base) {
    uint32_t cmd = mmio_read32(base + AHCI_PORT_CMD);
    cmd &= ~AHCI_CMD_ST;
    mmio_write32(base + AHCI_PORT_CMD, cmd);
    if (!wait_cmd_clear(base, AHCI_CMD_CR)) {
        return false;
    }

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

static bool issue_identify(uint8_t port, uint16_t identify[256]) {
    uint64_t cl_phys = pmm_alloc_page();
    uint64_t fis_phys = pmm_alloc_page();
    uint64_t table_phys = pmm_alloc_page();
    uint64_t data_phys = pmm_alloc_page();

    if (cl_phys == 0u || fis_phys == 0u || table_phys == 0u || data_phys == 0u) {
        if (cl_phys != 0u) pmm_free_page(cl_phys);
        if (fis_phys != 0u) pmm_free_page(fis_phys);
        if (table_phys != 0u) pmm_free_page(table_phys);
        if (data_phys != 0u) pmm_free_page(data_phys);
        return false;
    }

    void *cl_virt = pmm_phys_to_virt(cl_phys);
    void *fis_virt = pmm_phys_to_virt(fis_phys);
    void *table_virt = pmm_phys_to_virt(table_phys);
    uint16_t *data_virt = (uint16_t *)pmm_phys_to_virt(data_phys);
    zero_bytes(cl_virt, AURORA_PAGE_SIZE);
    zero_bytes(fis_virt, AURORA_PAGE_SIZE);
    zero_bytes(table_virt, AURORA_PAGE_SIZE);
    zero_bytes(data_virt, AURORA_PAGE_SIZE);

    uint32_t base = AHCI_PORT_BASE + (uint32_t)port * AHCI_PORT_STRIDE;
    if (!stop_port(base)) {
        goto fail;
    }

    mmio_write32(base + AHCI_PORT_CLB, (uint32_t)cl_phys);
    mmio_write32(base + AHCI_PORT_CLBU, (uint32_t)(cl_phys >> 32));
    mmio_write32(base + AHCI_PORT_FB, (uint32_t)fis_phys);
    mmio_write32(base + AHCI_PORT_FBU, (uint32_t)(fis_phys >> 32));
    mmio_write32(base + AHCI_PORT_SERR, 0xFFFFFFFFu);
    mmio_write32(base + AHCI_PORT_IS, 0xFFFFFFFFu);

    struct ahci_command_header *header = (struct ahci_command_header *)cl_virt;
    header[0].flags = 5u; /* 20-byte Register H2D FIS. */
    header[0].prdt_length = 1u;
    header[0].ctba = (uint32_t)table_phys;
    header[0].ctbau = (uint32_t)(table_phys >> 32);

    struct ahci_command_table_one_prdt *table =
        (struct ahci_command_table_one_prdt *)table_virt;
    uint8_t *cfis = table->cfis;
    cfis[0] = FIS_TYPE_REG_H2D;
    cfis[1] = 0x80u; /* Command bit. */
    cfis[2] = ATA_CMD_IDENTIFY;

    table->prdt[0].dba = (uint32_t)data_phys;
    table->prdt[0].dbau = (uint32_t)(data_phys >> 32);
    table->prdt[0].dbc_i = 511u; /* 512 bytes, zero-based byte count. */

    start_port(base);

    for (uint32_t i = 0u; i < AHCI_WAIT_LIMIT; ++i) {
        uint32_t tfd = mmio_read32(base + AHCI_PORT_TFD);
        if ((tfd & (AHCI_TFD_BSY | AHCI_TFD_DRQ)) == 0u) {
            break;
        }
        if (i + 1u == AHCI_WAIT_LIMIT) {
            goto fail_running;
        }
    }

    mmio_write32(base + AHCI_PORT_CI, 1u);

    for (uint32_t i = 0u; i < AHCI_WAIT_LIMIT; ++i) {
        uint32_t is = mmio_read32(base + AHCI_PORT_IS);
        uint32_t tfd = mmio_read32(base + AHCI_PORT_TFD);
        if ((is & AHCI_PORT_IS_TFES) != 0u || (tfd & AHCI_TFD_ERR) != 0u) {
            goto fail_running;
        }
        if ((mmio_read32(base + AHCI_PORT_CI) & 1u) == 0u) {
            for (size_t word = 0u; word < 256u; ++word) {
                identify[word] = data_virt[word];
            }
            stop_port(base);
            pmm_free_page(cl_phys);
            pmm_free_page(fis_phys);
            pmm_free_page(table_phys);
            pmm_free_page(data_phys);
            return true;
        }
    }

fail_running:
    (void)stop_port(base);
fail:
    pmm_free_page(cl_phys);
    pmm_free_page(fis_phys);
    pmm_free_page(table_phys);
    pmm_free_page(data_phys);
    return false;
}

static uint64_t identify_sector_count(const uint16_t words[256]) {
    if ((words[83] & (1u << 10)) != 0u) {
        return (uint64_t)words[100]
            | ((uint64_t)words[101] << 16)
            | ((uint64_t)words[102] << 32)
            | ((uint64_t)words[103] << 48);
    }
    return (uint64_t)words[60] | ((uint64_t)words[61] << 16);
}

static uint32_t identify_logical_sector_size(const uint16_t words[256]) {
    uint16_t word106 = words[106];
    if ((word106 & (1u << 14)) != 0u && (word106 & (1u << 15)) == 0u &&
        (word106 & (1u << 12)) != 0u) {
        uint32_t words_per_sector = (uint32_t)words[117]
            | ((uint32_t)words[118] << 16);
        if (words_per_sector >= 256u) {
            return words_per_sector * 2u;
        }
    }
    return 512u;
}

static void identify_model(const uint16_t words[256], char out[41]) {
    size_t pos = 0u;
    for (size_t word = 27u; word <= 46u; ++word) {
        out[pos++] = (char)(words[word] >> 8);
        out[pos++] = (char)(words[word] & 0xFFu);
    }
    out[40] = '\0';
    while (pos > 0u && out[pos - 1u] == ' ') {
        out[--pos] = '\0';
    }
}

bool ahci_probe(struct aurora_ahci_probe_result *out_result) {
    struct aurora_ahci_probe_result result = { 0 };
    struct aurora_pci_device device;

    if (!pci_find_class(
            PCI_CLASS_MASS_STORAGE,
            PCI_SUBCLASS_SATA,
            PCI_PROGIF_AHCI,
            &device)) {
        if (out_result != NULL) {
            *out_result = result;
        }
        return false;
    }

    uint32_t bar5 = pci_read_bar32(&device, 5u);
    if ((bar5 & 0x1u) != 0u || (bar5 & 0x6u) != 0u) {
        if (out_result != NULL) {
            *out_result = result;
        }
        return false;
    }

    uint64_t abar = (uint64_t)(bar5 & ~0xFu);
    if (abar == 0u || !pci_enable_memory_bus_master(&device) ||
        !map_abar(abar)) {
        if (out_result != NULL) {
            *out_result = result;
        }
        return false;
    }

    uint32_t ghc = mmio_read32(AHCI_REG_GHC);
    if ((ghc & AHCI_GHC_AE) == 0u) {
        mmio_write32(AHCI_REG_GHC, ghc | AHCI_GHC_AE);
        ghc = mmio_read32(AHCI_REG_GHC);
    }

    result.found = true;
    result.mmio_ready = (ghc & AHCI_GHC_AE) != 0u;
    result.bus = device.bus;
    result.slot = device.slot;
    result.function = device.function;
    result.vendor_id = device.vendor_id;
    result.device_id = device.device_id;
    result.abar_physical = abar;
    result.capabilities = mmio_read32(AHCI_REG_CAP);
    result.version = mmio_read32(AHCI_REG_VS);
    result.ports_implemented = mmio_read32(AHCI_REG_PI);
    result.sata_ports_active = active_sata_ports(result.ports_implemented);
    ahci_active_ports = result.sata_ports_active;

    if (out_result != NULL) {
        *out_result = result;
    }

    return result.mmio_ready;
}

bool ahci_identify_first(struct aurora_ahci_identify_result *out_result) {
    struct aurora_ahci_identify_result result = { 0 };
    if (ahci_mmio == NULL || ahci_active_ports == 0u) {
        if (out_result != NULL) {
            *out_result = result;
        }
        return false;
    }

    uint8_t port = 0u;
    while (port < 32u && (ahci_active_ports & (1u << port)) == 0u) {
        ++port;
    }
    if (port >= 32u) {
        if (out_result != NULL) {
            *out_result = result;
        }
        return false;
    }

    uint16_t words[256];
    zero_bytes(words, sizeof(words));
    if (!issue_identify(port, words)) {
        if (out_result != NULL) {
            *out_result = result;
        }
        return false;
    }

    result.identified = true;
    result.port = port;
    result.logical_sector_size = identify_logical_sector_size(words);
    result.sector_count = identify_sector_count(words);
    identify_model(words, result.model);

    if (out_result != NULL) {
        *out_result = result;
    }
    return result.sector_count != 0u;
}
