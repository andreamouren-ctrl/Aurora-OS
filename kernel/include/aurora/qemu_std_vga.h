#ifndef AURORA_QEMU_STD_VGA_H
#define AURORA_QEMU_STD_VGA_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/gpu_display_driver.h>

#define AURORA_QEMU_VGA_VENDOR_ID UINT16_C(0x1234)
#define AURORA_QEMU_VGA_DEVICE_ID UINT16_C(0x1111)

const struct aurora_gpu_display_driver *qemu_std_vga_driver(void);

bool qemu_std_vga_bound_info(
    const struct aurora_gpu_display_device *device,
    uint64_t *out_lfb_physical
);

bool qemu_std_vga_selftest(void);

#endif
