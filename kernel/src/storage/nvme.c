#include <stddef.h>
#include <stdint.h>

#include <aurora/block_device.h>
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
#define NVME_IO_QUEUE_DEPTH    16u
#define NVME_IO_QUEUE_ID       1u

#define NVME_ADMIN_OPCODE_CREATE_IO_SQ 0x01u
#define NVME_ADMIN_OPCODE_CREATE_IO_CQ 0x05u
#define NVME_ADMIN_OPCODE_IDENTIFY     0x06u
#define NVME_NVM_OPCODE_FLUSH          0x00u
#define NVME_NVM_OPCODE_WRITE          0x01u
#define NVME_NVM_OPCODE_READ           0x02u

#define NVME_IDENTIFY_CNS_NAMESPACE  0x00u
#define NVME_IDENTIFY_CNS_CONTROLLER 0x01u

static const char nvme_rw_test_signature[] = "AURORA-NVME-RW-TEST-V1";

struct nvme_queue_state {
    uint64_t sq_phys;
    uint64_t cq_phys;
    uint32_t *sq;
    uint32_t *cq;
    uint16_t depth;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint16_t next_cid;
    uint8_t cq_phase;
    uint16_t queue_id;
    bool initialized;
};

static volatile uint8_t *nvme_mmio;
static uint64_t nvme_capabilities;
static uint32_t nvme_doorbell_stride_bytes;
static struct nvme_queue_state admin_queue;
static struct nvme_queue_state io_queue;
static struct aurora_nvme_admin_result namespace_identity;
static struct aurora_block_device namespace_device;
static bool namespace_device_ready;
static uint8_t rw_original[NVME_PAGE_SIZE];
static uint8_t rw_pattern[NVME_PAGE_SIZE];
static uint8_t rw_readback[NVME_PAGE_SIZE];

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

static bool bytes_equal(const uint8_t *left, const uint8_t *right, uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}

static bool has_rw_test_signature(const uint8_t *block, uint32_t block_size) {
    uint32_t signature_length = (uint32_t)(sizeof(nvme_rw_test_signature) - 1u);
    if (block_size < signature_length) {
        return false;
    }

    for (uint32_t i = 0u; i < signature_length; ++i) {
        if (block[i] != (uint8_t)nvme_rw_test_signature[i]) {
            return false;
        }
    }
    return true;
}

static uint32_t doorbell_offset(uint16_t queue_id, bool completion) {
    uint32_t index = (uint32_t)queue_id * 2u + (completion ? 1u : 0u);
    return NVME_REG_DOORBELL_BASE + index * nvme_doorbell_stride_bytes;
}

static bool queue_wait_completion(struct nvme_queue_state *queue, uint16_t cid) {
    uint64_t deadline = clock_now_ns() + controller_timeout_ns();

    for (;;) {
        uint32_t *cqe = queue->cq + ((uint32_t)queue->cq_head * 4u);
        uint16_t status = (uint16_t)(cqe[3] >> 16);

        if ((status & 1u) == queue->cq_phase) {
            uint16_t completed_cid = (uint16_t)(cqe[3] & 0xFFFFu);
            bool success = completed_cid == cid && (status & 0xFFFEu) == 0u;

            queue->cq_head++;
            if (queue->cq_head == queue->depth) {
                queue->cq_head = 0u;
                queue->cq_phase ^= 1u;
            }
            mmio_write32(
                doorbell_offset(queue->queue_id, true),
                queue->cq_head);
            return success;
        }

        if (clock_now_ns() >= deadline) {
            return false;
        }
    }
}

static bool queue_submit(struct nvme_queue_state *queue, const uint32_t command[16]) {
    if (queue == NULL || !queue->initialized || command == NULL) {
        return false;
    }

    uint16_t cid = queue->next_cid++;
    if (queue->next_cid == 0u) {
        queue->next_cid = 1u;
    }

    uint16_t tail = queue->sq_tail;
    uint32_t *sqe = queue->sq + ((uint32_t)tail * 16u);

    for (uint32_t i = 0u; i < 16u; ++i) {
        sqe[i] = command[i];
    }
    sqe[0] = (sqe[0] & 0x0000FFFFu) | ((uint32_t)cid << 16);

    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    queue->sq_tail = (uint16_t)((tail + 1u) % queue->depth);
    mmio_write32(
        doorbell_offset(queue->queue_id, false),
        queue->sq_tail);

    return queue_wait_completion(queue, cid);
}

static bool admin_queue_init(void) {
    if (admin_queue.initialized) {
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
    new_cc |= (6u << 16);
    new_cc |= (4u << 20);
    mmio_write32(NVME_REG_CC, new_cc);

    if (!wait_ready(true)) {
        mmio_write32(NVME_REG_CC, 0u);
        (void)wait_ready(false);
        pmm_free_page(sq_phys);
        pmm_free_page(cq_phys);
        return false;
    }

    nvme_doorbell_stride_bytes =
        4u << ((uint32_t)((nvme_capabilities >> 32) & 0x0Fu));

    admin_queue.sq_phys = sq_phys;
    admin_queue.cq_phys = cq_phys;
    admin_queue.sq = sq;
    admin_queue.cq = cq;
    admin_queue.depth = NVME_ADMIN_QUEUE_DEPTH;
    admin_queue.sq_tail = 0u;
    admin_queue.cq_head = 0u;
    admin_queue.next_cid = 1u;
    admin_queue.cq_phase = 1u;
    admin_queue.queue_id = 0u;
    admin_queue.initialized = true;
    return true;
}

static bool admin_identify(uint32_t namespace_id, uint8_t cns, uint64_t buffer_phys) {
    uint32_t command[16] = { 0 };
    command[0] = NVME_ADMIN_OPCODE_IDENTIFY;
    command[1] = namespace_id;
    command[6] = (uint32_t)buffer_phys;
    command[7] = (uint32_t)(buffer_phys >> 32);
    command[10] = cns;
    return queue_submit(&admin_queue, command);
}

static bool io_queue_init(void) {
    if (io_queue.initialized) {
        return true;
    }

    if (!admin_queue.initialized) {
        return false;
    }

    uint16_t max_entries = (uint16_t)((nvme_capabilities & 0xFFFFu) + 1u);
    if (max_entries < NVME_IO_QUEUE_DEPTH) {
        return false;
    }

    uint64_t cq_phys = pmm_alloc_page();
    uint64_t sq_phys = pmm_alloc_page();
    if (cq_phys == 0u || sq_phys == 0u) {
        if (cq_phys != 0u) {
            pmm_free_page(cq_phys);
        }
        if (sq_phys != 0u) {
            pmm_free_page(sq_phys);
        }
        return false;
    }

    uint32_t *cq = pmm_phys_to_virt(cq_phys);
    uint32_t *sq = pmm_phys_to_virt(sq_phys);
    zero_page(cq);
    zero_page(sq);

    uint32_t create_cq[16] = { 0 };
    create_cq[0] = NVME_ADMIN_OPCODE_CREATE_IO_CQ;
    create_cq[6] = (uint32_t)cq_phys;
    create_cq[7] = (uint32_t)(cq_phys >> 32);
    create_cq[10] =
        (uint32_t)NVME_IO_QUEUE_ID |
        ((uint32_t)(NVME_IO_QUEUE_DEPTH - 1u) << 16);
    create_cq[11] = 1u;

    if (!queue_submit(&admin_queue, create_cq)) {
        pmm_free_page(cq_phys);
        pmm_free_page(sq_phys);
        return false;
    }

    uint32_t create_sq[16] = { 0 };
    create_sq[0] = NVME_ADMIN_OPCODE_CREATE_IO_SQ;
    create_sq[6] = (uint32_t)sq_phys;
    create_sq[7] = (uint32_t)(sq_phys >> 32);
    create_sq[10] =
        (uint32_t)NVME_IO_QUEUE_ID |
        ((uint32_t)(NVME_IO_QUEUE_DEPTH - 1u) << 16);
    create_sq[11] = 1u | ((uint32_t)NVME_IO_QUEUE_ID << 16);

    if (!queue_submit(&admin_queue, create_sq)) {
        pmm_free_page(cq_phys);
        pmm_free_page(sq_phys);
        return false;
    }

    io_queue.sq_phys = sq_phys;
    io_queue.cq_phys = cq_phys;
    io_queue.sq = sq;
    io_queue.cq = cq;
    io_queue.depth = NVME_IO_QUEUE_DEPTH;
    io_queue.sq_tail = 0u;
    io_queue.cq_head = 0u;
    io_queue.next_cid = 1u;
    io_queue.cq_phase = 1u;
    io_queue.queue_id = NVME_IO_QUEUE_ID;
    io_queue.initialized = true;
    return true;
}

static bool nvme_read_one(uint64_t lba, void *buffer) {
    if (!io_queue.initialized || !namespace_device_ready || buffer == NULL ||
        namespace_identity.block_size == 0u ||
        namespace_identity.block_size > NVME_PAGE_SIZE ||
        lba >= namespace_identity.block_count) {
        return false;
    }

    uint64_t data_phys = pmm_alloc_page();
    if (data_phys == 0u) {
        return false;
    }

    uint8_t *data = pmm_phys_to_virt(data_phys);
    zero_page(data);

    uint32_t command[16] = { 0 };
    command[0] = NVME_NVM_OPCODE_READ;
    command[1] = namespace_identity.namespace_id;
    command[6] = (uint32_t)data_phys;
    command[7] = (uint32_t)(data_phys >> 32);
    command[10] = (uint32_t)lba;
    command[11] = (uint32_t)(lba >> 32);
    command[12] = 0u;

    bool success = queue_submit(&io_queue, command);
    if (success) {
        uint8_t *out = buffer;
        for (uint32_t i = 0u; i < namespace_identity.block_size; ++i) {
            out[i] = data[i];
        }
    }

    pmm_free_page(data_phys);
    return success;
}

static bool nvme_write_one(uint64_t lba, const void *buffer) {
    if (!io_queue.initialized || !namespace_device_ready || buffer == NULL ||
        namespace_identity.block_size == 0u ||
        namespace_identity.block_size > NVME_PAGE_SIZE ||
        lba >= namespace_identity.block_count) {
        return false;
    }

    uint64_t data_phys = pmm_alloc_page();
    if (data_phys == 0u) {
        return false;
    }

    uint8_t *data = pmm_phys_to_virt(data_phys);
    zero_page(data);
    const uint8_t *source = buffer;
    for (uint32_t i = 0u; i < namespace_identity.block_size; ++i) {
        data[i] = source[i];
    }

    uint32_t command[16] = { 0 };
    command[0] = NVME_NVM_OPCODE_WRITE;
    command[1] = namespace_identity.namespace_id;
    command[6] = (uint32_t)data_phys;
    command[7] = (uint32_t)(data_phys >> 32);
    command[10] = (uint32_t)lba;
    command[11] = (uint32_t)(lba >> 32);
    command[12] = 0u;

    bool success = queue_submit(&io_queue, command);
    pmm_free_page(data_phys);
    return success;
}

static bool nvme_block_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (device != &namespace_device || buffer == NULL || block_count == 0u ||
        lba >= namespace_identity.block_count ||
        (uint64_t)block_count > namespace_identity.block_count - lba) {
        return false;
    }

    uint8_t *bytes = buffer;
    for (uint32_t i = 0u; i < block_count; ++i) {
        if (!nvme_read_one(
                lba + i,
                bytes + (uint64_t)i * namespace_identity.block_size)) {
            return false;
        }
    }

    return true;
}

static bool nvme_block_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    if (device != &namespace_device || buffer == NULL || block_count == 0u ||
        lba >= namespace_identity.block_count ||
        (uint64_t)block_count > namespace_identity.block_count - lba) {
        return false;
    }

    const uint8_t *bytes = buffer;
    for (uint32_t i = 0u; i < block_count; ++i) {
        if (!nvme_write_one(
                lba + i,
                bytes + (uint64_t)i * namespace_identity.block_size)) {
            return false;
        }
    }

    return true;
}

static bool nvme_block_flush(struct aurora_block_device *device) {
    if (device != &namespace_device || !io_queue.initialized ||
        !namespace_device_ready) {
        return false;
    }

    uint32_t command[16] = { 0 };
    command[0] = NVME_NVM_OPCODE_FLUSH;
    command[1] = namespace_identity.namespace_id;
    return queue_submit(&io_queue, command);
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

    if (!admin_identify(0u, NVME_IDENTIFY_CNS_CONTROLLER, identify_phys)) {
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
    if (!admin_identify(1u, NVME_IDENTIFY_CNS_NAMESPACE, identify_phys)) {
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

bool nvme_namespace_block_device_init(const struct aurora_nvme_admin_result *identity) {
    if (identity == NULL || identity->namespace_id == 0u ||
        identity->block_count == 0u || identity->block_size == 0u ||
        identity->block_size > NVME_PAGE_SIZE || !io_queue_init()) {
        return false;
    }

    namespace_identity = *identity;
    namespace_device.name = "nvme-ns1";
    namespace_device.block_size = identity->block_size;
    namespace_device.block_count = identity->block_count;
    namespace_device.read_only = false;
    namespace_device.context = NULL;
    namespace_device.read_blocks = nvme_block_read;
    namespace_device.write_blocks = nvme_block_write;
    namespace_device.flush = nvme_block_flush;
    namespace_device_ready = true;
    return true;
}

struct aurora_block_device *nvme_namespace_block_device(void) {
    return namespace_device_ready ? &namespace_device : NULL;
}

static bool signed_rw_probe(struct aurora_block_device *device) {
    if (device == NULL || device->block_count == 0u ||
        device->block_size == 0u || device->block_size > NVME_PAGE_SIZE) {
        return false;
    }

    uint64_t test_lba = device->block_count - 1u;
    uint32_t block_size = device->block_size;

    if (!block_device_read(device, test_lba, 1u, rw_original)) {
        return false;
    }

    if (!has_rw_test_signature(rw_original, block_size)) {
        log_line("[nvme] signed write/flush probe skipped (CI signature absent)");
        return true;
    }

    nvme_rw_probe_media = true;

    for (uint32_t i = 0u; i < block_size; ++i) {
        rw_pattern[i] = (uint8_t)(0x5Au ^ (uint8_t)i);
        rw_readback[i] = 0u;
    }

    if (!block_device_write(device, test_lba, 1u, rw_pattern) ||
        !block_device_flush(device) ||
        !block_device_read(device, test_lba, 1u, rw_readback) ||
        !bytes_equal(rw_pattern, rw_readback, block_size)) {
        return false;
    }

    if (!block_device_write(device, test_lba, 1u, rw_original) ||
        !block_device_flush(device)) {
        return false;
    }

    for (uint32_t i = 0u; i < block_size; ++i) {
        rw_readback[i] = 0u;
    }

    if (!block_device_read(device, test_lba, 1u, rw_readback) ||
        !bytes_equal(rw_original, rw_readback, block_size)) {
        return false;
    }

    log_line("[nvme] NVM Write + Flush reversible probe verified");
    return true;
}

static bool nvme_bootstrap_complete;
static bool nvme_rw_probe_media;

void nvme_bootstrap_probe(void) {
    if (nvme_bootstrap_complete) {
        log_line("[nvme] bootstrap already complete; reusing active namespace");
        return;
    }

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

    if (!nvme_namespace_block_device_init(&admin)) {
        log_line("[nvme] I/O Queue initialization failed");
        return;
    }

    struct aurora_block_device *device = nvme_namespace_block_device();
    if (device == NULL || !block_device_register(device) ||
        block_device_find("nvme-ns1") != device) {
        log_line("[nvme] namespace block-device registration failed");
        return;
    }

    log_line("[nvme] I/O Submission/Completion Queue pair initialized");
    log_line("[storage] block device registered: nvme-ns1");

#if AURORA_BOOT_VALIDATION
    uint8_t first_block[NVME_PAGE_SIZE];
    if (!block_device_read(device, 0u, 1u, first_block)) {
        log_line("[nvme] NVM Read LBA0 through block layer failed");
        return;
    }

    log_line("[nvme] NVM Read LBA0 via block layer verified");

    if (!signed_rw_probe(device)) {
        log_line("[nvme] signed write/flush probe failed");
        return;
    }
#endif

    nvme_bootstrap_complete = true;
}
bool nvme_namespace_reserved_for_rw_probe(void) {
    return nvme_rw_probe_media;
}
