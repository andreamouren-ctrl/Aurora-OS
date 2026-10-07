#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/pci.h>
#include <aurora/qemu_std_vga.h>

#define BOCHS_DISPI_IOPORT_INDEX 0x01CEu
#define BOCHS_DISPI_IOPORT_DATA  0x01CFu

#define BOCHS_DISPI_INDEX_ID          0x00u
#define BOCHS_DISPI_INDEX_XRES        0x01u
#define BOCHS_DISPI_INDEX_YRES        0x02u
#define BOCHS_DISPI_INDEX_BPP         0x03u
#define BOCHS_DISPI_INDEX_ENABLE      0x04u
#define BOCHS_DISPI_INDEX_VIRT_WIDTH  0x06u

#define BOCHS_DISPI_DISABLED      0x00u
#define BOCHS_DISPI_ENABLED       0x01u
#define BOCHS_DISPI_LFB_ENABLED   0x40u

struct qemu_std_vga_context {
    uint64_t lfb_physical;
    uint64_t programmed_pitch;
    uint16_t width;
    uint16_t height;
    uint16_t bpp;
};

static struct qemu_std_vga_context qemu_context;

static void dispi_write(uint16_t index, uint16_t value) {
    arch_out16(BOCHS_DISPI_IOPORT_INDEX, index);
    arch_out16(BOCHS_DISPI_IOPORT_DATA, value);
}

static uint16_t dispi_read(uint16_t index) {
    arch_out16(BOCHS_DISPI_IOPORT_INDEX, index);
    return arch_in16(BOCHS_DISPI_IOPORT_DATA);
}

static bool qemu_match(
    const struct aurora_pci_device *device
) {
    return device != NULL &&
        device->class_code == 0x03u &&
        device->vendor_id == AURORA_QEMU_VGA_VENDOR_ID &&
        device->device_id == AURORA_QEMU_VGA_DEVICE_ID;
}

static bool qemu_set_mode(
    void *context,
    const struct aurora_display_mode *mode
) {
    struct qemu_std_vga_context *state =
        (struct qemu_std_vga_context *)context;

    if (state == NULL ||
        mode == NULL ||
        !display_mode_valid(mode) ||
        mode->width > UINT16_MAX ||
        mode->height > UINT16_MAX ||
        (mode->format.bits_per_pixel != 32u &&
         mode->format.bits_per_pixel != 24u &&
         mode->format.bits_per_pixel != 16u)) {
        return false;
    }

    dispi_write(BOCHS_DISPI_INDEX_ENABLE, BOCHS_DISPI_DISABLED);
    dispi_write(BOCHS_DISPI_INDEX_XRES, (uint16_t)mode->width);
    dispi_write(BOCHS_DISPI_INDEX_YRES, (uint16_t)mode->height);
    dispi_write(BOCHS_DISPI_INDEX_BPP, mode->format.bits_per_pixel);
    dispi_write(BOCHS_DISPI_INDEX_VIRT_WIDTH, (uint16_t)mode->width);

    state->width = (uint16_t)mode->width;
    state->height = (uint16_t)mode->height;
    state->bpp = mode->format.bits_per_pixel;
    state->programmed_pitch = mode->pitch;
    return true;
}

static bool qemu_set_scanout(
    void *context,
    const struct aurora_display_scanout *scanout
) {
    struct qemu_std_vga_context *state =
        (struct qemu_std_vga_context *)context;

    if (state == NULL ||
        scanout == NULL ||
        state->lfb_physical == 0u ||
        scanout->physical_address != state->lfb_physical ||
        scanout->pitch != state->programmed_pitch) {
        return false;
    }

    return true;
}

static bool qemu_set_enabled(
    void *context,
    bool enabled
) {
    struct qemu_std_vga_context *state =
        (struct qemu_std_vga_context *)context;

    if (state == NULL || state->lfb_physical == 0u) {
        return false;
    }

    dispi_write(
        BOCHS_DISPI_INDEX_ENABLE,
        enabled
            ? (BOCHS_DISPI_ENABLED | BOCHS_DISPI_LFB_ENABLED)
            : BOCHS_DISPI_DISABLED
    );

    return true;
}

static bool qemu_bind(
    const struct aurora_pci_device *device,
    struct aurora_gpu_display_device *out_device
) {
    if (!qemu_match(device) || out_device == NULL) {
        return false;
    }

    uint64_t lfb = 0u;
    if (!pci_read_bar64(device, 0u, &lfb) || lfb == 0u) {
        return false;
    }

    static const struct aurora_display_controller_ops controller_ops = {
        .set_mode = qemu_set_mode,
        .set_scanout = qemu_set_scanout,
        .set_enabled = qemu_set_enabled
    };

    qemu_context.lfb_physical = lfb;
    qemu_context.programmed_pitch = 0u;
    qemu_context.width = 0u;
    qemu_context.height = 0u;
    qemu_context.bpp = 0u;

    out_device->controller.ops = &controller_ops;
    out_device->controller.context = &qemu_context;
    out_device->phy.ops = NULL;
    out_device->phy.context = NULL;
    out_device->bound = false;
    return true;
}

static const struct aurora_gpu_display_driver driver = {
    .name = "qemu-std-vga-bochs-vbe",
    .match = qemu_match,
    .bind = qemu_bind
};

const struct aurora_gpu_display_driver *qemu_std_vga_driver(void) {
    return &driver;
}

bool qemu_std_vga_bound_info(
    const struct aurora_gpu_display_device *device,
    uint64_t *out_lfb_physical
) {
    if (device == NULL ||
        out_lfb_physical == NULL ||
        !device->bound ||
        device->driver_name != driver.name ||
        device->controller.context != &qemu_context ||
        qemu_context.lfb_physical == 0u) {
        return false;
    }

    *out_lfb_physical = qemu_context.lfb_physical;
    return true;
}

bool qemu_std_vga_selftest(void) {
    struct aurora_pci_device mismatch = {
        .vendor_id = AURORA_QEMU_VGA_VENDOR_ID,
        .device_id = UINT16_C(0xFFFF),
        .class_code = 0x03u
    };

    if (qemu_match(&mismatch)) return false;

    struct aurora_pci_device match = {
        .vendor_id = AURORA_QEMU_VGA_VENDOR_ID,
        .device_id = AURORA_QEMU_VGA_DEVICE_ID,
        .class_code = 0x03u
    };

    return qemu_match(&match) &&
        qemu_std_vga_driver() == &driver &&
        dispi_read(BOCHS_DISPI_INDEX_ID) != UINT16_C(0xFFFF);
}
