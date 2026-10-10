#ifndef AURORA_WINDOW_POLICY_H
#define AURORA_WINDOW_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/graphics_surface.h>

#define AURORA_WINDOW_POLICY_MAX_TOPLEVELS 64u
#define AURORA_WINDOW_POLICY_MAX_ACTIVATION_TOKENS 32u
#define AURORA_WINDOW_ACTIVATION_MAX_SERIAL_AGE 32u

enum aurora_window_state_flags {
    AURORA_WINDOW_STATE_NONE = 0,
    AURORA_WINDOW_STATE_MAXIMIZED = 1u << 0,
    AURORA_WINDOW_STATE_FULLSCREEN = 1u << 1,
    AURORA_WINDOW_STATE_MINIMIZED = 1u << 2,
    AURORA_WINDOW_STATE_ACTIVATED = 1u << 3
};

struct aurora_window_configure {
    uint64_t serial;
    uint32_t width;
    uint32_t height;
    uint32_t state_flags;
};

struct aurora_window_placement {
    int32_t x;
    int32_t y;
    int32_t z;
};

struct aurora_window_toplevel {
    uint64_t window_id;
    struct aurora_graphics_surface *surface;
    uint32_t surface_generation;
    struct aurora_window_configure pending_configure;
    uint64_t acked_configure_serial;
    uint64_t commit_serial_at_ack;
    struct aurora_window_placement placement;
    bool configured;
    bool active;
    bool used;
};

struct aurora_window_activation_token {
    uint64_t token;
    uint64_t target_window_id;
    uint64_t interaction_serial;
    bool used;
    bool consumed;
};

struct aurora_window_policy {
    struct aurora_window_toplevel toplevels[
        AURORA_WINDOW_POLICY_MAX_TOPLEVELS
    ];
    struct aurora_window_activation_token activation_tokens[
        AURORA_WINDOW_POLICY_MAX_ACTIVATION_TOKENS
    ];
    uint32_t output_width;
    uint32_t output_height;
    uint64_t next_window_id;
    uint64_t next_configure_serial;
    uint64_t next_activation_token;
    int32_t next_z;
    uint32_t cascade_index;
    bool initialized;
};

bool window_policy_init(
    struct aurora_window_policy *policy,
    uint32_t output_width,
    uint32_t output_height
);

bool window_policy_create_toplevel(
    struct aurora_window_policy *policy,
    struct aurora_graphics_surface *surface,
    uint64_t *out_window_id
);

bool window_policy_configure(
    struct aurora_window_policy *policy,
    uint64_t window_id,
    uint32_t width,
    uint32_t height,
    uint32_t state_flags,
    uint64_t *out_serial
);

bool window_policy_ack_configure(
    struct aurora_window_policy *policy,
    uint64_t window_id,
    uint64_t serial
);

bool window_policy_configure_ready(
    const struct aurora_window_policy *policy,
    uint64_t window_id,
    uint32_t committed_width,
    uint32_t committed_height
);

bool window_policy_issue_activation_token(
    struct aurora_window_policy *policy,
    uint64_t target_window_id,
    uint64_t interaction_serial,
    uint64_t *out_token
);

bool window_policy_activate(
    struct aurora_window_policy *policy,
    uint64_t target_window_id,
    uint64_t token,
    uint64_t current_interaction_serial,
    bool trusted_shell
);

bool window_policy_place_initial(
    struct aurora_window_policy *policy,
    uint64_t window_id,
    uint32_t width,
    uint32_t height,
    struct aurora_window_placement *out_placement
);

bool window_policy_move(
    struct aurora_window_policy *policy,
    uint64_t window_id,
    int32_t x,
    int32_t y,
    uint32_t width,
    uint32_t height
);

/* Call before releasing a surface owner or tearing down a session. */
uint32_t window_policy_revoke_surface(
    struct aurora_window_policy *policy,
    const struct aurora_graphics_surface *surface
);

void window_policy_reset(struct aurora_window_policy *policy);

bool window_policy_destroy_toplevel(
    struct aurora_window_policy *policy,
    uint64_t window_id
);

bool window_policy_raise(
    struct aurora_window_policy *policy,
    uint64_t window_id
);

bool window_policy_read_toplevel(
    const struct aurora_window_policy *policy,
    uint64_t window_id,
    struct aurora_window_toplevel *out_toplevel
);

/* Query the topmost visible, configured toplevel containing a point.
 * This is policy geometry only: the input router still owns delivery. */
/* Input-safe variant: requires a newer committed buffer after configure ACK.
 * A geometry-only hit test must never authorize pointer delivery. */
bool window_policy_hit_test_committed(
    const struct aurora_window_policy *policy,
    int32_t x,
    int32_t y,
    uint64_t *out_window_id
);

bool window_policy_hit_test(
    const struct aurora_window_policy *policy,
    int32_t x,
    int32_t y,
    uint64_t *out_window_id
);

/* Trusted input router only: select a committed target and consume a
 * target-bound, one-shot interaction token before changing focus. */
bool window_policy_focus_at(
    struct aurora_window_policy *policy,
    int32_t x,
    int32_t y,
    uint64_t activation_token,
    uint64_t interaction_serial,
    uint64_t *out_window_id
);

bool window_policy_selftest(void);

#endif
