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
#define XHCI_PORTSC_CSC             (1u << 17)
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

#define XHCI_TRB_TYPE_NORMAL         1u
#define XHCI_TRB_TYPE_SETUP_STAGE    2u
#define XHCI_TRB_TYPE_DATA_STAGE     3u
#define XHCI_TRB_TYPE_STATUS_STAGE   4u
#define XHCI_TRB_TYPE_LINK           6u
#define XHCI_TRB_TYPE_ENABLE_SLOT    9u
#define XHCI_TRB_TYPE_DISABLE_SLOT   10u
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11u
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12u
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32u
#define XHCI_TRB_TYPE_COMMAND_COMPLETION 33u
#define XHCI_TRB_TYPE_PORT_STATUS_CHANGE 34u
#define XHCI_COMPLETION_SUCCESS      1u
#define XHCI_EVENT_SPIN_LIMIT        10000000u
#define XHCI_ASYNC_EVENT_SPIN_LIMIT  2000000000u
#define XHCI_TRB_CYCLE               (1u << 0)
#define XHCI_TRB_TOGGLE_CYCLE        (1u << 1)
#define XHCI_TRB_CHAIN               (1u << 4)
#define XHCI_TRB_IOC                 (1u << 5)
#define XHCI_TRB_IDT                 (1u << 6)
#define XHCI_TRB_DIR_IN              (1u << 16)
#define XHCI_SETUP_TRT_IN            (3u << 16)
#define XHCI_TRB_TYPE_SHIFT          10u
#define XHCI_RING_TRB_COUNT          256u
#define XHCI_BOOTSTRAP_SCRATCHPAD_MAX 64u

#define XHCI_CONTEXT_SLOT_INDEX      0u
#define XHCI_CONTEXT_EP0_INDEX       1u
#define XHCI_INPUT_CONTROL_CONTEXT   0u
#define XHCI_INPUT_SLOT_CONTEXT      1u
#define XHCI_INPUT_EP0_CONTEXT       2u

#define XHCI_SLOT_CONTEXT_ENTRIES_SHIFT 27u
#define XHCI_SLOT_CONTEXT_SPEED_SHIFT   20u
#define XHCI_SLOT_CONTEXT_ROOT_PORT_SHIFT 16u

#define XHCI_EP_CONTEXT_STATE_MASK   0x7u
#define XHCI_EP_CONTEXT_CERR_SHIFT   1u
#define XHCI_EP_CONTEXT_TYPE_SHIFT   3u
#define XHCI_EP_CONTEXT_MAX_PACKET_SHIFT 16u
#define XHCI_EP_TYPE_CONTROL         4u
#define XHCI_EP_TYPE_INTERRUPT_IN    7u
#define XHCI_EP0_ERROR_COUNT         3u

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


static uint32_t *xhci_context_ptr(
    uint64_t physical,
    uint8_t context_size,
    uint32_t index
) {
    if (physical == 0u ||
        (context_size != 32u && context_size != 64u)) {
        return NULL;
    }

    uint8_t *base =
        (uint8_t *)pmm_phys_to_virt(physical);

    return (uint32_t *)(
        base + (uint64_t)context_size * index
    );
}

static uint16_t xhci_ep0_max_packet(uint8_t speed_id) {
    switch (speed_id) {
        case 1u: return 64u;   /* Full-speed */
        case 2u: return 8u;    /* Low-speed */
        case 3u: return 64u;   /* High-speed */
        case 4u: return 512u;  /* SuperSpeed */
        default: return 0u;
    }
}


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


bool xhci_disable_slot(
    struct aurora_xhci_controller_state *state,
    uint8_t slot_id
) {
    if (state == NULL ||
        !state->dma_ready ||
        !state->running ||
        slot_id == 0u ||
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
    uint32_t cycle =
        state->command_cycle ? XHCI_TRB_CYCLE : 0u;

    ring[index] = (struct xhci_trb){
        .parameter = 0u,
        .status = 0u,
        .control =
            (XHCI_TRB_TYPE_DISABLE_SLOT << XHCI_TRB_TYPE_SHIFT) |
            ((uint32_t)slot_id << 24u) |
            cycle
    };

    __asm__ volatile ("" ::: "memory");

    uint64_t command_physical =
        state->command_ring_physical +
        (uint64_t)index * sizeof(struct xhci_trb);

    ++state->command_enqueue;

    if (state->command_enqueue ==
        XHCI_RING_TRB_COUNT - 1u) {
        struct xhci_trb *link =
            &ring[XHCI_RING_TRB_COUNT - 1u];

        uint32_t link_cycle =
            state->command_cycle ? XHCI_TRB_CYCLE : 0u;

        link->control =
            (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TOGGLE_CYCLE |
            link_cycle;

        __asm__ volatile ("" ::: "memory");

        state->command_enqueue = 0u;
        state->command_cycle = !state->command_cycle;
    }

    xhci_mmio_write32(
        xhci_doorbell_base,
        0u,
        0u
    );

    uint8_t completion_slot = 0u;

    bool completed =
        xhci_wait_command_completion(
            state,
            command_physical,
            &completion_slot
        ) &&
        completion_slot == slot_id;

    if (completed && state->addressed_slot_id == slot_id) {
        state->addressed_slot_disabled = true;
    }

    return completed;
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

    volatile uint8_t *interrupter0 =
        xhci_runtime_base + XHCI_RUNTIME_INTERRUPTER0;

    for (uint32_t spin = 0u;
         spin < XHCI_EVENT_SPIN_LIMIT;
         ++spin) {
        struct xhci_trb event =
            events[state->event_dequeue];

        bool cycle =
            (event.control & XHCI_TRB_CYCLE) != 0u;

        if (cycle != state->event_cycle) {
            __asm__ volatile ("pause");
            continue;
        }

        uint32_t type =
            (event.control >> XHCI_TRB_TYPE_SHIFT) & 0x3Fu;

        /*
         * Port reset legitimately produces a Port Status Change Event before
         * the following Enable Slot completion. Consume that asynchronous
         * event and continue waiting. Unknown event classes fail closed.
         */
        if (type != XHCI_TRB_TYPE_COMMAND_COMPLETION &&
            type != XHCI_TRB_TYPE_PORT_STATUS_CHANGE) {
            log_write("[xhci] unexpected event while waiting command type=");
            log_u64(type);
            log_line("");
            return false;
        }

        uint16_t consumed_index = state->event_dequeue;
        ++state->event_dequeue;

        if (state->event_dequeue == XHCI_RING_TRB_COUNT) {
            state->event_dequeue = 0u;
            state->event_cycle = !state->event_cycle;
        }

        uint64_t dequeue_physical =
            state->event_ring_physical +
            (uint64_t)state->event_dequeue *
            sizeof(struct xhci_trb);

        xhci_mmio_write64(
            interrupter0,
            XHCI_INTR_ERDP,
            dequeue_physical | (1ull << 3)
        );

        if (type == XHCI_TRB_TYPE_PORT_STATUS_CHANGE) {
            uint8_t port_id =
                (uint8_t)(event.parameter >> 24u);

            log_write("[xhci] consumed port-status event port ");
            log_u64(port_id);
            log_line("");

            /*
             * Mark the consumed slot locally after advancing ERDP. Hardware
             * ownership is governed by the cycle bit, so no TRB mutation is
             * required here.
             */
            (void)consumed_index;
            continue;
        }

        uint8_t completion_code =
            (uint8_t)(event.status >> 24u);
        uint8_t slot_id =
            (uint8_t)(event.control >> 24u);

        if (completion_code != XHCI_COMPLETION_SUCCESS ||
            event.parameter != command_trb_physical ||
            slot_id == 0u) {
            log_write("[xhci] bad command completion code=");
            log_u64(completion_code);
            log_write(" slot=");
            log_u64(slot_id);
            log_write(" ptr=");
            log_hex64(event.parameter);
            log_line("");
            return false;
        }

        *out_slot_id = slot_id;
        return true;
    }

    log_line("[xhci] command completion timeout");
    return false;
}

bool xhci_wait_port_status_change(
    struct aurora_xhci_controller_state *state,
    uint8_t expected_port_id,
    uint32_t *out_portsc
) {
    if (out_portsc != NULL) *out_portsc = 0u;

    if (state == NULL ||
        out_portsc == NULL ||
        expected_port_id == 0u ||
        state->event_ring_physical == 0u ||
        xhci_runtime_base == NULL ||
        xhci_operational_base == NULL ||
        state->event_dequeue >= XHCI_RING_TRB_COUNT) {
        return false;
    }

    struct xhci_trb *events =
        (struct xhci_trb *)pmm_phys_to_virt(
            state->event_ring_physical
        );

    volatile uint8_t *interrupter0 =
        xhci_runtime_base + XHCI_RUNTIME_INTERRUPTER0;

    for (uint32_t spin = 0u;
         spin < XHCI_ASYNC_EVENT_SPIN_LIMIT;
         ++spin) {
        struct xhci_trb event =
            events[state->event_dequeue];

        bool cycle =
            (event.control & XHCI_TRB_CYCLE) != 0u;

        if (cycle != state->event_cycle) {
            __asm__ volatile ("pause");
            continue;
        }

        uint32_t type =
            (event.control >> XHCI_TRB_TYPE_SHIFT) & 0x3Fu;

        if (type != XHCI_TRB_TYPE_PORT_STATUS_CHANGE &&
            type != XHCI_TRB_TYPE_TRANSFER_EVENT) {
            log_write("[xhci] unexpected event while waiting port change type=");
            log_u64(type);
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

        xhci_mmio_write64(
            interrupter0,
            XHCI_INTR_ERDP,
            dequeue_physical | (1ull << 3)
        );

        if (type == XHCI_TRB_TYPE_TRANSFER_EVENT) {
            uint8_t event_slot =
                (uint8_t)(event.control >> 24u);
            uint8_t endpoint_id =
                (uint8_t)((event.control >> 16u) & 0x1Fu);
            uint8_t completion_code =
                (uint8_t)(event.status >> 24u);

            log_write("[xhci] drained transfer event during disconnect slot ");
            log_u64(event_slot);
            log_write(" ep ");
            log_u64(endpoint_id);
            log_write(" code ");
            log_u64(completion_code);
            log_line("");

            continue;
        }

        uint8_t port_id =
            (uint8_t)(event.parameter >> 24u);

        if (port_id != expected_port_id) {
            log_write("[xhci] port-status event for unexpected port ");
            log_u64(port_id);
            log_line("");
            return false;
        }

        uint32_t offset =
            XHCI_OP_PORT_BASE +
            (uint32_t)(port_id - 1u) *
            XHCI_OP_PORT_STRIDE +
            XHCI_PORTSC;

        *out_portsc =
            xhci_mmio_read32(
                xhci_operational_base,
                offset
            );

        return true;
    }

    log_line("[xhci] port-status change timeout");
    return false;
}

bool xhci_acknowledge_port_disconnect(
    uint8_t port_id,
    uint32_t portsc
) {
    if (port_id == 0u ||
        xhci_operational_base == NULL ||
        (portsc & XHCI_PORTSC_CCS) != 0u ||
        (portsc & XHCI_PORTSC_CSC) == 0u) {
        return false;
    }

    uint32_t offset =
        XHCI_OP_PORT_BASE +
        (uint32_t)(port_id - 1u) *
        XHCI_OP_PORT_STRIDE +
        XHCI_PORTSC;

    /*
     * PORTSC change bits are RW1C.  Preserve the currently asserted
     * non-change state (notably Port Power) while writing back all change
     * flags observed in the snapshot so they are acknowledged atomically.
     * On a disconnected port PED/PR are already clear, so this does not
     * disable an active endpoint or restart reset.
     */
    uint32_t acknowledge =
        (portsc & ~XHCI_PORTSC_RW1C_MASK) |
        (portsc & XHCI_PORTSC_RW1C_MASK);

    xhci_mmio_write32(
        xhci_operational_base,
        offset,
        acknowledge
    );

    uint32_t after =
        xhci_mmio_read32(
            xhci_operational_base,
            offset
        );

    if ((after & XHCI_PORTSC_CCS) != 0u ||
        (after & XHCI_PORTSC_CSC) != 0u) {
        log_write("[xhci] disconnect acknowledge failed port ");
        log_u64(port_id);
        log_write(" portsc ");
        log_hex64(after);
        log_line("");
        return false;
    }

    return true;
}

bool xhci_reset_connected_port_after(
    const struct aurora_xhci_probe_result *probe,
    uint8_t after_port_id,
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

    uint8_t start_port =
        after_port_id >= probe->max_ports
            ? probe->max_ports
            : after_port_id;

    for (uint8_t port = start_port;
         port < probe->max_ports;
         ++port) {
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


bool xhci_reset_first_connected_port(
    const struct aurora_xhci_probe_result *probe,
    uint8_t *out_port_id,
    uint8_t *out_speed_id
) {
    return xhci_reset_connected_port_after(
        probe,
        0u,
        out_port_id,
        out_speed_id
    );
}

bool xhci_prepare_address_device(
    struct aurora_xhci_controller_state *state,
    uint8_t slot_id,
    uint8_t port_id,
    uint8_t speed_id
) {
    if (state == NULL ||
        !state->dma_ready ||
        !state->running ||
        state->dcbaa_physical == 0u ||
        slot_id == 0u ||
        port_id == 0u ||
        speed_id == 0u ||
        state->context_size == 0u ||
        state->device_context_physical != 0u ||
        state->input_context_physical != 0u ||
        state->ep0_ring_physical != 0u) {
        return false;
    }

    uint16_t max_packet = xhci_ep0_max_packet(speed_id);
    if (max_packet == 0u) return false;

    uint64_t device_context = pmm_alloc_page();
    uint64_t input_context = pmm_alloc_page();
    uint64_t ep0_ring = pmm_alloc_page();

    if (device_context == 0u ||
        input_context == 0u ||
        ep0_ring == 0u) {
        if (ep0_ring != 0u) pmm_free_page(ep0_ring);
        if (input_context != 0u) pmm_free_page(input_context);
        if (device_context != 0u) pmm_free_page(device_context);
        return false;
    }

    uint64_t *dcbaa =
        (uint64_t *)pmm_phys_to_virt(
            state->dcbaa_physical
        );
    dcbaa[slot_id] = device_context;

    uint32_t *control = xhci_context_ptr(
        input_context,
        state->context_size,
        XHCI_INPUT_CONTROL_CONTEXT
    );
    uint32_t *slot = xhci_context_ptr(
        input_context,
        state->context_size,
        XHCI_INPUT_SLOT_CONTEXT
    );
    uint32_t *ep0 = xhci_context_ptr(
        input_context,
        state->context_size,
        XHCI_INPUT_EP0_CONTEXT
    );

    if (control == NULL || slot == NULL || ep0 == NULL) {
        dcbaa[slot_id] = 0u;
        pmm_free_page(ep0_ring);
        pmm_free_page(input_context);
        pmm_free_page(device_context);
        return false;
    }

    /*
     * Add Slot Context (bit 0) and Endpoint 0 Context (bit 1).
     */
    control[1] = (1u << 0) | (1u << 1);

    slot[0] =
        ((uint32_t)speed_id << XHCI_SLOT_CONTEXT_SPEED_SHIFT) |
        (1u << XHCI_SLOT_CONTEXT_ENTRIES_SHIFT);
    slot[1] =
        ((uint32_t)port_id << XHCI_SLOT_CONTEXT_ROOT_PORT_SHIFT);

    ep0[1] =
        ((uint32_t)XHCI_EP0_ERROR_COUNT << XHCI_EP_CONTEXT_CERR_SHIFT) |
        ((uint32_t)XHCI_EP_TYPE_CONTROL << XHCI_EP_CONTEXT_TYPE_SHIFT) |
        ((uint32_t)max_packet << XHCI_EP_CONTEXT_MAX_PACKET_SHIFT);

    ep0[2] = (uint32_t)(ep0_ring | 1u);
    ep0[3] = (uint32_t)(ep0_ring >> 32u);
    ep0[4] = 8u;

    struct xhci_trb *ring =
        (struct xhci_trb *)pmm_phys_to_virt(ep0_ring);

    ring[XHCI_RING_TRB_COUNT - 1u] = (struct xhci_trb){
        .parameter = ep0_ring,
        .status = 0u,
        .control =
            (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TOGGLE_CYCLE |
            XHCI_TRB_CYCLE
    };

    __asm__ volatile ("" ::: "memory");

    state->device_context_physical = device_context;
    state->input_context_physical = input_context;
    state->ep0_ring_physical = ep0_ring;
    state->addressed_slot_id = slot_id;
    state->ep0_enqueue = 0u;
    state->ep0_cycle = true;
    return true;
}


bool xhci_submit_address_device(
    struct aurora_xhci_controller_state *state,
    uint8_t slot_id,
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
        state->input_context_physical == 0u ||
        state->device_context_physical == 0u ||
        state->ep0_ring_physical == 0u ||
        state->addressed_slot_id != slot_id ||
        slot_id == 0u ||
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
        .parameter = state->input_context_physical,
        .status = 0u,
        .control =
            (XHCI_TRB_TYPE_ADDRESS_DEVICE << XHCI_TRB_TYPE_SHIFT) |
            ((uint32_t)slot_id << 24u) |
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

    xhci_mmio_write32(
        xhci_doorbell_base,
        0u,
        0u
    );

    return true;
}


bool xhci_validate_addressed_device(
    struct aurora_xhci_controller_state *state,
    uint8_t slot_id
) {
    if (state == NULL ||
        slot_id == 0u ||
        state->addressed_slot_id != slot_id ||
        state->device_context_physical == 0u ||
        state->context_size == 0u) {
        return false;
    }

    uint32_t *slot = xhci_context_ptr(
        state->device_context_physical,
        state->context_size,
        XHCI_CONTEXT_SLOT_INDEX
    );
    uint32_t *ep0 = xhci_context_ptr(
        state->device_context_physical,
        state->context_size,
        XHCI_CONTEXT_EP0_INDEX
    );

    if (slot == NULL || ep0 == NULL) return false;

    uint8_t usb_address = (uint8_t)(slot[3] & 0xFFu);
    uint8_t slot_state = (uint8_t)((slot[3] >> 27u) & 0x1Fu);
    uint8_t ep0_state =
        (uint8_t)(ep0[0] & XHCI_EP_CONTEXT_STATE_MASK);

    /*
     * After Address Device completes successfully, the controller must have
     * assigned a non-zero USB address, advanced the Slot Context out of the
     * disabled state and placed default control endpoint 0 into Running.
     */
    if (usb_address == 0u ||
        slot_state == 0u ||
        ep0_state != 1u) {
        log_write("[xhci] addressed context invalid address=");
        log_u64(usb_address);
        log_write(" slot-state=");
        log_u64(slot_state);
        log_write(" ep0-state=");
        log_u64(ep0_state);
        log_line("");
        return false;
    }

    uint64_t *dcbaa =
        (uint64_t *)pmm_phys_to_virt(
            state->dcbaa_physical
        );

    if (dcbaa[slot_id] != state->device_context_physical) {
        log_line("[xhci] addressed context invalid DCBAA slot pointer");
        return false;
    }

    state->usb_device_address = usb_address;
    state->ep0_state = ep0_state;
    return true;
}


static bool xhci_ep0_push_trb(
    struct aurora_xhci_controller_state *state,
    const struct xhci_trb *template_trb,
    uint64_t *out_physical
) {
    if (out_physical != NULL) *out_physical = 0u;

    if (state == NULL ||
        template_trb == NULL ||
        out_physical == NULL ||
        state->ep0_ring_physical == 0u ||
        state->ep0_enqueue >= XHCI_RING_TRB_COUNT - 1u) {
        return false;
    }

    struct xhci_trb *ring =
        (struct xhci_trb *)pmm_phys_to_virt(
            state->ep0_ring_physical
        );

    uint16_t index = state->ep0_enqueue;
    uint32_t cycle =
        state->ep0_cycle ? XHCI_TRB_CYCLE : 0u;

    ring[index] = *template_trb;
    ring[index].control =
        (ring[index].control & ~XHCI_TRB_CYCLE) | cycle;

    __asm__ volatile ("" ::: "memory");

    *out_physical =
        state->ep0_ring_physical +
        (uint64_t)index * sizeof(struct xhci_trb);

    ++state->ep0_enqueue;

    if (state->ep0_enqueue ==
        XHCI_RING_TRB_COUNT - 1u) {
        struct xhci_trb *link =
            &ring[XHCI_RING_TRB_COUNT - 1u];

        uint32_t link_cycle =
            state->ep0_cycle ? XHCI_TRB_CYCLE : 0u;

        link->control =
            (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TOGGLE_CYCLE |
            link_cycle;

        __asm__ volatile ("" ::: "memory");

        state->ep0_enqueue = 0u;
        state->ep0_cycle = !state->ep0_cycle;
    }

    return true;
}

static bool xhci_wait_transfer_completion(
    struct aurora_xhci_controller_state *state,
    uint8_t slot_id,
    uint64_t completion_trb_physical
) {
    if (state == NULL ||
        slot_id == 0u ||
        completion_trb_physical == 0u ||
        state->event_ring_physical == 0u ||
        xhci_runtime_base == NULL) {
        return false;
    }

    struct xhci_trb *events =
        (struct xhci_trb *)pmm_phys_to_virt(
            state->event_ring_physical
        );

    volatile uint8_t *interrupter0 =
        xhci_runtime_base + XHCI_RUNTIME_INTERRUPTER0;

    for (uint32_t spin = 0u;
         spin < XHCI_EVENT_SPIN_LIMIT;
         ++spin) {
        struct xhci_trb event =
            events[state->event_dequeue];

        bool cycle =
            (event.control & XHCI_TRB_CYCLE) != 0u;

        if (cycle != state->event_cycle) {
            __asm__ volatile ("pause");
            continue;
        }

        uint32_t type =
            (event.control >> XHCI_TRB_TYPE_SHIFT) & 0x3Fu;

        if (type != XHCI_TRB_TYPE_TRANSFER_EVENT &&
            type != XHCI_TRB_TYPE_PORT_STATUS_CHANGE) {
            log_write("[xhci] unexpected event while waiting transfer type=");
            log_u64(type);
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

        xhci_mmio_write64(
            interrupter0,
            XHCI_INTR_ERDP,
            dequeue_physical | (1ull << 3)
        );

        if (type == XHCI_TRB_TYPE_PORT_STATUS_CHANGE) {
            continue;
        }

        uint8_t completion_code =
            (uint8_t)(event.status >> 24u);
        uint8_t event_slot =
            (uint8_t)(event.control >> 24u);
        uint8_t endpoint_id =
            (uint8_t)((event.control >> 16u) & 0x1Fu);

        if (completion_code != XHCI_COMPLETION_SUCCESS ||
            event_slot != slot_id ||
            endpoint_id != 1u ||
            event.parameter != completion_trb_physical) {
            log_write("[xhci] bad transfer completion code=");
            log_u64(completion_code);
            log_write(" slot=");
            log_u64(event_slot);
            log_write(" ep=");
            log_u64(endpoint_id);
            log_write(" ptr=");
            log_hex64(event.parameter);
            log_line("");
            return false;
        }

        return true;
    }

    log_line("[xhci] transfer completion timeout");
    return false;
}

bool xhci_control_in(
    struct aurora_xhci_controller_state *state,
    uint8_t request_type,
    uint8_t request,
    uint16_t value,
    uint16_t index,
    void *buffer,
    uint16_t length
) {
    if (state == NULL ||
        buffer == NULL ||
        length == 0u ||
        length > XHCI_PAGE_SIZE ||
        state->addressed_slot_id == 0u ||
        state->ep0_state != 1u ||
        state->ep0_ring_physical == 0u ||
        xhci_doorbell_base == NULL) {
        return false;
    }

    uint64_t data_page = pmm_alloc_page();
    if (data_page == 0u) return false;

    uint64_t setup_packet =
        (uint64_t)request_type |
        ((uint64_t)request << 8u) |
        ((uint64_t)value << 16u) |
        ((uint64_t)index << 32u) |
        ((uint64_t)length << 48u);

    struct xhci_trb setup = {
        .parameter = setup_packet,
        .status = 8u,
        .control =
            (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_IDT |
            XHCI_TRB_CHAIN |
            XHCI_SETUP_TRT_IN
    };

    struct xhci_trb data = {
        .parameter = data_page,
        .status = (uint32_t)length,
        .control =
            (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_CHAIN |
            XHCI_TRB_DIR_IN
    };

    struct xhci_trb status = {
        .parameter = 0u,
        .status = 0u,
        .control =
            (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_IOC
    };

    uint64_t ignored = 0u;
    uint64_t completion_trb = 0u;

    if (!xhci_ep0_push_trb(state, &setup, &ignored) ||
        !xhci_ep0_push_trb(state, &data, &ignored) ||
        !xhci_ep0_push_trb(
            state,
            &status,
            &completion_trb)) {
        pmm_free_page(data_page);
        return false;
    }

    __asm__ volatile ("" ::: "memory");

    xhci_mmio_write32(
        xhci_doorbell_base,
        (uint32_t)state->addressed_slot_id * 4u,
        1u
    );

    if (!xhci_wait_transfer_completion(
            state,
            state->addressed_slot_id,
            completion_trb)) {
        pmm_free_page(data_page);
        return false;
    }

    const uint8_t *source =
        (const uint8_t *)pmm_phys_to_virt(data_page);
    uint8_t *destination = (uint8_t *)buffer;

    for (uint16_t i = 0u; i < length; ++i) {
        destination[i] = source[i];
    }

    pmm_free_page(data_page);
    return true;
}


static uint16_t usb_read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] |
        ((uint16_t)bytes[1] << 8u);
}

bool xhci_get_device_descriptor(
    struct aurora_xhci_controller_state *state,
    struct aurora_usb_device_descriptor *out_descriptor
) {
    if (state == NULL || out_descriptor == NULL) {
        return false;
    }

    uint8_t raw[18] = {0};

    if (!xhci_control_in(
            state,
            0x80u,
            0x06u,
            0x0100u,
            0u,
            raw,
            sizeof(raw))) {
        log_line("[xhci] GET_DESCRIPTOR(Device) transfer failed");
        return false;
    }

    if (raw[0] != 18u || raw[1] != 0x01u) {
        log_write("[xhci] invalid Device Descriptor length/type ");
        log_u64(raw[0]);
        log_write("/");
        log_u64(raw[1]);
        log_line("");
        return false;
    }

    uint8_t max_packet = raw[7];

    if (max_packet != 8u &&
        max_packet != 16u &&
        max_packet != 32u &&
        max_packet != 64u) {
        log_write("[xhci] invalid bMaxPacketSize0 ");
        log_u64(max_packet);
        log_line("");
        return false;
    }

    if (raw[17] == 0u) {
        log_line("[xhci] Device Descriptor reports zero configurations");
        return false;
    }

    *out_descriptor = (struct aurora_usb_device_descriptor){
        .usb_version_bcd = usb_read_le16(&raw[2]),
        .device_class = raw[4],
        .device_subclass = raw[5],
        .device_protocol = raw[6],
        .max_packet_size0 = max_packet,
        .vendor_id = usb_read_le16(&raw[8]),
        .product_id = usb_read_le16(&raw[10]),
        .device_version_bcd = usb_read_le16(&raw[12]),
        .manufacturer_string = raw[14],
        .product_string = raw[15],
        .serial_string = raw[16],
        .configuration_count = raw[17]
    };

    return true;
}


#define AURORA_USB_CONFIGURATION_MAX 4096u

bool xhci_find_boot_hid_endpoint(
    struct aurora_xhci_controller_state *state,
    struct aurora_usb_hid_endpoint_descriptor *out_endpoint
) {
    if (state == NULL || out_endpoint == NULL) {
        return false;
    }

    uint8_t header[9] = {0};

    if (!xhci_control_in(
            state,
            0x80u,
            0x06u,
            0x0200u,
            0u,
            header,
            sizeof(header))) {
        log_line("[xhci] GET_DESCRIPTOR(Configuration header) failed");
        return false;
    }

    if (header[0] < 9u || header[1] != 0x02u) {
        log_line("[xhci] invalid Configuration Descriptor header");
        return false;
    }

    uint16_t total_length = usb_read_le16(&header[2]);

    if (total_length < 9u ||
        total_length > AURORA_USB_CONFIGURATION_MAX) {
        log_write("[xhci] invalid configuration total length ");
        log_u64(total_length);
        log_line("");
        return false;
    }

    static uint8_t raw[AURORA_USB_CONFIGURATION_MAX];

    if (!xhci_control_in(
            state,
            0x80u,
            0x06u,
            0x0200u,
            0u,
            raw,
            total_length)) {
        log_line("[xhci] GET_DESCRIPTOR(Configuration) failed");
        return false;
    }

    if (raw[0] < 9u ||
        raw[1] != 0x02u ||
        usb_read_le16(&raw[2]) != total_length ||
        raw[5] == 0u) {
        log_line("[xhci] malformed Configuration Descriptor root");
        return false;
    }

    uint8_t configuration_value = raw[5];
    bool in_boot_hid_interface = false;
    uint8_t interface_number = 0u;
    uint8_t interface_subclass = 0u;
    uint8_t interface_protocol = 0u;
    uint16_t report_descriptor_length = 0u;

    uint16_t offset = 0u;

    while (offset < total_length) {
        if ((uint16_t)(total_length - offset) < 2u) {
            return false;
        }

        uint8_t length = raw[offset];
        uint8_t type = raw[offset + 1u];

        if (length < 2u ||
            (uint16_t)(offset + length) > total_length) {
            log_line("[xhci] malformed USB descriptor chain");
            return false;
        }

        if (type == 0x04u) {
            if (length < 9u) return false;

            interface_number = raw[offset + 2u];
            uint8_t alternate_setting = raw[offset + 3u];
            uint8_t interface_class = raw[offset + 5u];
            interface_subclass = raw[offset + 6u];
            interface_protocol = raw[offset + 7u];
            report_descriptor_length = 0u;

            in_boot_hid_interface =
                alternate_setting == 0u &&
                interface_class == 0x03u &&
                interface_subclass == 0x01u &&
                (interface_protocol == 0x01u ||
                 interface_protocol == 0x02u);
        } else if (type == 0x21u &&
                   in_boot_hid_interface) {
            if (length < 9u) return false;

            uint8_t descriptor_count = raw[offset + 5u];
            if (descriptor_count == 0u) return false;

            uint16_t cursor = (uint16_t)(offset + 6u);
            uint16_t descriptor_end = (uint16_t)(offset + length);

            for (uint8_t i = 0u; i < descriptor_count; ++i) {
                if ((uint16_t)(descriptor_end - cursor) < 3u) {
                    return false;
                }

                uint8_t subordinate_type = raw[cursor];
                uint16_t subordinate_length =
                    usb_read_le16(&raw[cursor + 1u]);

                if (subordinate_type == 0x22u) {
                    report_descriptor_length = subordinate_length;
                }

                cursor = (uint16_t)(cursor + 3u);
            }
        } else if (type == 0x05u &&
                   in_boot_hid_interface) {
            if (length < 7u) return false;

            uint8_t endpoint_address = raw[offset + 2u];
            uint8_t attributes = raw[offset + 3u];
            uint16_t max_packet =
                (uint16_t)(usb_read_le16(&raw[offset + 4u]) & 0x07FFu);
            uint8_t interval = raw[offset + 6u];

            bool direction_in =
                (endpoint_address & 0x80u) != 0u;
            bool interrupt_transfer =
                (attributes & 0x03u) == 0x03u;

            if (direction_in &&
                interrupt_transfer &&
                max_packet != 0u &&
                interval != 0u &&
                report_descriptor_length != 0u) {
                *out_endpoint =
                    (struct aurora_usb_hid_endpoint_descriptor){
                        .configuration_value = configuration_value,
                        .interface_number = interface_number,
                        .interface_subclass = interface_subclass,
                        .interface_protocol = interface_protocol,
                        .endpoint_address = endpoint_address,
                        .max_packet_size = max_packet,
                        .interval = interval,
                        .total_configuration_length = total_length,
                        .report_descriptor_length = report_descriptor_length
                    };

                return true;
            }
        }

        offset = (uint16_t)(offset + length);
    }

    log_line("[xhci] no HID Boot interrupt-IN endpoint found");
    return false;
}


static bool xhci_control_no_data(
    struct aurora_xhci_controller_state *state,
    uint8_t request_type,
    uint8_t request,
    uint16_t value,
    uint16_t index
) {
    if (state == NULL ||
        state->addressed_slot_id == 0u ||
        state->ep0_state != 1u ||
        state->ep0_ring_physical == 0u ||
        xhci_doorbell_base == NULL) {
        return false;
    }

    uint64_t setup_packet =
        (uint64_t)request_type |
        ((uint64_t)request << 8u) |
        ((uint64_t)value << 16u) |
        ((uint64_t)index << 32u);

    struct xhci_trb setup = {
        .parameter = setup_packet,
        .status = 8u,
        .control =
            (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_IDT |
            XHCI_TRB_CHAIN
    };

    struct xhci_trb status = {
        .parameter = 0u,
        .status = 0u,
        .control =
            (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_DIR_IN |
            XHCI_TRB_IOC
    };

    uint64_t ignored = 0u;
    uint64_t completion_trb = 0u;

    if (!xhci_ep0_push_trb(state, &setup, &ignored) ||
        !xhci_ep0_push_trb(
            state,
            &status,
            &completion_trb)) {
        return false;
    }

    __asm__ volatile ("" ::: "memory");

    xhci_mmio_write32(
        xhci_doorbell_base,
        (uint32_t)state->addressed_slot_id * 4u,
        1u
    );

    return xhci_wait_transfer_completion(
        state,
        state->addressed_slot_id,
        completion_trb
    );
}

bool xhci_set_configuration_and_boot_protocol(
    struct aurora_xhci_controller_state *state,
    const struct aurora_usb_hid_endpoint_descriptor *endpoint
) {
    if (state == NULL ||
        endpoint == NULL ||
        endpoint->configuration_value == 0u ||
        endpoint->interface_subclass != 0x01u ||
        (endpoint->interface_protocol != 0x01u &&
         endpoint->interface_protocol != 0x02u)) {
        return false;
    }

    /*
     * Standard SET_CONFIGURATION: host-to-device, device recipient.
     */
    if (!xhci_control_no_data(
            state,
            0x00u,
            0x09u,
            endpoint->configuration_value,
            0u)) {
        log_line("[xhci] SET_CONFIGURATION failed");
        return false;
    }

    /*
     * HID SET_PROTOCOL(0): force boot protocol so the already implemented
     * fixed-size keyboard/mouse decoders match the live wire format.
     */
    if (!xhci_control_no_data(
            state,
            0x21u,
            0x0Bu,
            0u,
            endpoint->interface_number)) {
        log_line("[xhci] HID SET_PROTOCOL(Boot) failed");
        return false;
    }

    return true;
}


static uint8_t xhci_endpoint_id_from_address(uint8_t endpoint_address) {
    uint8_t number = endpoint_address & 0x0Fu;
    bool direction_in = (endpoint_address & 0x80u) != 0u;

    if (number == 0u) return 1u;

    return (uint8_t)(number * 2u + (direction_in ? 1u : 0u));
}

static uint8_t xhci_encode_interrupt_interval(
    uint8_t speed_id,
    uint8_t usb_interval
) {
    if (usb_interval == 0u) return 0u;

    if (speed_id == 3u || speed_id == 4u) {
        return usb_interval > 16u ? 15u : (uint8_t)(usb_interval - 1u);
    }

    uint8_t exponent = 0u;
    uint8_t value = 1u;

    while (value < usb_interval && exponent < 7u) {
        value <<= 1u;
        ++exponent;
    }

    return (uint8_t)(exponent + 3u);
}

bool xhci_configure_hid_interrupt_endpoint(
    struct aurora_xhci_controller_state *state,
    const struct aurora_usb_hid_endpoint_descriptor *endpoint,
    uint8_t speed_id
) {
    if (state == NULL ||
        endpoint == NULL ||
        !state->dma_ready ||
        !state->running ||
        state->addressed_slot_id == 0u ||
        state->device_context_physical == 0u ||
        state->input_context_physical == 0u ||
        endpoint->max_packet_size == 0u ||
        endpoint->max_packet_size > 1024u ||
        endpoint->interval == 0u ||
        (endpoint->endpoint_address & 0x80u) == 0u ||
        state->hid_ring_physical != 0u) {
        return false;
    }

    uint8_t endpoint_id =
        xhci_endpoint_id_from_address(endpoint->endpoint_address);

    if (endpoint_id <= 1u || endpoint_id >= 32u) {
        return false;
    }

    uint8_t interval =
        xhci_encode_interrupt_interval(speed_id, endpoint->interval);

    if (interval == 0u || interval > 15u) {
        return false;
    }

    uint64_t hid_ring = pmm_alloc_page();
    if (hid_ring == 0u) return false;

    struct xhci_trb *ring =
        (struct xhci_trb *)pmm_phys_to_virt(hid_ring);

    ring[XHCI_RING_TRB_COUNT - 1u] = (struct xhci_trb){
        .parameter = hid_ring,
        .status = 0u,
        .control =
            (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TOGGLE_CYCLE |
            XHCI_TRB_CYCLE
    };

    uint8_t *input_bytes =
        (uint8_t *)pmm_phys_to_virt(
            state->input_context_physical
        );
    for (uint32_t i = 0u; i < XHCI_PAGE_SIZE; ++i) {
        input_bytes[i] = 0u;
    }

    uint32_t *control = xhci_context_ptr(
        state->input_context_physical,
        state->context_size,
        XHCI_INPUT_CONTROL_CONTEXT
    );
    uint32_t *input_slot = xhci_context_ptr(
        state->input_context_physical,
        state->context_size,
        XHCI_INPUT_SLOT_CONTEXT
    );
    uint32_t *device_slot = xhci_context_ptr(
        state->device_context_physical,
        state->context_size,
        XHCI_CONTEXT_SLOT_INDEX
    );
    uint32_t *input_ep = xhci_context_ptr(
        state->input_context_physical,
        state->context_size,
        (uint32_t)endpoint_id + 1u
    );

    if (control == NULL ||
        input_slot == NULL ||
        device_slot == NULL ||
        input_ep == NULL) {
        pmm_free_page(hid_ring);
        return false;
    }

    uint32_t context_dwords =
        (uint32_t)state->context_size / sizeof(uint32_t);

    for (uint32_t i = 0u; i < context_dwords; ++i) {
        input_slot[i] = device_slot[i];
    }

    input_slot[0] &= ~(0x1Fu << XHCI_SLOT_CONTEXT_ENTRIES_SHIFT);
    input_slot[0] |=
        ((uint32_t)endpoint_id << XHCI_SLOT_CONTEXT_ENTRIES_SHIFT);

    control[1] =
        (1u << 0) |
        (1u << endpoint_id);

    input_ep[0] =
        ((uint32_t)interval << 16u);

    input_ep[1] =
        ((uint32_t)XHCI_EP0_ERROR_COUNT << XHCI_EP_CONTEXT_CERR_SHIFT) |
        ((uint32_t)XHCI_EP_TYPE_INTERRUPT_IN << XHCI_EP_CONTEXT_TYPE_SHIFT) |
        ((uint32_t)endpoint->max_packet_size << XHCI_EP_CONTEXT_MAX_PACKET_SHIFT);

    input_ep[2] = (uint32_t)(hid_ring | 1u);
    input_ep[3] = (uint32_t)(hid_ring >> 32u);
    input_ep[4] =
        (uint32_t)endpoint->max_packet_size |
        ((uint32_t)endpoint->max_packet_size << 16u);

    struct xhci_trb *command_ring =
        (struct xhci_trb *)pmm_phys_to_virt(
            state->command_ring_physical
        );

    if (state->command_enqueue >= XHCI_RING_TRB_COUNT - 1u) {
        pmm_free_page(hid_ring);
        return false;
    }

    uint16_t command_index = state->command_enqueue;
    uint32_t cycle =
        state->command_cycle ? XHCI_TRB_CYCLE : 0u;

    command_ring[command_index] = (struct xhci_trb){
        .parameter = state->input_context_physical,
        .status = 0u,
        .control =
            (XHCI_TRB_TYPE_CONFIGURE_ENDPOINT << XHCI_TRB_TYPE_SHIFT) |
            ((uint32_t)state->addressed_slot_id << 24u) |
            cycle
    };

    __asm__ volatile ("" ::: "memory");

    uint64_t command_physical =
        state->command_ring_physical +
        (uint64_t)command_index * sizeof(struct xhci_trb);

    ++state->command_enqueue;

    if (state->command_enqueue ==
        XHCI_RING_TRB_COUNT - 1u) {
        struct xhci_trb *link =
            &command_ring[XHCI_RING_TRB_COUNT - 1u];

        uint32_t link_cycle =
            state->command_cycle ? XHCI_TRB_CYCLE : 0u;

        link->control =
            (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TOGGLE_CYCLE |
            link_cycle;

        __asm__ volatile ("" ::: "memory");

        state->command_enqueue = 0u;
        state->command_cycle = !state->command_cycle;
    }

    xhci_mmio_write32(
        xhci_doorbell_base,
        0u,
        0u
    );

    uint8_t completion_slot = 0u;
    if (!xhci_wait_command_completion(
            state,
            command_physical,
            &completion_slot) ||
        completion_slot != state->addressed_slot_id) {
        pmm_free_page(hid_ring);
        return false;
    }

    uint32_t *device_ep = xhci_context_ptr(
        state->device_context_physical,
        state->context_size,
        endpoint_id
    );

    if (device_ep == NULL ||
        (device_ep[0] & XHCI_EP_CONTEXT_STATE_MASK) != 1u) {
        log_line("[xhci] HID interrupt endpoint not Running after Configure Endpoint");
        pmm_free_page(hid_ring);
        return false;
    }

    state->hid_ring_physical = hid_ring;
    state->hid_endpoint_id = endpoint_id;
    state->hid_enqueue = 0u;
    state->hid_cycle = true;
    state->hid_endpoint_running = true;
    return true;
}


static bool xhci_wait_hid_transfer_completion(
    struct aurora_xhci_controller_state *state,
    uint64_t trb_physical,
    uint16_t requested_length,
    uint16_t *out_transferred
) {
    if (out_transferred != NULL) *out_transferred = 0u;

    if (state == NULL ||
        out_transferred == NULL ||
        trb_physical == 0u ||
        requested_length == 0u ||
        state->hid_endpoint_id <= 1u ||
        state->event_ring_physical == 0u ||
        xhci_runtime_base == NULL) {
        return false;
    }

    struct xhci_trb *events =
        (struct xhci_trb *)pmm_phys_to_virt(
            state->event_ring_physical
        );

    volatile uint8_t *interrupter0 =
        xhci_runtime_base + XHCI_RUNTIME_INTERRUPTER0;

    for (uint32_t spin = 0u;
         spin < XHCI_EVENT_SPIN_LIMIT * 20u;
         ++spin) {
        struct xhci_trb event =
            events[state->event_dequeue];

        bool cycle =
            (event.control & XHCI_TRB_CYCLE) != 0u;

        if (cycle != state->event_cycle) {
            __asm__ volatile ("pause");
            continue;
        }

        uint32_t type =
            (event.control >> XHCI_TRB_TYPE_SHIFT) & 0x3Fu;

        if (type != XHCI_TRB_TYPE_TRANSFER_EVENT &&
            type != XHCI_TRB_TYPE_PORT_STATUS_CHANGE) {
            log_write("[xhci] unexpected event while waiting HID report type=");
            log_u64(type);
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

        xhci_mmio_write64(
            interrupter0,
            XHCI_INTR_ERDP,
            dequeue_physical | (1ull << 3)
        );

        if (type == XHCI_TRB_TYPE_PORT_STATUS_CHANGE) {
            continue;
        }

        uint8_t completion_code =
            (uint8_t)(event.status >> 24u);
        uint32_t residual = event.status & 0x00FFFFFFu;
        uint8_t event_slot =
            (uint8_t)(event.control >> 24u);
        uint8_t endpoint_id =
            (uint8_t)((event.control >> 16u) & 0x1Fu);

        if ((completion_code != XHCI_COMPLETION_SUCCESS &&
             completion_code != 13u) ||
            event_slot != state->addressed_slot_id ||
            endpoint_id != state->hid_endpoint_id ||
            event.parameter != trb_physical ||
            residual > requested_length) {
            log_write("[xhci] bad HID transfer completion code=");
            log_u64(completion_code);
            log_write(" slot=");
            log_u64(event_slot);
            log_write(" ep=");
            log_u64(endpoint_id);
            log_write(" residual=");
            log_u64(residual);
            log_line("");
            return false;
        }

        *out_transferred =
            (uint16_t)(requested_length - residual);
        return true;
    }

    log_line("[xhci] HID interrupt-IN transfer timeout");
    return false;
}

bool xhci_receive_hid_interrupt_report(
    struct aurora_xhci_controller_state *state,
    uint8_t *report,
    uint16_t report_size
) {
    if (state == NULL ||
        report == NULL ||
        report_size == 0u ||
        report_size > XHCI_PAGE_SIZE ||
        !state->hid_endpoint_running ||
        state->hid_ring_physical == 0u ||
        state->hid_endpoint_id <= 1u ||
        state->hid_enqueue >= XHCI_RING_TRB_COUNT - 1u ||
        xhci_doorbell_base == NULL) {
        return false;
    }

    uint64_t buffer_page = pmm_alloc_page();
    if (buffer_page == 0u) return false;

    struct xhci_trb *ring =
        (struct xhci_trb *)pmm_phys_to_virt(
            state->hid_ring_physical
        );

    uint16_t index = state->hid_enqueue;
    uint32_t cycle =
        state->hid_cycle ? XHCI_TRB_CYCLE : 0u;

    ring[index] = (struct xhci_trb){
        .parameter = buffer_page,
        .status = (uint32_t)report_size,
        .control =
            (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_IOC |
            cycle
    };

    __asm__ volatile ("" ::: "memory");

    uint64_t trb_physical =
        state->hid_ring_physical +
        (uint64_t)index * sizeof(struct xhci_trb);

    ++state->hid_enqueue;

    if (state->hid_enqueue ==
        XHCI_RING_TRB_COUNT - 1u) {
        struct xhci_trb *link =
            &ring[XHCI_RING_TRB_COUNT - 1u];

        uint32_t link_cycle =
            state->hid_cycle ? XHCI_TRB_CYCLE : 0u;

        link->control =
            (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
            XHCI_TRB_TOGGLE_CYCLE |
            link_cycle;

        __asm__ volatile ("" ::: "memory");

        state->hid_enqueue = 0u;
        state->hid_cycle = !state->hid_cycle;
    }

    xhci_mmio_write32(
        xhci_doorbell_base,
        (uint32_t)state->addressed_slot_id * 4u,
        state->hid_endpoint_id
    );

    uint16_t transferred = 0u;
    if (!xhci_wait_hid_transfer_completion(
            state,
            trb_physical,
            report_size,
            &transferred) ||
        transferred != report_size) {
        pmm_free_page(buffer_page);
        return false;
    }

    const uint8_t *source =
        (const uint8_t *)pmm_phys_to_virt(buffer_page);

    for (uint16_t i = 0u; i < report_size; ++i) {
        report[i] = source[i];
    }

    pmm_free_page(buffer_page);
    return true;
}


bool xhci_release_addressed_device(
    struct aurora_xhci_controller_state *state,
    uint8_t slot_id
) {
    if (state == NULL ||
        slot_id == 0u ||
        state->addressed_slot_id != slot_id ||
        !state->addressed_slot_disabled ||
        state->dcbaa_physical == 0u) {
        return false;
    }

    uint64_t *dcbaa =
        (uint64_t *)pmm_phys_to_virt(
            state->dcbaa_physical
        );

    /*
     * Remove controller visibility before freeing any backing memory.
     */
    dcbaa[slot_id] = 0u;
    __asm__ volatile ("" ::: "memory");

    if (state->hid_ring_physical != 0u) {
        pmm_free_page(state->hid_ring_physical);
    }
    if (state->ep0_ring_physical != 0u) {
        pmm_free_page(state->ep0_ring_physical);
    }
    if (state->input_context_physical != 0u) {
        pmm_free_page(state->input_context_physical);
    }
    if (state->device_context_physical != 0u) {
        pmm_free_page(state->device_context_physical);
    }

    state->device_context_physical = 0u;
    state->input_context_physical = 0u;
    state->ep0_ring_physical = 0u;
    state->addressed_slot_id = 0u;
    state->usb_device_address = 0u;
    state->ep0_state = 0u;
    state->ep0_enqueue = 0u;
    state->ep0_cycle = true;

    state->hid_ring_physical = 0u;
    state->hid_endpoint_id = 0u;
    state->hid_enqueue = 0u;
    state->hid_cycle = true;
    state->hid_endpoint_running = false;
    state->addressed_slot_disabled = false;

    return true;
}


bool xhci_enumerate_boot_hid_after_port(
    const struct aurora_xhci_probe_result *probe,
    struct aurora_xhci_controller_state *state,
    uint8_t after_port_id,
    struct aurora_xhci_hid_device *out_device
) {
    if (probe == NULL ||
        state == NULL ||
        out_device == NULL ||
        state->addressed_slot_id != 0u ||
        state->device_context_physical != 0u ||
        state->input_context_physical != 0u ||
        state->ep0_ring_physical != 0u ||
        state->hid_ring_physical != 0u) {
        return false;
    }

    *out_device = (struct aurora_xhci_hid_device){0};

    uint8_t port_id = 0u;
    uint8_t speed_id = 0u;

    if (!xhci_reset_connected_port_after(
            probe,
            after_port_id,
            &port_id,
            &speed_id)) {
        return false;
    }

    uint64_t enable_slot_trb = 0u;
    uint8_t slot_id = 0u;

    if (!xhci_submit_enable_slot(
            state,
            &enable_slot_trb) ||
        !xhci_wait_command_completion(
            state,
            enable_slot_trb,
            &slot_id) ||
        slot_id == 0u) {
        return false;
    }

    bool slot_enabled = true;

    if (!xhci_prepare_address_device(
            state,
            slot_id,
            port_id,
            speed_id)) {
        (void)xhci_disable_slot(state, slot_id);
        return false;
    }

    uint64_t address_device_trb = 0u;
    uint8_t completion_slot = 0u;

    if (!xhci_submit_address_device(
            state,
            slot_id,
            &address_device_trb) ||
        !xhci_wait_command_completion(
            state,
            address_device_trb,
            &completion_slot) ||
        completion_slot != slot_id ||
        !xhci_validate_addressed_device(
            state,
            slot_id)) {
        if (slot_enabled && xhci_disable_slot(state, slot_id)) {
            (void)xhci_release_addressed_device(state, slot_id);
        }
        return false;
    }

    struct aurora_usb_device_descriptor device = {0};
    struct aurora_usb_hid_endpoint_descriptor endpoint = {0};

    if (!xhci_get_device_descriptor(
            state,
            &device) ||
        !xhci_find_boot_hid_endpoint(
            state,
            &endpoint) ||
        !xhci_set_configuration_and_boot_protocol(
            state,
            &endpoint) ||
        !xhci_configure_hid_interrupt_endpoint(
            state,
            &endpoint,
            speed_id)) {
        if (xhci_disable_slot(state, slot_id)) {
            (void)xhci_release_addressed_device(state, slot_id);
        }
        return false;
    }

    *out_device = (struct aurora_xhci_hid_device){
        .port_id = port_id,
        .speed_id = speed_id,
        .slot_id = slot_id,
        .device = device,
        .endpoint = endpoint
    };

    return true;
}


#define AURORA_USB_HID_REPORT_DESCRIPTOR_MAX 1024u

static uint32_t hid_item_unsigned(
    const uint8_t *data,
    uint8_t size
) {
    uint32_t value = 0u;

    for (uint8_t i = 0u; i < size; ++i) {
        value |= (uint32_t)data[i] << (8u * i);
    }

    return value;
}

bool xhci_get_hid_report_layout(
    struct aurora_xhci_controller_state *state,
    const struct aurora_usb_hid_endpoint_descriptor *endpoint,
    struct aurora_usb_hid_mouse_report_layout *out_layout
) {
    if (state == NULL ||
        endpoint == NULL ||
        out_layout == NULL ||
        endpoint->report_descriptor_length == 0u ||
        endpoint->report_descriptor_length >
            AURORA_USB_HID_REPORT_DESCRIPTOR_MAX) {
        return false;
    }

    static uint8_t raw[AURORA_USB_HID_REPORT_DESCRIPTOR_MAX];

    uint16_t length = endpoint->report_descriptor_length;

    if (!xhci_control_in(
            state,
            0x81u,
            0x06u,
            0x2200u,
            endpoint->interface_number,
            raw,
            length)) {
        log_line("[xhci] GET_DESCRIPTOR(HID Report) failed");
        return false;
    }

    struct aurora_usb_hid_mouse_report_layout layout = {
        .descriptor_length = length,
        .x_bit_offset = UINT16_MAX,
        .y_bit_offset = UINT16_MAX,
        .wheel_bit_offset = UINT16_MAX
    };

    uint16_t bit_offset = 0u;
    uint16_t usage_page = 0u;
    uint8_t report_size = 0u;
    uint8_t report_count = 0u;
    uint8_t report_id = 0u;

    uint16_t usage_min = UINT16_MAX;
    uint16_t usage_max = UINT16_MAX;
    uint16_t usages[16] = {0};
    uint8_t usage_count = 0u;

    uint16_t cursor = 0u;

    while (cursor < length) {
        uint8_t prefix = raw[cursor++];

        if (prefix == 0xFEu) {
            log_line("[xhci] long HID report items unsupported");
            return false;
        }

        uint8_t size_code = prefix & 0x03u;
        uint8_t item_size =
            size_code == 3u ? 4u : size_code;

        if ((uint16_t)(length - cursor) < item_size) {
            return false;
        }

        uint8_t type = (uint8_t)((prefix >> 2u) & 0x03u);
        uint8_t tag = (uint8_t)((prefix >> 4u) & 0x0Fu);
        uint32_t value =
            hid_item_unsigned(&raw[cursor], item_size);

        cursor = (uint16_t)(cursor + item_size);

        if (type == 1u) {
            if (tag == 0x0u) {
                usage_page = (uint16_t)value;
            } else if (tag == 0x7u) {
                if (value == 0u || value > 32u) return false;
                report_size = (uint8_t)value;
            } else if (tag == 0x8u) {
                if (value == 0u || value > 255u) return false;
                report_id = (uint8_t)value;
                if (layout.report_id != 0u &&
                    layout.report_id != report_id) {
                    log_line("[xhci] multiple HID report IDs unsupported");
                    return false;
                }
                layout.report_id = report_id;
                bit_offset = 8u;
            } else if (tag == 0x9u) {
                if (value == 0u || value > 255u) return false;
                report_count = (uint8_t)value;
            }
            continue;
        }

        if (type == 2u) {
            if (tag == 0x0u) {
                if (usage_count <
                    (uint8_t)(sizeof(usages) / sizeof(usages[0]))) {
                    usages[usage_count++] = (uint16_t)value;
                }
            } else if (tag == 0x1u) {
                usage_min = (uint16_t)value;
            } else if (tag == 0x2u) {
                usage_max = (uint16_t)value;
            }
            continue;
        }

        if (type == 0u && tag == 0x8u) {
            if (report_size == 0u || report_count == 0u) {
                return false;
            }

            uint32_t field_bits =
                (uint32_t)report_size *
                (uint32_t)report_count;

            if ((uint32_t)bit_offset + field_bits >
                UINT16_MAX) {
                return false;
            }

            bool constant = (value & 0x01u) != 0u;

            if (!constant && usage_page == 0x09u) {
                uint16_t declared_buttons = 0u;

                if (usage_min != UINT16_MAX &&
                    usage_max != UINT16_MAX &&
                    usage_max >= usage_min) {
                    declared_buttons =
                        (uint16_t)(usage_max - usage_min + 1u);
                } else {
                    declared_buttons = report_count;
                }

                if (report_size != 1u ||
                    declared_buttons == 0u ||
                    declared_buttons > report_count ||
                    declared_buttons > 8u) {
                    return false;
                }

                layout.button_count =
                    (uint8_t)declared_buttons;
                layout.button_bit_offset = bit_offset;
            }

            if (!constant && usage_page == 0x01u) {
                for (uint8_t i = 0u;
                     i < report_count;
                     ++i) {
                    uint16_t usage = 0u;

                    if (i < usage_count) {
                        usage = usages[i];
                    } else if (usage_min != UINT16_MAX &&
                               usage_max != UINT16_MAX &&
                               usage_min + i <= usage_max) {
                        usage = (uint16_t)(usage_min + i);
                    }

                    uint16_t field_offset =
                        (uint16_t)(
                            bit_offset +
                            (uint16_t)i * report_size
                        );

                    if (usage == 0x30u) {
                        layout.has_x = true;
                        layout.x_bit_offset = field_offset;
                        layout.x_bit_size = report_size;
                    } else if (usage == 0x31u) {
                        layout.has_y = true;
                        layout.y_bit_offset = field_offset;
                        layout.y_bit_size = report_size;
                    } else if (usage == 0x38u) {
                        layout.has_wheel = true;
                        layout.wheel_bit_offset = field_offset;
                        layout.wheel_bit_size = report_size;
                    }
                }
            }

            bit_offset =
                (uint16_t)(bit_offset + field_bits);

            usage_min = UINT16_MAX;
            usage_max = UINT16_MAX;
            usage_count = 0u;
        } else if (type == 0u) {
            usage_min = UINT16_MAX;
            usage_max = UINT16_MAX;
            usage_count = 0u;
        }
    }

    layout.input_report_bits = bit_offset;

    if (layout.button_count == 0u ||
        !layout.has_x ||
        !layout.has_y ||
        layout.x_bit_size == 0u ||
        layout.y_bit_size == 0u ||
        layout.input_report_bits == 0u) {
        return false;
    }

    *out_layout = layout;
    return true;
}


bool xhci_set_hid_report_protocol(
    struct aurora_xhci_controller_state *state,
    const struct aurora_usb_hid_endpoint_descriptor *endpoint
) {
    if (state == NULL ||
        endpoint == NULL ||
        endpoint->interface_subclass != 0x01u ||
        (endpoint->interface_protocol != 0x01u &&
         endpoint->interface_protocol != 0x02u)) {
        return false;
    }

    /*
     * HID SET_PROTOCOL(1): select Report Protocol.  The descriptor-derived
     * layout must be used by higher layers before they decode non-Boot
     * reports, especially devices exposing more than the fixed 3 buttons.
     */
    if (!xhci_control_no_data(
            state,
            0x21u,
            0x0Bu,
            1u,
            endpoint->interface_number)) {
        log_line("[xhci] HID SET_PROTOCOL(Report) failed");
        return false;
    }

    return true;
}
