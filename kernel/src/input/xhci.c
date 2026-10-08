#include <stddef.h>
#include <stdint.h>

#include <aurora/log.h>
#include <aurora/pci.h>
#include <aurora/vmm.h>
#include <aurora/xhci.h>

#define PCI_CLASS_SERIAL_BUS        0x0Cu
#define PCI_SUBCLASS_USB            0x03u
#define PCI_PROGIF_XHCI             0x30u

#define PCI_CAP_ID_MSI              0x05u
#define PCI_CAP_ID_MSIX             0x11u

#define XHCI_MMIO_VIRTUAL           0xFFFFFFFFB0400000ull
#define XHCI_PAGE_SIZE              4096ull

#define XHCI_CAP_CAPLENGTH          0x00u
#define XHCI_CAP_HCSPARAMS1         0x04u
#define XHCI_CAP_HCSPARAMS2         0x08u
#define XHCI_CAP_HCCPARAMS1         0x10u
#define XHCI_CAP_DBOFF              0x14u
#define XHCI_CAP_RTSOFF             0x18u

static volatile uint8_t *xhci_capability_base;

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
