#ifndef AURORA_DISPLAY_CONTROLLER_H
#define AURORA_DISPLAY_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/display_output.h>

struct aurora_display_scanout {
    uint64_t physical_address;
    uint64_t byte_length;
    uint64_t pitch;
    struct aurora_display_pixel_format format;
};

typedef bool (*aurora_display_modeset_fn)(
    void *context,
    const struct aurora_display_mode *mode
);

typedef bool (*aurora_display_scanout_fn)(
    void *context,
    const struct aurora_display_scanout *scanout
);

typedef bool (*aurora_display_enable_fn)(
    void *context,
    bool enabled
);

struct aurora_display_controller_ops {
    aurora_display_modeset_fn set_mode;
    aurora_display_scanout_fn set_scanout;
    aurora_display_enable_fn set_enabled;
};

struct aurora_display_controller {
    const struct aurora_display_controller_ops *ops;
    void *context;
    struct aurora_display_mode programmed_mode;
    bool mode_programmed;
    bool scanout_programmed;
    bool enabled;
};

bool display_controller_modeset(
    struct aurora_display_controller *controller,
    const struct aurora_display_mode *mode
);

bool display_controller_set_scanout(
    struct aurora_display_controller *controller,
    const struct aurora_display_scanout *scanout
);

bool display_controller_enable(
    struct aurora_display_controller *controller
);

bool display_controller_disable(
    struct aurora_display_controller *controller
);

bool display_controller_selftest(void);

#endif
