#include <stddef.h>
#include <stdint.h>

#include <aurora/window_policy.h>

static void clear_bytes(void *ptr, uint64_t size) {
    uint8_t *bytes = (uint8_t *)ptr;
    for (uint64_t i = 0u; i < size; ++i) bytes[i] = 0u;
}

static struct aurora_window_toplevel *find_toplevel(
    struct aurora_window_policy *policy,
    uint64_t window_id
) {
    if (policy == NULL ||
        !policy->initialized ||
        window_id == 0u) {
        return NULL;
    }

    for (uint32_t i = 0u;
         i < AURORA_WINDOW_POLICY_MAX_TOPLEVELS;
         ++i) {
        if (policy->toplevels[i].used &&
            policy->toplevels[i].window_id == window_id) {
            return &policy->toplevels[i];
        }
    }

    return NULL;
}

static const struct aurora_window_toplevel *find_toplevel_const(
    const struct aurora_window_policy *policy,
    uint64_t window_id
) {
    return find_toplevel(
        (struct aurora_window_policy *)(uintptr_t)policy,
        window_id
    );
}

static uint64_t next_nonzero(uint64_t *counter) {
    uint64_t value = (*counter)++;
    if (value == 0u) value = (*counter)++;
    if (*counter == 0u) *counter = 1u;
    return value;
}

bool window_policy_init(
    struct aurora_window_policy *policy,
    uint32_t output_width,
    uint32_t output_height
) {
    if (policy == NULL ||
        output_width == 0u ||
        output_height == 0u) {
        return false;
    }

    clear_bytes(policy, sizeof(*policy));
    policy->output_width = output_width;
    policy->output_height = output_height;
    policy->next_window_id = 1u;
    policy->next_configure_serial = 1u;
    policy->next_activation_token = 1u;
    policy->next_z = 1;
    policy->initialized = true;
    return true;
}

bool window_policy_create_toplevel(
    struct aurora_window_policy *policy,
    struct aurora_graphics_surface *surface,
    uint64_t *out_window_id
) {
    if (out_window_id != NULL) *out_window_id = 0u;

    if (policy == NULL ||
        !policy->initialized ||
        surface == NULL ||
        out_window_id == NULL ||
        surface->state == AURORA_GRAPHICS_SURFACE_FREE ||
        surface->destroy_requested) {
        return false;
    }

    for (uint32_t i = 0u;
         i < AURORA_WINDOW_POLICY_MAX_TOPLEVELS;
         ++i) {
        if (!policy->toplevels[i].used) {
            uint64_t id = next_nonzero(&policy->next_window_id);

            policy->toplevels[i] =
                (struct aurora_window_toplevel){
                    .window_id = id,
                    .surface = surface,
                    .surface_generation = surface->generation,
                    .used = true
                };

            *out_window_id = id;
            return true;
        }
    }

    return false;
}

bool window_policy_configure(
    struct aurora_window_policy *policy,
    uint64_t window_id,
    uint32_t width,
    uint32_t height,
    uint32_t state_flags,
    uint64_t *out_serial
) {
    if (out_serial != NULL) *out_serial = 0u;

    struct aurora_window_toplevel *window =
        find_toplevel(policy, window_id);

    if (window == NULL ||
        out_serial == NULL ||
        width == 0u ||
        height == 0u ||
        width > policy->output_width ||
        height > policy->output_height ||
        (state_flags & ~(AURORA_WINDOW_STATE_MAXIMIZED |
                         AURORA_WINDOW_STATE_FULLSCREEN |
                         AURORA_WINDOW_STATE_MINIMIZED |
                         AURORA_WINDOW_STATE_ACTIVATED)) != 0u) {
        return false;
    }

    uint64_t serial =
        next_nonzero(&policy->next_configure_serial);

    window->pending_configure =
        (struct aurora_window_configure){
            .serial = serial,
            .width = width,
            .height = height,
            .state_flags = state_flags
        };

    window->configured = true;
    *out_serial = serial;
    return true;
}

bool window_policy_ack_configure(
    struct aurora_window_policy *policy,
    uint64_t window_id,
    uint64_t serial
) {
    struct aurora_window_toplevel *window =
        find_toplevel(policy, window_id);

    if (window == NULL ||
        !window->configured ||
        serial == 0u ||
        serial != window->pending_configure.serial ||
        serial <= window->acked_configure_serial) {
        return false;
    }

    window->acked_configure_serial = serial;
    return true;
}

bool window_policy_configure_ready(
    const struct aurora_window_policy *policy,
    uint64_t window_id,
    uint32_t committed_width,
    uint32_t committed_height
) {
    const struct aurora_window_toplevel *window =
        find_toplevel_const(policy, window_id);

    return window != NULL &&
        window->configured &&
        window->pending_configure.serial != 0u &&
        window->acked_configure_serial ==
            window->pending_configure.serial &&
        committed_width == window->pending_configure.width &&
        committed_height == window->pending_configure.height;
}

/*
 * Activation and placement are added by the following G5 implementation
 * steps.  Stubs fail closed until those policy mechanisms are installed.
 */
bool window_policy_issue_activation_token(
    struct aurora_window_policy *policy,
    uint64_t target_window_id,
    uint64_t interaction_serial,
    uint64_t *out_token
) {
    if (out_token != NULL) *out_token = 0u;

    if (policy == NULL ||
        !policy->initialized ||
        out_token == NULL ||
        interaction_serial == 0u ||
        find_toplevel(policy, target_window_id) == NULL) {
        return false;
    }

    for (uint32_t i = 0u;
         i < AURORA_WINDOW_POLICY_MAX_ACTIVATION_TOKENS;
         ++i) {
        if (!policy->activation_tokens[i].used ||
            policy->activation_tokens[i].consumed) {
            uint64_t token =
                next_nonzero(&policy->next_activation_token);

            policy->activation_tokens[i] =
                (struct aurora_window_activation_token){
                    .token = token,
                    .target_window_id = target_window_id,
                    .interaction_serial = interaction_serial,
                    .used = true,
                    .consumed = false
                };

            *out_token = token;
            return true;
        }
    }

    return false;
}

bool window_policy_activate(
    struct aurora_window_policy *policy,
    uint64_t target_window_id,
    uint64_t token,
    uint64_t current_interaction_serial,
    bool trusted_shell
) {
    struct aurora_window_toplevel *target =
        find_toplevel(policy, target_window_id);

    if (target == NULL) return false;

    if (!trusted_shell) {
        if (token == 0u ||
            current_interaction_serial == 0u) {
            return false;
        }

        struct aurora_window_activation_token *match = NULL;

        for (uint32_t i = 0u;
             i < AURORA_WINDOW_POLICY_MAX_ACTIVATION_TOKENS;
             ++i) {
            if (policy->activation_tokens[i].used &&
                !policy->activation_tokens[i].consumed &&
                policy->activation_tokens[i].token == token) {
                match = &policy->activation_tokens[i];
                break;
            }
        }

        if (match == NULL ||
            match->target_window_id != target_window_id ||
            current_interaction_serial < match->interaction_serial ||
            current_interaction_serial - match->interaction_serial >
                AURORA_WINDOW_ACTIVATION_MAX_SERIAL_AGE) {
            return false;
        }

        match->consumed = true;
    }

    for (uint32_t i = 0u;
         i < AURORA_WINDOW_POLICY_MAX_TOPLEVELS;
         ++i) {
        if (policy->toplevels[i].used) {
            policy->toplevels[i].active = false;
        }
    }

    target->active = true;
    return true;
}

bool window_policy_place_initial(
    struct aurora_window_policy *policy,
    uint64_t window_id,
    uint32_t width,
    uint32_t height,
    struct aurora_window_placement *out_placement
) {
    (void)policy;
    (void)window_id;
    (void)width;
    (void)height;
    if (out_placement != NULL) {
        *out_placement = (struct aurora_window_placement){0};
    }
    return false;
}

bool window_policy_raise(
    struct aurora_window_policy *policy,
    uint64_t window_id
) {
    (void)policy;
    (void)window_id;
    return false;
}

bool window_policy_read_toplevel(
    const struct aurora_window_policy *policy,
    uint64_t window_id,
    struct aurora_window_toplevel *out_toplevel
) {
    if (out_toplevel == NULL) return false;

    const struct aurora_window_toplevel *window =
        find_toplevel_const(policy, window_id);

    if (window == NULL) return false;

    *out_toplevel = *window;
    return true;
}

bool window_policy_selftest(void) {
    struct aurora_window_policy policy;
    struct aurora_graphics_surface surface = {
        .generation = 7u,
        .state = AURORA_GRAPHICS_SURFACE_READY
    };

    if (!window_policy_init(&policy, 1920u, 1080u)) {
        return false;
    }

    uint64_t window = 0u;
    if (!window_policy_create_toplevel(
            &policy,
            &surface,
            &window) ||
        window == 0u) {
        return false;
    }

    uint64_t first = 0u;
    uint64_t second = 0u;

    if (!window_policy_configure(
            &policy,
            window,
            800u,
            600u,
            AURORA_WINDOW_STATE_NONE,
            &first) ||
        first == 0u ||
        window_policy_configure_ready(
            &policy,
            window,
            800u,
            600u) ||
        !window_policy_ack_configure(
            &policy,
            window,
            first) ||
        !window_policy_configure_ready(
            &policy,
            window,
            800u,
            600u) ||
        window_policy_configure_ready(
            &policy,
            window,
            801u,
            600u)) {
        return false;
    }

    if (!window_policy_configure(
            &policy,
            window,
            1024u,
            768u,
            AURORA_WINDOW_STATE_MAXIMIZED,
            &second) ||
        second <= first ||
        window_policy_ack_configure(
            &policy,
            window,
            first) ||
        window_policy_ack_configure(
            &policy,
            window,
            second + 1u) ||
        !window_policy_ack_configure(
            &policy,
            window,
            second) ||
        !window_policy_configure_ready(
            &policy,
            window,
            1024u,
            768u)) {
        return false;
    }

    uint64_t token = 0u;

    if (!window_policy_issue_activation_token(
            &policy,
            window,
            100u,
            &token) ||
        token == 0u ||
        window_policy_activate(
            &policy,
            window,
            token,
            133u,
            false) ||
        !window_policy_activate(
            &policy,
            window,
            token,
            120u,
            false) ||
        window_policy_activate(
            &policy,
            window,
            token,
            120u,
            false)) {
        return false;
    }

    uint64_t stale_token = 0u;
    if (!window_policy_issue_activation_token(
            &policy,
            window,
            200u,
            &stale_token) ||
        window_policy_activate(
            &policy,
            window,
            stale_token,
            233u,
            false) ||
        !window_policy_activate(
            &policy,
            window,
            0u,
            0u,
            true)) {
        return false;
    }

    return true;
}
