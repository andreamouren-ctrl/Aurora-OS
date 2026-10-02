#include <stddef.h>
#include <stdint.h>

#include <aurora/ahci.h>
#include <aurora/pci.h>
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
#define AHCI_PORT_SIG          0x24u
#define AHCI_PORT_SSTS         0x28u
#define AHCI_SSTS_DET_MASK     0x0Fu
#define AHCI_SSTS_IPM_MASK     0x0F00u
#define AHCI_SSTS_IPM_SHIFT    8u
#define AHCI_DET_PRESENT       3u
#define AHCI_IPM_ACTIVE        1u
#define AHCI_SIG_ATA           0x00000101u

static volatile uint8_t *ahci_mmio;

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

    if (out_result != NULL) {
        *out_result = result;
    }

    return result.mmio_ready;
}
