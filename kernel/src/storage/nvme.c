#include <stddef.h>
#include <stdint.h>

#include <aurora/clock.h>
#include <aurora/log.h>
#include <aurora/nvme.h>
#include <aurora/pci.h>
#include <aurora/pmm.h>
#include <aurora/vmm.h>

#define PCI_CLASS_MASS_STORAGE 0x01u
#define PCI_SUBCLASS_NVM       0x08u
#define PCI_PROGIF_NVME        0x02u

#define NVME_MMIO_VIRTUAL      0xFFFFFFFFB0300000ull
#define NVME_MMIO_PAGE_COUNT   4u
#define NVME_PAGE_SIZE         4096ull

#define NVME_REG_CAP           0x0000u
#define NVME_REG_VS            0x0008u
#define NVME_REG_CC            0x0014u
#define NVME_REG_CSTS          0x001Cu
#define NVME_REG_AQA           0x0024u
#define NVME_REG_ASQ           0x0028u
#define NVME_REG_ACQ           0x0030u
#define NVME_REG_DOORBELL_BASE 0x1000u

#define NVME_CC_EN             (1u << 0)
#define NVME_CSTS_RDY          (1u << 0)

#define NVME_ADMIN_QUEUE_DEPTH 16u
#define NVME_ADMIN_OPCODE_IDENTIFY 0x06u
#define NVME_IDENTIFY_CNS_NAMESPACE  0x00u
#define NVME_IDENTIFY_CNS_CONTROLLER 0x01u

struct nvme_admin_state {
    uint64_t sq_phys;
    uint64_t cq_phys;
    uint32_t *sq;
    uint32_t *cq;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint16_t next_cid;
    uint8_t cq_phase;
    uint32_t doorbell_stride_bytes;
    bool initialized;
};

static volatile uint8_t *nvme_mmio;
static uint64_t nvme_capabilities;
static struct nvme_admin_state admin_state;

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

static void mmio_write32(uint32_t offset, uint32_t value) {
    volatile uint32_t *reg =
        (volatile uint32_t *)(nvme_mmio + offset);
    *reg = value;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static void mmio_write64(uint32_t offset, uint64_t value) {
    mmio_write32(offset, (uint32_t)value);
    mmio_write32(offset + 4u, (uint32_t)(value >> 32));
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

static uint64_t controller_timeout_ns(void) {
    uint64_t timeout_units = (nvme_capabilities >> 24) & 0xFFu;
    return (timeout_units + 1u) * 500000000ull;
}

static bool wait_ready(bool ready) {
    uint64_t deadline = clock_now_ns() + controller_timeout_ns();
    uint32_t wanted = ready ? NVME_CSTS_RDY : 0u;

    while ((mmio_read32(NVME_REG_CSTS) & NVME_CSTS_RDY) != wanted) {
        if (clock_now_ns() >= deadline) {
            return false;
        }
    }

    return true;
}

static void zero_page(void *page) {
    uint8_t *bytes = page;
    for (uint32_t i = 0u; i < NVME_PAGE_SIZE; ++i) {
        bytes[i] = 0u;
    }
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p) {
    return (uint64_t)read_le32(p) |
           ((uint64_t)read_le32(p + 4u) << 32);
}

static void copy_ascii_trim(char *out, size_t out_size, const uint8_t *in, size_t in_size) {
    if (out == NULL || out_size == 0u) {
        return;
    }

    size_t length = in_size;
    while (length > 0u && (in[length - 1u] == ' ' || in[length - 1u] == 0u)) {
        --length;
    }

    if (length >= out_size) {
        length = out_size - 1u;
    }

    for (size_t i = 0u; i < length; ++i) {
        uint8_t value = in[i];
        out[i] = (value >= 32u && value <= 126u) ? (char)value : '?';
    }
    out[length] = '\0';
}

static uint32_t doorbell_offset(uint16_t queue_id, bool completion) {
    uint32_t index = (uint32_t)queue_id * 2u + (completion ? 1u : 0u);
    return NVME_REG_DOORBELL_BASE + index * admin_state.doorbell_stride_bytes;
}

static bool admin_queue_init(void) {
    if (admin_state.initialized) {
        return true;
    }

    uint16_t max_entries = (uint16_t)((nvme_capabilities & 0xFFFFu) + 1u);
    if (max_entries < NVME_ADMIN_QUEUE_DEPTH) {
        return false;
    }

    uint8_t min_page_shift = (uint8_t)(12u + ((nvme_capabilities >> 48) & 0x0Fu));
    uint8_t max_page_shift = (uint8_t)(12u + ((nvme_capabilities >> 52) & 0x0Fu));
    if (min_page_shift > 12u || max_page_shift < 12u) {
        return false;
    }

    uint64_t sq_phys = pmm_alloc_page();
    uint64_t cq_phys = pmm_alloc_page();
    if (sq_phys == 0u || cq_phys == 0u) {
        if (sq_phys != 0u) {
            pmm_free_page(sq_phys);
        }
        if (cq_phys != 0u) {
            pmm_free_page(cq_phys);
        }
        return false;
    }

    uint32_t *sq = pmm_phys_to_virt(sq_phys);
    uint32_t *cq = pmm_phys_to_virt(cq_phys);
    zero_page(sq);
    zero_page(cq);

    uint32_t cc = mmio_read32(NVME_REG_CC);
    if ((cc & NVME_CC_EN) != 0u) {
        mmio_write32(NVME_REG_CC, cc & ~NVME_CC_EN);
        if (!wait_ready(false)) {
            pmm_free_page(sq_phys);
            pmm_free_page(cq_phys);
            return false;
        }
    }

    mmio_write32(
        NVME_REG_AQA,
        ((NVME_ADMIN_QUEUE_DEPTH - 1u) << 16) |
        (NVME_ADMIN_QUEUE_DEPTH - 1u));
    mmio_write64(NVME_REG_ASQ, sq_phys);
    mmio_write64(NVME_REG_ACQ, cq_phys);

    uint32_t new_cc = 0u;
    new_cc |= NVME_CC_EN;
    new_cc |= (6u << 16); /* IOSQES: 64-byte submission entries. */
    new_cc |= (4u << 20); /* IOCQES: 16-byte completion entries. */
    mmio_write32(NVME_REG_CC, new_cc);

    if (!wait_ready(true)) {
        mmio_write32(NVME_REG_CC, 0u);
        (void)wait_ready(false);
        pmm_free_page(sq_phys);
        pmm_free_page(cq_phys);
        return false;
    }

    admin_state.sq_phys = sq_phys;
    admin_state.cq_phys = cq_phys;
    admin_state.sq = sq;
    admin_state.cq = cq;
    admin_state.sq_tail = 0u;
    admin_state.cq_head = 0u;
    admin_state.next_cid = 1u;
    admin_state.cq_phase = 1u;
    admin_state.doorbell_stride_bytes =
        4u << ((uint32_t)((nvme_capabilities >> 32) & 0x0Fu));
    admin_state.initialized = true;
    return true;
}

static bool admin_identify(uint32_t namespace_id, uint8_t cns, uint8_t *buffer) {
    if (!admin_state.initialized || buffer == NULL) {
        return false;
    }

    uint64_t buffer_phys = 0u;
    uintptr_t buffer_virt = (uintptr_t)buffer;
    uint64_t hhdm = pmm_hhdm_offset();
    if ((uint64_t)buffer_virt < hhdm) {
        return false;
    }
    buffer_phys = (uint64_t)buffer_virt - hhdm;

    uint16_t cid = admin_state.next_cid++;
    uint16_t tail = admin_state.sq_tail;
    uint32_t *sqe = admin_state.sq + ((uint32_t)tail * 16u);

    for (uint32_t i = 0u; i < 16u; ++i) {
        sqe[i] = 0u;
    }

    sqe[0] = NVME_ADMIN_OPCODE_IDENTIFY | ((uint32_t)cid << 16);
    sqe[1] = namespace_id;
    sqe[6] = (uint32_t)buffer_phys;
    sqe[7] = (uint32_t)(buffer_phys >> 32);
    sqe[10] = cns;

    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    admin_state.sq_tail =
        (uint16_t)((tail + 1u) % NVME_ADMIN_QUEUE_DEPTH);
    mmio_write32(doorbell_offset(0u, false), admin_state.sq_tail);

    uint64_t deadline = clock_now_ns() + controller_timeout_ns();
    for (;;) {
        uint32_t *cqe = admin_state.cq + ((uint32_t)admin_state.cq_head * 4u);
        uint16_t status = (uint16_t)(cqe[3] >> 16);

        if ((status & 1u) == admin_state.cq_phase) {
            uint16_t completed_cid = (uint16_t)(cqe[3] & 0xFFFFu);
            bool success = completed_cid == cid && (status & 0xFFFEu) == 0u;

            admin_state.cq_head++;
            if (admin_state.cq_head == NVME_ADMIN_QUEUE_DEPTH) {
                admin_state.cq_head = 0u;
                admin_state.cq_phase ^= 1u;
            }
            mmio_write32(doorbell_offset(0u, true), admin_state.cq_head);
            return success;
        }

        if (clock_now_ns() >= deadline) {
            return false;
        }
    }
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

    nvme_capabilities = cap;

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

bool nvme_admin_identify(struct aurora_nvme_admin_result *out_result) {
    if (out_result == NULL || nvme_mmio == NULL || nvme_capabilities == 0u) {
        return false;
    }

    if (!admin_queue_init()) {
        return false;
    }

    uint64_t identify_phys = pmm_alloc_page();
    if (identify_phys == 0u) {
        return false;
    }

    uint8_t *identify = pmm_phys_to_virt(identify_phys);
    zero_page(identify);

    if (!admin_identify(0u, NVME_IDENTIFY_CNS_CONTROLLER, identify)) {
        pmm_free_page(identify_phys);
        return false;
    }

    struct aurora_nvme_admin_result result = { 0 };
    copy_ascii_trim(result.serial, sizeof(result.serial), identify + 4u, 20u);
    copy_ascii_trim(result.model, sizeof(result.model), identify + 24u, 40u);
    result.namespace_count = read_le32(identify + 516u);

    if (result.namespace_count == 0u) {
        pmm_free_page(identify_phys);
        return false;
    }

    zero_page(identify);
    if (!admin_identify(1u, NVME_IDENTIFY_CNS_NAMESPACE, identify)) {
        pmm_free_page(identify_phys);
        return false;
    }

    uint64_t nsze = read_le64(identify + 0u);
    uint8_t flbas = identify[26u] & 0x0Fu;
    uint32_t lbaf_offset = 128u + (uint32_t)flbas * 4u;
    uint8_t lbads = identify[lbaf_offset + 2u];

    if (nsze == 0u || lbads < 9u || lbads > 31u) {
        pmm_free_page(identify_phys);
        return false;
    }

    result.namespace_id = 1u;
    result.block_count = nsze;
    result.block_size = 1u << lbads;

    pmm_free_page(identify_phys);
    *out_result = result;
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

    struct aurora_nvme_admin_result admin;
    if (!nvme_admin_identify(&admin)) {
        log_line("[nvme] Admin Queue / Identify Controller+Namespace failed");
        return;
    }

    log_line("[nvme] Admin Queue initialized and controller ready");
    log_write("[nvme] model: ");
    log_line(admin.model);
    log_write("[nvme] serial: ");
    log_line(admin.serial);
    log_write("[nvme] namespaces: ");
    log_u64(admin.namespace_count);
    log_line("");
    log_write("[nvme] namespace 1 blocks: ");
    log_u64(admin.block_count);
    log_write(" logical-block: ");
    log_u64(admin.block_size);
    log_line("");
    log_line("[nvme] Identify Controller + Namespace 1 passed");
}
