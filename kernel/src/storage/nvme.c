#include <stddef.h>
#include <stdint.h>

#include <aurora/log.h>
#include <aurora/nvme.h>
#include <aurora/pci.h>
#include <aurora/vmm.h>

#define PCI_CLASS_MASS_STORAGE 0x01u
#define PCI_SUBCLASS_NVM       0x08u
#define PCI_PROGIF_NVME        0x02u

#define NVME_MMIO_VIRTUAL      0xFFFFFFFFB0300000ull
#define NVME_MMIO_PAGE_COUNT   4u
#define NVME_PAGE_SIZE         4096ull

#define NVME_REG_CAP           0x0000u
#define NVME_REG_VS            0x0008u
#define NVME_REG_CSTS          0x001Cu

static volatile uint8_t *nvme_mmio;

static uint32_t mmio_read32(uint32_t offset) {
    volatile uint32_t *reg =
        (volatile uint32_t *)(nvme_mmio + offset);
    return *reg;
}

static uint64_t mmio_read64(uint32_t offset) {
    uint32_t low = mmio_read32(offset);
    uint32_t high = mmio_read32(offset + 4u);
    return (uint64_t)low | ((uint64_t)high << 32);
}

static bool map_bar0(uint64_t bar0_physical) {
    uint64_t physical_base = bar0_physical & ~(NVME_PAGE_SIZE - 1u);
    uint64_t page_offset = bar0_physical & (NVME_PAGE_SIZE - 1u);

    for (uint64_t page = 0u; page < NVME_MMIO_PAGE_COUNT; ++page) {
        uint64_t virtual_address = NVME_MMIO_VIRTUAL + page * NVME_PAGE_SIZE;
        uint64_t physical_address = physical_base + page * NVME_PAGE_SIZE;

        if (!vmm_map_page(
                virtual_address,
                physical_address,
                VMM_FLAG_WRITE | VMM_FLAG_NO_CACHE)) {
            uint64_t existing = 0u;
            if (!vmm_translate(virtual_address, &existing) ||
                (existing & ~(NVME_PAGE_SIZE - 1u)) != physical_address) {
                return false;
            }
        }
    }

    nvme_mmio = (volatile uint8_t *)(uintptr_t)(NVME_MMIO_VIRTUAL + page_offset);
    return true;
}

bool nvme_probe(struct aurora_nvme_probe_result *out_result) {
    struct aurora_pci_device device;

    if (!pci_find_class(
            PCI_CLASS_MASS_STORAGE,
            PCI_SUBCLASS_NVM,
            PCI_PROGIF_NVME,
            &device)) {
        if (out_result != NULL) {
            *out_result = (struct aurora_nvme_probe_result){ 0 };
        }
        return false;
    }

    uint64_t bar0 = 0u;
    if (!pci_read_bar64(&device, 0u, &bar0) ||
        !pci_enable_memory_bus_master(&device) ||
        !map_bar0(bar0)) {
        return false;
    }

    uint64_t cap = mmio_read64(NVME_REG_CAP);
    uint32_t version = mmio_read32(NVME_REG_VS);
    uint32_t csts = mmio_read32(NVME_REG_CSTS);

    uint16_t mqes = (uint16_t)((cap & 0xFFFFu) + 1u);
    uint8_t dstrd = (uint8_t)((cap >> 32) & 0x0Fu);
    uint8_t mpsmin = (uint8_t)(12u + ((cap >> 48) & 0x0Fu));
    uint8_t mpsmax = (uint8_t)(12u + ((cap >> 52) & 0x0Fu));

    if (mqes == 0u || mpsmin > mpsmax) {
        return false;
    }

    if (out_result != NULL) {
        out_result->found = true;
        out_result->bus = device.bus;
        out_result->slot = device.slot;
        out_result->function = device.function;
        out_result->vendor_id = device.vendor_id;
        out_result->device_id = device.device_id;
        out_result->bar0_physical = bar0;
        out_result->capabilities = cap;
        out_result->version = version;
        out_result->controller_status = csts;
        out_result->max_queue_entries = mqes;
        out_result->doorbell_stride = dstrd;
        out_result->minimum_page_shift = mpsmin;
        out_result->maximum_page_shift = mpsmax;
    }

    return true;
}

void nvme_bootstrap_probe(void) {
    struct aurora_nvme_probe_result result;

    if (!nvme_probe(&result)) {
        log_line("[storage] NVMe controller unavailable");
        return;
    }

    log_write("[storage] NVMe controller PCI ");
    log_u64(result.bus);
    log_putc(':');
    log_u64(result.slot);
    log_putc('.');
    log_u64(result.function);
    log_write(" vendor/device ");
    log_hex64(((uint64_t)result.vendor_id << 16) | result.device_id);
    log_line("");

    log_write("[nvme] BAR0 physical: ");
    log_hex64(result.bar0_physical);
    log_line("");

    log_write("[nvme] version: ");
    log_hex64(result.version);
    log_write(" max-queue-entries: ");
    log_u64(result.max_queue_entries);
    log_write(" doorbell-stride: ");
    log_u64(result.doorbell_stride);
    log_line("");

    log_write("[nvme] page-shift range: ");
    log_u64(result.minimum_page_shift);
    log_putc('-');
    log_u64(result.maximum_page_shift);
    log_write(" CSTS: ");
    log_hex64(result.controller_status);
    log_line("");

    log_line("[nvme] PCI/BAR/MMIO capability probe passed");
}
