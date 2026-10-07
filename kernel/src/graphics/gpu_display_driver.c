#include <stddef.h>
#include <stdint.h>

#include <aurora/gpu_display_driver.h>

static const struct aurora_gpu_display_driver *
    drivers[AURORA_GPU_DISPLAY_DRIVER_MAX];
static size_t driver_count;

void gpu_display_driver_registry_init(void) {
    for (size_t i = 0u;
         i < AURORA_GPU_DISPLAY_DRIVER_MAX;
         ++i) {
        drivers[i] = NULL;
    }

    driver_count = 0u;
}

bool gpu_display_driver_register(
    const struct aurora_gpu_display_driver *driver
) {
    if (driver == NULL ||
        driver->name == NULL ||
        driver->match == NULL ||
        driver->bind == NULL ||
        driver_count >= AURORA_GPU_DISPLAY_DRIVER_MAX) {
        return false;
    }

    for (size_t i = 0u; i < driver_count; ++i) {
        if (drivers[i] == driver) return true;
    }

    drivers[driver_count++] = driver;
    return true;
}

size_t gpu_display_driver_count(void) {
    return driver_count;
}

const struct aurora_gpu_display_driver *gpu_display_driver_at(
    size_t index
) {
    if (index >= driver_count) return NULL;
    return drivers[index];
}

bool gpu_display_driver_bind(
    const struct aurora_pci_device *device,
    struct aurora_gpu_display_device *out_device
) {
    if (device == NULL ||
        out_device == NULL ||
        device->class_code != 0x03u) {
        return false;
    }

    for (size_t i = 0u; i < driver_count; ++i) {
        const struct aurora_gpu_display_driver *driver =
            drivers[i];

        if (!driver->match(device)) continue;

        struct aurora_gpu_display_device candidate = {0};

        if (!driver->bind(device, &candidate) ||
            candidate.bound ||
            candidate.controller.ops == NULL) {
            continue;
        }

        candidate.pci = *device;
        candidate.driver_name = driver->name;
        candidate.bound = true;
        *out_device = candidate;
        return true;
    }

    return false;
}

static bool selftest_match(
    const struct aurora_pci_device *device
) {
    return device != NULL &&
        device->vendor_id == UINT16_C(0x1234) &&
        device->device_id == UINT16_C(0x5678);
}

static bool noop_mode(
    void *context,
    const struct aurora_display_mode *mode
) {
    (void)context;
    return mode != NULL;
}

static bool noop_scanout(
    void *context,
    const struct aurora_display_scanout *scanout
) {
    (void)context;
    return scanout != NULL;
}

static bool noop_enable(void *context, bool enabled) {
    (void)context;
    (void)enabled;
    return true;
}

static bool selftest_bind(
    const struct aurora_pci_device *device,
    struct aurora_gpu_display_device *out_device
) {
    if (device == NULL || out_device == NULL) return false;

    static const struct aurora_display_controller_ops controller_ops = {
        .set_mode = noop_mode,
        .set_scanout = noop_scanout,
        .set_enabled = noop_enable
    };

    out_device->controller.ops = &controller_ops;
    out_device->controller.context = NULL;
    out_device->phy.ops = NULL;
    out_device->phy.context = NULL;
    out_device->bound = false;
    return true;
}

bool gpu_display_driver_selftest(void) {
    gpu_display_driver_registry_init();

    static const struct aurora_gpu_display_driver driver = {
        .name = "synthetic-gpu-display",
        .match = selftest_match,
        .bind = selftest_bind
    };

    if (!gpu_display_driver_register(&driver) ||
        gpu_display_driver_count() != 1u ||
        gpu_display_driver_at(0u) != &driver) {
        return false;
    }

    struct aurora_pci_device device = {
        .vendor_id = UINT16_C(0x1234),
        .device_id = UINT16_C(0x5678),
        .class_code = 0x03u,
        .subclass = 0x00u
    };

    struct aurora_gpu_display_device bound = {0};

    if (!gpu_display_driver_bind(&device, &bound) ||
        !bound.bound ||
        bound.driver_name != driver.name ||
        bound.pci.vendor_id != device.vendor_id ||
        bound.controller.ops == NULL) {
        return false;
    }

    struct aurora_pci_device storage = device;
    storage.class_code = 0x01u;

    return !gpu_display_driver_bind(
        &storage,
        &bound
    );
}
