#ifndef AURORA_DISPLAY_VRR_BACKEND_H
#define AURORA_DISPLAY_VRR_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/display_output.h>

typedef bool (*aurora_vrr_program_fn)(
    void *context,
    uint32_t min_millihz,
    uint32_t max_millihz,
    uint32_t preferred_millihz
);

typedef bool (*aurora_vrr_disable_fn)(
    void *context
);

struct aurora_vrr_backend {
    aurora_vrr_program_fn program;
    aurora_vrr_disable_fn disable;
    void *context;
};

bool display_vrr_program_output(
    struct aurora_display_output *output,
    const struct aurora_vrr_backend *backend
);

bool display_vrr_disable_output(
    struct aurora_display_output *output,
    const struct aurora_vrr_backend *backend
);

bool display_vrr_backend_selftest(void);

#endif
