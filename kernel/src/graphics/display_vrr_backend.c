#include <stddef.h>
#include <stdint.h>

#include <aurora/display_vrr_backend.h>

bool display_vrr_program_output(
    struct aurora_display_output *output,
    const struct aurora_vrr_backend *backend
) {
    if (output == NULL ||
        backend == NULL ||
        backend->program == NULL ||
        !output->connected ||
        !output->vrr_policy.enabled ||
        !display_vrr_range_valid(
            output->vrr_policy.min_millihz,
            output->vrr_policy.max_millihz) ||
        (output->capabilities.flags & AURORA_DISPLAY_CAP_VRR) == 0u) {
        return false;
    }

    return backend->program(
        backend->context,
        output->vrr_policy.min_millihz,
        output->vrr_policy.max_millihz,
        output->vrr_policy.preferred_millihz
    );
}

bool display_vrr_disable_output(
    struct aurora_display_output *output,
    const struct aurora_vrr_backend *backend
) {
    if (output == NULL ||
        backend == NULL ||
        backend->disable == NULL ||
        !output->connected) {
        return false;
    }

    if (!backend->disable(backend->context)) {
        return false;
    }

    return display_output_set_vrr_policy(
        output,
        false,
        0u,
        0u,
        0u
    );
}

struct vrr_selftest_state {
    bool programmed;
    bool disabled;
    uint32_t min_millihz;
    uint32_t max_millihz;
    uint32_t preferred_millihz;
};

static bool selftest_program(
    void *context,
    uint32_t min_millihz,
    uint32_t max_millihz,
    uint32_t preferred_millihz
) {
    struct vrr_selftest_state *state =
        (struct vrr_selftest_state *)context;

    if (state == NULL) return false;

    state->programmed = true;
    state->min_millihz = min_millihz;
    state->max_millihz = max_millihz;
    state->preferred_millihz = preferred_millihz;
    return true;
}

static bool selftest_disable(void *context) {
    struct vrr_selftest_state *state =
        (struct vrr_selftest_state *)context;

    if (state == NULL) return false;
    state->disabled = true;
    return true;
}

bool display_vrr_backend_selftest(void) {
    struct aurora_display_output output;

    if (!display_output_init(
            &output,
            UINT64_C(100),
            AURORA_DISPLAY_BACKEND_BOOT_FRAMEBUFFER,
            false)) {
        return false;
    }

    struct aurora_display_capabilities caps = {
        .flags = AURORA_DISPLAY_CAP_SDR |
            AURORA_DISPLAY_CAP_VRR,
        .min_bits_per_component = 8u,
        .max_bits_per_component = 10u,
        .vrr_min_millihz = 48000u,
        .vrr_max_millihz = 144000u
    };

    if (!display_output_set_capabilities(&output, &caps) ||
        !display_output_set_vrr_policy(
            &output,
            true,
            48000u,
            120000u,
            60000u)) {
        return false;
    }

    struct vrr_selftest_state state = {0};
    struct aurora_vrr_backend backend = {
        .program = selftest_program,
        .disable = selftest_disable,
        .context = &state
    };

    if (!display_vrr_program_output(&output, &backend) ||
        !state.programmed ||
        state.min_millihz != 48000u ||
        state.max_millihz != 120000u ||
        state.preferred_millihz != 60000u ||
        !display_vrr_disable_output(&output, &backend) ||
        !state.disabled ||
        output.vrr_policy.enabled) {
        return false;
    }

    return true;
}
