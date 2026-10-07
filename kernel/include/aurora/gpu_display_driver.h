#ifndef AURORA_GPU_DISPLAY_DRIVER_H
#define AURORA_GPU_DISPLAY_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/display_controller.h>
#include <aurora/display_phy.h>
#include <aurora/pci.h>

#define AURORA_GPU_DISPLAY_DRIVER_MAX 8u

struct aurora_gpu_display_device {
    struct aurora_pci_device pci;
    struct aurora_display_controller controller;
    struct aurora_display_phy phy;
    const char *driver_name;
    bool bound;
};

typedef bool (*aurora_gpu_display_match_fn)(
    const struct aurora_pci_device *device
);

typedef bool (*aurora_gpu_display_bind_fn)(
    const struct aurora_pci_device *device,
    struct aurora_gpu_display_device *out_device
);

struct aurora_gpu_display_driver {
    const char *name;
    aurora_gpu_display_match_fn match;
    aurora_gpu_display_bind_fn bind;
};

void gpu_display_driver_registry_init(void);

bool gpu_display_driver_register(
    const struct aurora_gpu_display_driver *driver
);

size_t gpu_display_driver_count(void);

const struct aurora_gpu_display_driver *gpu_display_driver_at(
    size_t index
);

bool gpu_display_driver_bind(
    const struct aurora_pci_device *device,
    struct aurora_gpu_display_device *out_device
);

bool gpu_display_driver_selftest(void);

#endif
