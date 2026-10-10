#ifndef AURORA_GRAPHICS_INPUT_ROUTER_H
#define AURORA_GRAPHICS_INPUT_ROUTER_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/input.h>
#include <aurora/software_compositor.h>
#include <aurora/window_policy.h>

#define AURORA_GRAPHICS_INPUT_MAX_TARGETS 32u
#define AURORA_GRAPHICS_INPUT_QUEUE_CAPACITY 32u

struct aurora_graphics_input_target {
    uint64_t target_id;
    uint64_t node_id;
    uint64_t window_id; /* Trusted policy binding, never supplied by Ring 3. */
    struct aurora_input_event queue[AURORA_GRAPHICS_INPUT_QUEUE_CAPACITY];
    uint32_t head;
    uint32_t tail;
    bool used;
};

struct aurora_graphics_input_router {
    struct aurora_software_compositor *compositor;
    struct aurora_window_policy *window_policy;
    struct aurora_graphics_input_target targets[AURORA_GRAPHICS_INPUT_MAX_TARGETS];
    int32_t pointer_x;
    int32_t pointer_y;
    uint64_t pointer_focus_target;
    uint64_t keyboard_focus_target;
    uint64_t capture_target;
    bool initialized;
};

bool graphics_input_router_init(
    struct aurora_graphics_input_router *router,
    struct aurora_software_compositor *compositor
);

bool graphics_input_register_target(
    struct aurora_graphics_input_router *router,
    uint64_t target_id,
    uint64_t node_id
);

/* Trusted Shell-only registration and focus synchronization.
 * Unbound legacy targets retain their existing compositor-only behavior. */
bool graphics_input_bind_window_policy(
    struct aurora_graphics_input_router *router,
    struct aurora_window_policy *policy
);
bool graphics_input_bind_window_target(
    struct aurora_graphics_input_router *router,
    uint64_t target_id,
    uint64_t window_id
);
bool graphics_input_sync_window_focus(
    struct aurora_graphics_input_router *router
);

bool graphics_input_unregister_target(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
);

bool graphics_input_set_keyboard_focus(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
);

bool graphics_input_set_capture(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
);

bool graphics_input_request_capture(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
);

bool graphics_input_release_capture(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
);

void graphics_input_revoke_capture(
    struct aurora_graphics_input_router *router
);

void graphics_input_revoke_all_focus(
    struct aurora_graphics_input_router *router
);

void graphics_input_revoke_session(
    struct aurora_graphics_input_router *router
);

bool graphics_input_route_event(
    struct aurora_graphics_input_router *router,
    const struct aurora_input_event *event
);

bool graphics_input_poll_target(
    struct aurora_graphics_input_router *router,
    uint64_t target_id,
    struct aurora_input_event *out_event
);

bool graphics_input_router_selftest(void);

#endif
