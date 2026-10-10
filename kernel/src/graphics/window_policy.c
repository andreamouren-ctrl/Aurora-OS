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

static bool surface_live(const struct aurora_window_toplevel *window) {
    return window != NULL && window->surface != NULL &&
        window->surface->generation == window->surface_generation &&
        window->surface->state != AURORA_GRAPHICS_SURFACE_FREE &&
        !window->surface->destroy_requested;
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
        output_height == 0u ||
        output_width > INT32_MAX || output_height > INT32_MAX) {
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
        if (policy->toplevels[i].used &&
            policy->toplevels[i].surface == surface &&
            surface_live(&policy->toplevels[i])) return false;
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

    if (!surface_live(window) ||
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

    if (!surface_live(window) ||
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

    return surface_live(window) &&
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
        !surface_live(find_toplevel(policy, target_window_id))) {
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

    if (!surface_live(target)) return false;

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
    if (out_placement != NULL) {
        *out_placement = (struct aurora_window_placement){0};
    }

    struct aurora_window_toplevel *window =
        find_toplevel(policy, window_id);

    if (!surface_live(window) ||
        out_placement == NULL ||
        width == 0u ||
        height == 0u ||
        width > policy->output_width ||
        height > policy->output_height) {
        return false;
    }

    uint32_t centered_x =
        (policy->output_width - width) / 2u;
    uint32_t centered_y =
        (policy->output_height - height) / 2u;

    uint32_t cascade = policy->cascade_index++;
    uint32_t offset = (cascade % 8u) * 32u;

    uint32_t max_x = policy->output_width - width;
    uint32_t max_y = policy->output_height - height;

    uint32_t x = centered_x + offset;
    uint32_t y = centered_y + offset;

    if (x > max_x) x = max_x;
    if (y > max_y) y = max_y;

    int32_t z = policy->next_z++;
    if (policy->next_z <= 0) {
        policy->next_z = 1;
    }

    window->placement = (struct aurora_window_placement){
        .x = (int32_t)x,
        .y = (int32_t)y,
        .z = z
    };

    *out_placement = window->placement;
    return true;
}

bool window_policy_move(
    struct aurora_window_policy *policy,
    uint64_t window_id,
    int32_t x,
    int32_t y,
    uint32_t width,
    uint32_t height
) {
    struct aurora_window_toplevel *window =
        find_toplevel(policy, window_id);
    if (window == NULL ||
        window->surface == NULL ||
        window->surface->generation != window->surface_generation ||
        window->surface->destroy_requested ||
        window->surface->state == AURORA_GRAPHICS_SURFACE_FREE ||
        !window_policy_configure_ready(policy, window_id, width, height) ||
        x < 0 || y < 0 ||
        (uint64_t)(uint32_t)x + width > policy->output_width ||
        (uint64_t)(uint32_t)y + height > policy->output_height) {
        return false;
    }
    window->placement.x = x;
    window->placement.y = y;
    return true;
}

bool window_policy_destroy_toplevel(
    struct aurora_window_policy *policy,
    uint64_t window_id
) {
    struct aurora_window_toplevel *window =
        find_toplevel(policy, window_id);
    if (window == NULL) return false;

    for (uint32_t i = 0u;
         i < AURORA_WINDOW_POLICY_MAX_ACTIVATION_TOKENS;
         ++i) {
        if (policy->activation_tokens[i].used &&
            policy->activation_tokens[i].target_window_id == window_id) {
            clear_bytes(&policy->activation_tokens[i],
                        sizeof(policy->activation_tokens[i]));
        }
    }
    clear_bytes(window, sizeof(*window));
    return true;
}

uint32_t window_policy_revoke_surface(
    struct aurora_window_policy *policy,
    const struct aurora_graphics_surface *surface
) {
    if (policy == NULL || !policy->initialized || surface == NULL)
        return 0u;
    uint32_t revoked = 0u;
    for (uint32_t i = 0u; i < AURORA_WINDOW_POLICY_MAX_TOPLEVELS; ++i) {
        if (policy->toplevels[i].used &&
            policy->toplevels[i].surface == surface) {
            uint64_t id = policy->toplevels[i].window_id;
            if (window_policy_destroy_toplevel(policy, id)) ++revoked;
        }
    }
    return revoked;
}

void window_policy_reset(struct aurora_window_policy *policy) {
    if (policy == NULL) return;
    clear_bytes(policy, sizeof(*policy));
}

bool window_policy_raise(
    struct aurora_window_policy *policy,
    uint64_t window_id
) {
    struct aurora_window_toplevel *window =
        find_toplevel(policy, window_id);

    if (!surface_live(window)) return false;

    int32_t z = policy->next_z++;
    if (policy->next_z <= 0) {
        policy->next_z = 1;
    }

    window->placement.z = z;
    return true;
}

bool window_policy_read_toplevel(
    const struct aurora_window_policy *policy,
    uint64_t window_id,
    struct aurora_window_toplevel *out_toplevel
) {
    if (out_toplevel == NULL) return false;

    const struct aurora_window_toplevel *window =
        find_toplevel_const(policy, window_id);

    if (!surface_live(window)) return false;

    *out_toplevel = *window;
    return true;
}

bool window_policy_hit_test(
    const struct aurora_window_policy *policy,
    int32_t x,
    int32_t y,
    uint64_t *out_window_id
) {
    if (out_window_id != NULL) *out_window_id = 0u;
    if (policy == NULL || !policy->initialized || out_window_id == NULL ||
        x < 0 || y < 0 ||
        (uint32_t)x >= policy->output_width ||
        (uint32_t)y >= policy->output_height) return false;

    int32_t top_z = INT32_MIN;
    uint64_t top_id = 0u;
    for (uint32_t i = 0u; i < AURORA_WINDOW_POLICY_MAX_TOPLEVELS; ++i) {
        const struct aurora_window_toplevel *w = &policy->toplevels[i];
        if (!w->used || !surface_live(w) || !w->configured ||
            (w->pending_configure.state_flags &
             AURORA_WINDOW_STATE_MINIMIZED) != 0u ||
            w->acked_configure_serial != w->pending_configure.serial ||
            w->pending_configure.serial == 0u) continue;
        const int64_t dx = (int64_t)x - w->placement.x;
        const int64_t dy = (int64_t)y - w->placement.y;
        if (dx < 0 || dy < 0 ||
            (uint64_t)dx >= w->pending_configure.width ||
            (uint64_t)dy >= w->pending_configure.height) continue;
        if (top_id == 0u || w->placement.z > top_z) {
            top_z = w->placement.z;
            top_id = w->window_id;
        }
    }
    if (top_id == 0u) return false;
    *out_window_id = top_id;
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

    struct aurora_graphics_surface second_surface = {
        .generation = 8u,
        .state = AURORA_GRAPHICS_SURFACE_READY
    };
    uint64_t second_window = 0u;

    if (!window_policy_create_toplevel(
            &policy,
            &second_surface,
            &second_window) ||
        second_window == 0u ||
        second_window == window) {
        return false;
    }

    struct aurora_window_placement first_place = {0};
    struct aurora_window_placement second_place = {0};

    if (!window_policy_place_initial(
            &policy,
            window,
            800u,
            600u,
            &first_place) ||
        !window_policy_place_initial(
            &policy,
            second_window,
            800u,
            600u,
            &second_place) ||
        first_place.x != 560 ||
        first_place.y != 240 ||
        second_place.x <= first_place.x ||
        second_place.y <= first_place.y ||
        second_place.z <= first_place.z ||
        !window_policy_raise(
            &policy,
            window)) {
        return false;
    }

    struct aurora_window_toplevel first_snapshot = {0};
    struct aurora_window_toplevel second_snapshot = {0};

    if (!window_policy_read_toplevel(
            &policy,
            window,
            &first_snapshot) ||
        !window_policy_read_toplevel(
            &policy,
            second_window,
            &second_snapshot) ||
        first_snapshot.placement.z <= second_snapshot.placement.z) {
        return false;
    }

    /* G5 lifecycle gate: no offscreen move, no stale token after close. */
    if (!window_policy_move(&policy, window, 50, 60, 1024u, 768u) ||
        window_policy_move(&policy, window, -1, 60, 1024u, 768u) ||
        window_policy_move(&policy, window, 1000, 60, 1024u, 768u) ||
        !window_policy_issue_activation_token(
            &policy, window, 300u, &token) ||
        !window_policy_destroy_toplevel(&policy, window) ||
        window_policy_destroy_toplevel(&policy, window) ||
        window_policy_activate(&policy, window, token, 300u, false) ||
        window_policy_move(&policy, window, 0, 0, 1024u, 768u)) {
        return false;
    }

    /* Reject stale surface generations across every policy entry point. */
    uint64_t stale_id = 0u;
    struct aurora_graphics_surface stale_surface = {
        .generation = 13u,
        .state = AURORA_GRAPHICS_SURFACE_READY
    };
    if (!window_policy_create_toplevel(
            &policy, &stale_surface, &stale_id) ||
        window_policy_create_toplevel(
            &policy, &stale_surface, &token)) {
        return false;
    }
    stale_surface.generation++;
    uint64_t serial = 0u;
    if (window_policy_configure(&policy, stale_id, 400u, 300u, 0u, &serial) ||
        window_policy_issue_activation_token(&policy, stale_id, 400u, &token) ||
        window_policy_activate(&policy, stale_id, 0u, 0u, true) ||
        window_policy_raise(&policy, stale_id) ||
        window_policy_read_toplevel(&policy, stale_id, &first_snapshot) ||
        !window_policy_destroy_toplevel(&policy, stale_id)) {
        return false;
    }

    /* Surface teardown revokes all outstanding activation authority. */
    struct aurora_graphics_surface cleanup_surface = {
        .generation = 20u,
        .state = AURORA_GRAPHICS_SURFACE_READY
    };
    uint64_t cleanup_id = 0u;
    uint64_t cleanup_token = 0u;
    if (!window_policy_create_toplevel(&policy, &cleanup_surface, &cleanup_id) ||
        !window_policy_issue_activation_token(&policy, cleanup_id, 450u,
                                               &cleanup_token) ||
        window_policy_revoke_surface(&policy, &cleanup_surface) != 1u ||
        window_policy_revoke_surface(&policy, &cleanup_surface) != 0u ||
        window_policy_activate(&policy, cleanup_id, cleanup_token, 450u, false)) {
        return false;
    }
    window_policy_reset(&policy);
    if (window_policy_create_toplevel(&policy, &cleanup_surface, &cleanup_id) ||
        window_policy_issue_activation_token(&policy, cleanup_id, 451u,
                                              &cleanup_token)) {
        return false;
    }

    return true;
}
