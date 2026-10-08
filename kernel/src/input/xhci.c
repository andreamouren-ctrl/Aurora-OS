#include <stddef.h>
#include <stdint.h>

#include <aurora/log.h>
#include <aurora/pci.h>
#include <aurora/pmm.h>
#include <aurora/vmm.h>
#include <aurora/xhci.h>

#define PCI_CLASS_SERIAL_BUS        0x0Cu
#define PCI_SUBCLASS_USB            0x03u
#define PCI_PROGIF_XHCI             0x30u

#define PCI_CAP_ID_MSI              0x05u
#define PCI_CAP_ID_MSIX             0x11u

#define XHCI_MMIO_VIRTUAL           0xFFFFFFFFB0400000ull
#define XHCI_OPERATIONAL_VIRTUAL    0xFFFFFFFFB0401000ull
#define XHCI_RUNTIME_VIRTUAL        0xFFFFFFFFB0402000ull
#define XHCI_DOORBELL_VIRTUAL       0xFFFFFFFFB0403000ull
#define XHCI_PAGE_SIZE              4096ull
#define XHCI_RESET_SPIN_LIMIT       10000000u

#define XHCI_CAP_CAPLENGTH          0x00u
#define XHCI_CAP_HCSPARAMS1         0x04u
#define XHCI_CAP_HCSPARAMS2         0x08u
#define XHCI_CAP_HCCPARAMS1         0x10u
#define XHCI_CAP_DBOFF              0x14u
#define XHCI_CAP_RTSOFF             0x18u

#define XHCI_OP_USBCMD              0x00u
#define XHCI_OP_USBSTS              0x04u
#define XHCI_OP_PAGESIZE            0x08u
#define XHCI_OP_CRCR                0x18u
#define XHCI_OP_DCBAAP              0x30u
#define XHCI_OP_CONFIG              0x38u
#define XHCI_OP_PORT_BASE           0x400u
#define XHCI_OP_PORT_STRIDE         0x10u
#define XHCI_PORTSC                 0x00u

#define XHCI_USBCMD_RUN_STOP        (1u << 0)
#define XHCI_USBCMD_HCRST           (1u << 1)
#define XHCI_USBSTS_HCHALTED        (1u << 0)
#define XHCI_USBSTS_CNR             (1u << 11)

#define XHCI_PORTSC_CCS             (1u << 0)
#define XHCI_PORTSC_PED             (1u << 1)
#define XHCI_PORTSC_PR              (1u << 4)
#define XHCI_PORTSC_PP              (1u << 9)
#define XHCI_PORTSC_SPEED_SHIFT     10u
#define XHCI_PORTSC_SPEED_MASK      (0xFu << XHCI_PORTSC_SPEED_SHIFT)
#define XHCI_PORTSC_RW1C_MASK       ((1u << 17) | (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21) | (1u << 22) | (1u << 23))
#define XHCI_PORT_RESET_SPIN_LIMIT  10000000u

#define XHCI_RUNTIME_INTERRUPTER0    0x20u
#define XHCI_INTR_IMAN               0x00u
#define XHCI_INTR_ERSTSZ             0x08u
#define XHCI_INTR_ERSTBA             0x10u
#define XHCI_INTR_ERDP               0x18u

#define XHCI_TRB_TYPE_LINK           6u
#define XHCI_TRB_TYPE_ENABLE_SLOT    9u
#define XHCI_TRB_TYPE_COMMAND_COMPLETION 33u
#define XHCI_COMPLETION_SUCCESS      1u
#define XHCI_EVENT_SPIN_LIMIT        10000000u
#define XHCI_TRB_CYCLE               (1u << 0)
#define XHCI_TRB_TOGGLE_CYCLE        (1u << 1)
#define XHCI_TRB_TYPE_SHIFT          10u
#define XHCI_RING_TRB_COUNT          256u
#define XHCI_BOOTSTRAP_SCRATCHPAD_MAX 64u

static volatile uint8_t *xhci_capability_base;
static volatile uint8_t *xhci_operational_base;
static volatile uint8_t *xhci_runtime_base;
static volatile uint8_t *xhci_doorbell_base;

static bool xhci_map_capability_page(uint64_t physical) {
    uint64_t physical_page = physical & ~(XHCI_PAGE_SIZE - 1u);
    uint64_t page_offset = physical & (XHCI_PAGE_SIZE - 1u);

    if (!vmm_map_page(
            XHCI_MMIO_VIRTUAL,
            physical_page,
            VMM_FLAG_WRITE | VMM_FLAG_NO_CACHE)) {
        uint64_t existing = 0u;

        if (!vmm_translate(XHCI_MMIO_VIRTUAL, &existing) ||
            (existing & ~(XHCI_PAGE_SIZE - 1u)) != physical_page) {
            return false;
        }
    }

    xhci_capability_base =
        (volatile uint8_t *)(uintptr_t)(
            XHCI_MMIO_VIRTUAL + page_offset
        );

    return true;
}

static uint32_t xhci_read32(uint32_t offset) {
    return *(volatile uint32_t *)(xhci_capability_base + offset);
}
static bool xhci_map_register_page(
    uint64_t virtual_page,
    uint64_t physical,
    volatile uint8_t **out_base
) {
    if (out_base == NULL || physical == 0u) return false;

    uint64_t physical_page = physical & ~(XHCI_PAGE_SIZE - 1u);
    uint64_t page_offset = physical & (XHCI_PAGE_SIZE - 1u);

    if (!vmm_map_page(
            virtual_page,
            physical_page,
            VMM_FLAG_WRITE | VMM_FLAG_NO_CACHE)) {
        uint64_t existing = 0u;

        if (!vmm_translate(virtual_page, &existing) ||
            (existing & ~(XHCI_PAGE_SIZE - 1u)) != physical_page) {
            return false;
        }
    }

    *out_base = (volatile uint8_t *)(uintptr_t)(
        virtual_page + page_offset
    );
    return true;
}

static uint32_t xhci_mmio_read32(
    volatile uint8_t *base,
    uint32_t offset
) {
    return *(volatile uint32_t *)(base + offset);
}

static void xhci_mmio_write32(
    volatile uint8_t *base,
    uint32_t offset,
    uint32_t value
) {
    *(volatile uint32_t *)(base + offset) = value;
    __asm__ volatile ("" ::: "memory");
}
static void xhci_mmio_write64(
    volatile uint8_t *base,
    uint32_t offset,
    uint64_t value
) {
    *(volatile uint64_t *)(base + offset) = value;
    __asm__ volatile ("" ::: "memory");
}

struct xhci_trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
};

struct xhci_erst_entry {
    uint64_t segment_base;
    uint32_t segment_size;
    uint32_t reserved;
};


static bool xhci_wait_mask32(
    volatile uint8_t *base,
    uint32_t offset,
    uint32_t mask,
    uint32_t expected
) {
    for (uint32_t spin = 0u;
         spin < XHCI_RESET_SPIN_LIMIT;
         ++spin) {
        if ((xhci_mmio_read32(base, offset) & mask) == expected) {
            return true;
        }
        __asm__ volatile ("pause");
    }

    return false;
}


bool xhci_probe(struct aurora_xhci_probe_result *out_result) {
    if (out_result == NULL) return false;

    *out_result = (struct aurora_xhci_probe_result){0};

    struct aurora_pci_device device;

    if (!pci_find_class(
            PCI_CLASS_SERIAL_BUS,
            PCI_SUBCLASS_USB,
            PCI_PROGIF_XHCI,
            &device)) {
        log_line("[xhci] probe fail: PCI class 0C/03/30 not found");
        return false;
    }

    uint64_t mmio = 0u;

    if (!pci_read_bar64(&device, 0u, &mmio)) {
        log_line("[xhci] probe fail: BAR0 decode");
        return false;
    }

    if (!pci_enable_memory_bus_master(&device)) {
        log_line("[xhci] probe fail: PCI memory/bus-master enable");
        return false;
    }

    if (!xhci_map_capability_page(mmio)) {
        log_line("[xhci] probe fail: capability MMIO mapping");
        return false;
    }

    uint32_t capbase = xhci_read32(XHCI_CAP_CAPLENGTH);
    uint8_t cap_length = (uint8_t)(capbase & 0xFFu);
    uint16_t version = (uint16_t)(capbase >> 16u);
    uint32_t hcsparams1 = xhci_read32(XHCI_CAP_HCSPARAMS1);
    uint32_t dboff = xhci_read32(XHCI_CAP_DBOFF) & ~0x3u;
    uint32_t rtsoff = xhci_read32(XHCI_CAP_RTSOFF) & ~0x1Fu;

    uint8_t slots = (uint8_t)(hcsparams1 & 0xFFu);
    uint16_t interrupters =
        (uint16_t)((hcsparams1 >> 8u) & 0x7FFu);
    uint8_t ports = (uint8_t)(hcsparams1 >> 24u);

    if (cap_length < 0x20u ||
        version == 0u ||
        slots == 0u ||
        interrupters == 0u ||
        ports == 0u ||
        dboff < cap_length ||
        rtsoff < cap_length) {
        log_write("[xhci] probe fail: capability validation caplen=");
        log_u64(cap_length);
        log_write(" version=");
        log_hex64(version);
        log_write(" slots=");
        log_u64(slots);
        log_write(" intr=");
        log_u64(interrupters);
        log_write(" ports=");
        log_u64(ports);
        log_write(" dboff=");
        log_hex64(dboff);
        log_write(" rtsoff=");
        log_hex64(rtsoff);
        log_line("");
        return false;
    }

    uint8_t ignored_offset = 0u;

    *out_result = (struct aurora_xhci_probe_result){
        .bus = device.bus,
        .slot = device.slot,
        .function = device.function,
        .vendor_id = device.vendor_id,
        .device_id = device.device_id,
        .mmio_physical = mmio,
        .capability_length = cap_length,
        .interface_version = version,
        .max_device_slots = slots,
        .max_interrupters = interrupters,
        .max_ports = ports,
        .doorbell_offset = dboff,
        .runtime_offset = rtsoff,
        .has_msi = pci_find_capability(
            &device,
            PCI_CAP_ID_MSI,
            &ignored_offset
        ),
        .has_msix = pci_find_capability(
            &device,
            PCI_CAP_ID_MSIX,
            &ignored_offset
        )
    };

    return true;
}


bool xhci_read_controller_state(
    const struct aurora_xhci_probe_result *probe,
    struct aurora_xhci_controller_state *out_state
) {
    if (probe == NULL ||
        out_state == NULL ||
        xhci_capability_base == NULL ||
        probe->mmio_physical == 0u ||
        probe->capability_length < 0x20u) {
        return false;
    }

    uint32_t hcsparams2 = xhci_read32(XHCI_CAP_HCSPARAMS2);
    uint32_t hccparams1 = xhci_read32(XHCI_CAP_HCCPARAMS1);

    uint16_t scratchpad_hi =
        (uint16_t)((hcsparams2 >> 21u) & 0x1Fu);
    uint16_t scratchpad_lo =
        (uint16_t)((hcsparams2 >> 27u) & 0x1Fu);
    uint16_t scratchpads =
        (uint16_t)((scratchpad_hi << 5u) | scratchpad_lo);

    uint64_t operational =
        probe->mmio_physical +
        (uint64_t)probe->capability_length;
    uint64_t runtime =
        probe->mmio_physical +
        (uint64_t)probe->runtime_offset;
    uint64_t doorbell =
        probe->mmio_physical +
        (uint64_t)probe->doorbell_offset;

    if (operational < probe->mmio_physical ||
        runtime < probe->mmio_physical ||
        doorbell < probe->mmio_physical) {
        return false;
    }

    /*
     * PAGESIZE is an operational register and is read during reset/prepare.
     * Record the static architectural facts here; the live page-size mask is
     * filled by xhci_prepare_controller().
     */
    *out_state = (struct aurora_xhci_controller_state){
        .context_size = (hccparams1 & (1u << 2)) != 0u ? 64u : 32u,
        .scratchpad_count = scratchpads,
        .operational_physical = operational,
        .runtime_physical = runtime,
        .doorbell_physical = doorbell
    };

    return true;
}


bool xhci_prepare_controller(
    const struct aurora_xhci_probe_result *probe,
    struct aurora_xhci_controller_state *state
) {
    if (probe == NULL || state == NULL) return false;

    if (!xhci_map_register_page(
            XHCI_OPERATIONAL_VIRTUAL,
            state->operational_physical,
            &xhci_operational_base) ||
        !xhci_map_register_page(
            XHCI_RUNTIME_VIRTUAL,
            state->runtime_physical,
            &xhci_runtime_base) ||
        !xhci_map_register_page(
            XHCI_DOORBELL_VIRTUAL,
            state->doorbell_physical,
            &xhci_doorbell_base)) {
        log_line("[xhci] prepare fail: register MMIO mapping");
        return false;
    }

    uint32_t command =
        xhci_mmio_read32(xhci_operational_base, XHCI_OP_USBCMD);

    if ((command & XHCI_USBCMD_RUN_STOP) != 0u) {
        command &= ~XHCI_USBCMD_RUN_STOP;
        xhci_mmio_write32(
            xhci_operational_base,
            XHCI_OP_USBCMD,
            command
        );
    }

    if (!xhci_wait_mask32(
            xhci_operational_base,
            XHCI_OP_USBSTS,
            XHCI_USBSTS_HCHALTED,
            XHCI_USBSTS_HCHALTED)) {
        log_line("[xhci] prepare fail: controller did not halt");
        return false;
    }

    command =
        xhci_mmio_read32(xhci_operational_base, XHCI_OP_USBCMD);
    command |= XHCI_USBCMD_HCRST;
    xhci_mmio_write32(
        xhci_operational_base,
        XHCI_OP_USBCMD,
        command
    );

    if (!xhci_wait_mask32(
            xhci_operational_base,
            XHCI_OP_USBCMD,
            XHCI_USBCMD_HCRST,
            0u)) {
        log_line("[xhci] prepare fail: HCRST did not clear");
        return false;
    }

    if (!xhci_wait_mask32(
            xhci_operational_base,
            XHCI_OP_USBSTS,
            XHCI_USBSTS_CNR,
            0u)) {
        log_line("[xhci] prepare fail: controller not ready");
        return false;
    }

    uint32_t page_size =
        xhci_mmio_read32(xhci_operational_base, XHCI_OP_PAGESIZE);

    state->page_size_mask = page_size;
    state->supports_4k_pages = (page_size & 1u) != 0u;

    if (!state->supports_4k_pages) {
        log_line("[xhci] prepare fail: 4KiB pages unsupported");
        return false;
    }

    return true;
}


bool xhci_bootstrap_dma(
    const struct aurora_xhci_probe_result *probe,
    struct aurora_xhci_controller_state *state
) {
    if (probe == NULL ||
        state == NULL ||
        xhci_operational_base == NULL ||
        xhci_runtime_base == NULL ||
        !state->supports_4k_pages ||
        state->running) {
        return false;
    }

    if (state->scratchpad_count > XHCI_BOOTSTRAP_SCRATCHPAD_MAX) {
        log_line("[xhci] DMA bootstrap fail: scratchpad count exceeds bootstrap bound");
        return false;
    }

    uint64_t dcbaa = pmm_alloc_page();
    uint64_t command_ring = pmm_alloc_page();
    uint64_t event_ring = pmm_alloc_page();
    uint64_t erst = pmm_alloc_page();
    uint64_t scratchpad_array = 0u;
    static uint64_t scratchpad_pages[XHCI_BOOTSTRAP_SCRATCHPAD_MAX];

    for (uint32_t i = 0u; i < XHCI_BOOTSTRAP_SCRATCHPAD_MAX; ++i) {
        scratchpad_pages[i] = 0u;
    }

    if (dcbaa == 0u ||
        command_ring == 0u ||
        event_ring == 0u ||
        erst == 0u) {
        goto fail;
    }

    uint64_t *dcbaa_virtual =
        (uint64_t *)pmm_phys_to_virt(dcbaa);

    if (state->scratchpad_count != 0u) {
        scratchpad_array = pmm_alloc_page();
        if (scratchpad_array == 0u) goto fail;

        uint64_t *array =
            (uint64_t *)pmm_phys_to_virt(scratchpad_array);

        for (uint16_t i = 0u; i < state->scratchpad_count; ++i) {
            uint64_t page = pmm_alloc_page();
            if (page == 0u) goto fail;

            scratchpad_pages[i] = page;
            array[i] = page;
        }

        dcbaa_virtual[0] = scratchpad_array;
    }

    struct xhci_trb *command =
        (struct xhci_trb *)pmm_phys_to_virt(command_ring);

    command[XHCI_RING_TRB_COUNT - 1u] = (struct xhci_trb){
        .parameter = command_ring,
        .status = 0u,
        .control =
            (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TOGGLE_CYCLE |
            XHCI_TRB_CYCLE
    };

    struct xhci_erst_entry *erst_virtual =
        (struct xhci_erst_entry *)pmm_phys_to_virt(erst);

    erst_virtual[0] = (struct xhci_erst_entry){
        .segment_base = event_ring,
        .segment_size = XHCI_RING_TRB_COUNT,
        .reserved = 0u
    };

    xhci_mmio_write64(
        xhci_operational_base,
        XHCI_OP_DCBAAP,
        dcbaa
    );

    xhci_mmio_write64(
        xhci_operational_base,
        XHCI_OP_CRCR,
        command_ring | XHCI_TRB_CYCLE
    );

    uint32_t config =
        xhci_mmio_read32(xhci_operational_base, XHCI_OP_CONFIG);
    config &= ~0xFFu;
    config |= (uint32_t)probe->max_device_slots;
    xhci_mmio_write32(
        xhci_operational_base,
        XHCI_OP_CONFIG,
        config
    );

    volatile uint8_t *interrupter0 =
        xhci_runtime_base + XHCI_RUNTIME_INTERRUPTER0;

    xhci_mmio_write32(
        interrupter0,
        XHCI_INTR_IMAN,
        0u
    );
    xhci_mmio_write32(
        interrupter0,
        XHCI_INTR_ERSTSZ,
        1u
    );
    xhci_mmio_write64(
        interrupter0,
        XHCI_INTR_ERSTBA,
        erst
    );
    xhci_mmio_write64(
        interrupter0,
        XHCI_INTR_ERDP,
        event_ring
    );

    uint32_t command_reg =
        xhci_mmio_read32(xhci_operational_base, XHCI_OP_USBCMD);
    command_reg |= XHCI_USBCMD_RUN_STOP;
    xhci_mmio_write32(
        xhci_operational_base,
        XHCI_OP_USBCMD,
        command_reg
    );

    if (!xhci_wait_mask32(
            xhci_operational_base,
            XHCI_OP_USBSTS,
            XHCI_USBSTS_HCHALTED,
            0u)) {
        log_line("[xhci] DMA bootstrap fail: controller did not enter run state");
        goto fail_running;
    }

    state->dcbaa_physical = dcbaa;
    state->scratchpad_array_physical = scratchpad_array;
    state->command_ring_physical = command_ring;
    state->event_ring_physical = event_ring;
    state->erst_physical = erst;
    state->command_enqueue = 0u;
    state->command_cycle = true;
    state->event_dequeue = 0u;
    state->event_cycle = true;
    state->dma_ready = true;
    state->running = true;
    return true;

fail_running:
    command_reg =
        xhci_mmio_read32(xhci_operational_base, XHCI_OP_USBCMD);
    command_reg &= ~XHCI_USBCMD_RUN_STOP;
    xhci_mmio_write32(
        xhci_operational_base,
        XHCI_OP_USBCMD,
        command_reg
    );
    (void)xhci_wait_mask32(
        xhci_operational_base,
        XHCI_OP_USBSTS,
        XHCI_USBSTS_HCHALTED,
        XHCI_USBSTS_HCHALTED
    );

fail:
    for (uint16_t i = 0u;
         i < state->scratchpad_count &&
         i < XHCI_BOOTSTRAP_SCRATCHPAD_MAX;
         ++i) {
        if (scratchpad_pages[i] != 0u) {
            pmm_free_page(scratchpad_pages[i]);
        }
    }

    if (scratchpad_array != 0u) pmm_free_page(scratchpad_array);
    if (erst != 0u) pmm_free_page(erst);
    if (event_ring != 0u) pmm_free_page(event_ring);
    if (command_ring != 0u) pmm_free_page(command_ring);
    if (dcbaa != 0u) pmm_free_page(dcbaa);
    return false;
}


bool xhci_submit_enable_slot(
    struct aurora_xhci_controller_state *state,
    uint64_t *out_command_trb_physical
) {
    if (out_command_trb_physical != NULL) {
        *out_command_trb_physical = 0u;
    }

    if (state == NULL ||
        out_command_trb_physical == NULL ||
        !state->dma_ready ||
        !state->running ||
        state->command_ring_physical == 0u ||
        xhci_doorbell_base == NULL ||
        state->command_enqueue >= XHCI_RING_TRB_COUNT - 1u) {
        return false;
    }

    struct xhci_trb *ring =
        (struct xhci_trb *)pmm_phys_to_virt(
            state->command_ring_physical
        );

    uint16_t index = state->command_enqueue;
    uint32_t cycle = state->command_cycle
        ? XHCI_TRB_CYCLE
        : 0u;

    ring[index] = (struct xhci_trb){
        .parameter = 0u,
        .status = 0u,
        .control =
            (XHCI_TRB_TYPE_ENABLE_SLOT << XHCI_TRB_TYPE_SHIFT) |
            cycle
    };

    __asm__ volatile ("" ::: "memory");

    *out_command_trb_physical =
        state->command_ring_physical +
        (uint64_t)index * sizeof(struct xhci_trb);

    ++state->command_enqueue;

    if (state->command_enqueue ==
        XHCI_RING_TRB_COUNT - 1u) {
        struct xhci_trb *link =
            &ring[XHCI_RING_TRB_COUNT - 1u];

        uint32_t link_cycle =
            state->command_cycle
                ? XHCI_TRB_CYCLE
                : 0u;

        link->control =
            (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TOGGLE_CYCLE |
            link_cycle;

        __asm__ volatile ("" ::: "memory");

        state->command_enqueue = 0u;
        state->command_cycle = !state->command_cycle;
    }

    /*
     * Doorbell 0 targets the command ring. A zero write is the xHCI command
     * doorbell value; the controller consumes the newly published TRB.
     */
    xhci_mmio_write32(
        xhci_doorbell_base,
        0u,
        0u
    );

    return true;
}


bool xhci_wait_command_completion(
    struct aurora_xhci_controller_state *state,
    uint64_t command_trb_physical,
    uint8_t *out_slot_id
) {
    if (out_slot_id != NULL) *out_slot_id = 0u;

    if (state == NULL ||
        out_slot_id == NULL ||
        !state->dma_ready ||
        !state->running ||
        state->event_ring_physical == 0u ||
        xhci_runtime_base == NULL ||
        state->event_dequeue >= XHCI_RING_TRB_COUNT) {
        return false;
    }

    struct xhci_trb *events =
        (struct xhci_trb *)pmm_phys_to_virt(
            state->event_ring_physical
        );

    struct xhci_trb event = {0};
    bool ready = false;

    for (uint32_t spin = 0u;
         spin < XHCI_EVENT_SPIN_LIMIT;
         ++spin) {
        event = events[state->event_dequeue];

        bool cycle =
            (event.control & XHCI_TRB_CYCLE) != 0u;

        if (cycle == state->event_cycle) {
            ready = true;
            break;
        }

        __asm__ volatile ("pause");
    }

    if (!ready) {
        log_line("[xhci] command completion timeout");
        return false;
    }

    uint32_t type =
        (event.control >> XHCI_TRB_TYPE_SHIFT) & 0x3Fu;
    uint8_t completion_code =
        (uint8_t)(event.status >> 24u);
    uint8_t slot_id =
        (uint8_t)(event.control >> 24u);

    if (type != XHCI_TRB_TYPE_COMMAND_COMPLETION ||
        completion_code != XHCI_COMPLETION_SUCCESS ||
        event.parameter != command_trb_physical ||
        slot_id == 0u) {
        log_write("[xhci] bad command completion type=");
        log_u64(type);
        log_write(" code=");
        log_u64(completion_code);
        log_write(" slot=");
        log_u64(slot_id);
        log_write(" ptr=");
        log_hex64(event.parameter);
        log_line("");
        return false;
    }

    ++state->event_dequeue;
    if (state->event_dequeue == XHCI_RING_TRB_COUNT) {
        state->event_dequeue = 0u;
        state->event_cycle = !state->event_cycle;
    }

    uint64_t dequeue_physical =
        state->event_ring_physical +
        (uint64_t)state->event_dequeue *
        sizeof(struct xhci_trb);

    volatile uint8_t *interrupter0 =
        xhci_runtime_base + XHCI_RUNTIME_INTERRUPTER0;

    xhci_mmio_write64(
        interrupter0,
        XHCI_INTR_ERDP,
        dequeue_physical | (1ull << 3)
    );

    *out_slot_id = slot_id;
    return true;
}


bool xhci_reset_first_connected_port(
    const struct aurora_xhci_probe_result *probe,
    uint8_t *out_port_id,
    uint8_t *out_speed_id
) {
    if (out_port_id != NULL) *out_port_id = 0u;
    if (out_speed_id != NULL) *out_speed_id = 0u;

    if (probe == NULL ||
        out_port_id == NULL ||
        out_speed_id == NULL ||
        xhci_operational_base == NULL ||
        probe->max_ports == 0u) {
        return false;
    }

    for (uint8_t port = 0u; port < probe->max_ports; ++port) {
        uint32_t offset =
            XHCI_OP_PORT_BASE +
            (uint32_t)port * XHCI_OP_PORT_STRIDE +
            XHCI_PORTSC;

        uint32_t portsc =
            xhci_mmio_read32(xhci_operational_base, offset);

        if ((portsc & XHCI_PORTSC_CCS) == 0u) {
            continue;
        }

        /*
         * PORTSC change-status bits are RW1C. Clear them from the value we
         * write back so starting a reset cannot accidentally acknowledge
         * unrelated status transitions.
         */
        uint32_t reset_value =
            portsc & ~XHCI_PORTSC_RW1C_MASK;

        reset_value |= XHCI_PORTSC_PP;
        reset_value |= XHCI_PORTSC_PR;

        xhci_mmio_write32(
            xhci_operational_base,
            offset,
            reset_value
        );

        bool reset_done = false;

        for (uint32_t spin = 0u;
             spin < XHCI_PORT_RESET_SPIN_LIMIT;
             ++spin) {
            portsc =
                xhci_mmio_read32(
                    xhci_operational_base,
                    offset
                );

            if ((portsc & XHCI_PORTSC_PR) == 0u &&
                (portsc & XHCI_PORTSC_PED) != 0u &&
                (portsc & XHCI_PORTSC_CCS) != 0u) {
                reset_done = true;
                break;
            }

            __asm__ volatile ("pause");
        }

        if (!reset_done) {
            log_write("[xhci] port reset failed port ");
            log_u64((uint64_t)port + 1u);
            log_line("");
            return false;
        }

        uint8_t speed =
            (uint8_t)(
                (portsc & XHCI_PORTSC_SPEED_MASK) >>
                XHCI_PORTSC_SPEED_SHIFT
            );

        if (speed == 0u) {
            return false;
        }

        *out_port_id = (uint8_t)(port + 1u);
        *out_speed_id = speed;
        return true;
    }

    return false;
}
