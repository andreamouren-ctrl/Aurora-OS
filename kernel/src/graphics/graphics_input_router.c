#include <stddef.h>
#include <stdint.h>

#include <aurora/display.h>
#include <aurora/graphics_buffer.h>
#include <aurora/graphics_input_router.h>
#include <aurora/graphics_surface.h>
#include <aurora/pmm.h>

static void clear_bytes(void *ptr, uint64_t size) {
    uint8_t *p = (uint8_t *)ptr;
    for (uint64_t i = 0u; i < size; ++i) p[i] = 0u;
}

static struct aurora_graphics_input_target *find_target(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
) {
    if (router == NULL || target_id == 0u) return NULL;

    for (uint32_t i = 0u; i < AURORA_GRAPHICS_INPUT_MAX_TARGETS; ++i) {
        if (router->targets[i].used &&
            router->targets[i].target_id == target_id) {
            return &router->targets[i];
        }
    }

    return NULL;
}

static uint64_t target_for_node(
    struct aurora_graphics_input_router *router,
    uint64_t node_id
) {
    if (router == NULL || node_id == 0u) return 0u;

    for (uint32_t i = 0u; i < AURORA_GRAPHICS_INPUT_MAX_TARGETS; ++i) {
        if (router->targets[i].used &&
            router->targets[i].node_id == node_id) {
            return router->targets[i].target_id;
        }
    }

    return 0u;
}

static int32_t saturating_add_i32(int32_t a, int32_t b) {
    int64_t sum = (int64_t)a + (int64_t)b;

    if (sum > INT32_MAX) return INT32_MAX;
    if (sum < INT32_MIN) return INT32_MIN;
    return (int32_t)sum;
}

static bool enqueue(
    struct aurora_graphics_input_target *target,
    const struct aurora_input_event *event
) {
    if (target == NULL || event == NULL) return false;

    uint32_t next =
        (target->head + 1u) %
        AURORA_GRAPHICS_INPUT_QUEUE_CAPACITY;

    if (next == target->tail) {
        /*
         * Preserve key/button/scroll ordering under pressure. Only the newest
         * immediately-consecutive pointer motion may be coalesced.
         */
        uint32_t previous =
            (target->head +
             AURORA_GRAPHICS_INPUT_QUEUE_CAPACITY - 1u) %
            AURORA_GRAPHICS_INPUT_QUEUE_CAPACITY;
        struct aurora_input_event *last =
            &target->queue[previous];

        if (event->type == AURORA_INPUT_EVENT_POINTER_RELATIVE &&
            last->type == AURORA_INPUT_EVENT_POINTER_RELATIVE) {
            int32_t dx =
                saturating_add_i32(last->delta_x, event->delta_x);
            int32_t dy =
                saturating_add_i32(last->delta_y, event->delta_y);

            *last = *event;
            last->delta_x = dx;
            last->delta_y = dy;
            return true;
        }

        if (event->type == AURORA_INPUT_EVENT_POINTER_ABSOLUTE &&
            last->type == AURORA_INPUT_EVENT_POINTER_ABSOLUTE) {
            *last = *event;
            return true;
        }

        return false;
    }

    target->queue[target->head] = *event;
    target->head = next;
    return true;
}

static bool target_node_still_hittable(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
) {
    struct aurora_graphics_input_target *target =
        find_target(router, target_id);

    if (target == NULL) return false;

    for (uint32_t i = 0u; i < AURORA_COMPOSITOR_MAX_NODES; ++i) {
        const struct aurora_compositor_node *node =
            &router->compositor->nodes[i];

        if (node->used &&
            node->visible &&
            node->node_id == target->node_id) {
            if (router->compositor->secure_scene_active &&
                node->surface_class == AURORA_COMPOSITOR_SURFACE_NORMAL) {
                return false;
            }
            return true;
        }
    }

    return false;
}

static void graphics_input_scene_event(
    void *context,
    enum aurora_compositor_scene_event event,
    uint64_t node_id
) {
    struct aurora_graphics_input_router *router =
        (struct aurora_graphics_input_router *)context;

    if (router == NULL || !router->initialized) return;

    if (event == AURORA_COMPOSITOR_SCENE_DESTROYING) {
        graphics_input_revoke_session(router);
        return;
    }

    if (event == AURORA_COMPOSITOR_SCENE_NODE_REMOVED) {
        uint64_t target_id = target_for_node(router, node_id);

        if (target_id == 0u) return;

        if (router->pointer_focus_target == target_id) {
            router->pointer_focus_target = 0u;
        }
        if (router->keyboard_focus_target == target_id) {
            router->keyboard_focus_target = 0u;
        }
        if (router->capture_target == target_id) {
            router->capture_target = 0u;
        }

        struct aurora_graphics_input_target *target =
            find_target(router, target_id);

        if (target != NULL) {
            *target = (struct aurora_graphics_input_target){0};
        }

        return;
    }

    if (event == AURORA_COMPOSITOR_SCENE_NODE_VISIBILITY_CHANGED) {
        uint64_t target_id = target_for_node(router, node_id);

        if (target_id == 0u ||
            target_node_still_hittable(router, target_id)) {
            return;
        }

        if (router->pointer_focus_target == target_id) {
            router->pointer_focus_target = 0u;
        }
        if (router->keyboard_focus_target == target_id) {
            router->keyboard_focus_target = 0u;
        }
        if (router->capture_target == target_id) {
            router->capture_target = 0u;
        }

        return;
    }

    if (event == AURORA_COMPOSITOR_SCENE_SECURITY_POLICY_CHANGED) {
        if (router->pointer_focus_target != 0u &&
            !target_node_still_hittable(
                router,
                router->pointer_focus_target)) {
            router->pointer_focus_target = 0u;
        }

        if (router->keyboard_focus_target != 0u &&
            !target_node_still_hittable(
                router,
                router->keyboard_focus_target)) {
            router->keyboard_focus_target = 0u;
        }

        if (router->capture_target != 0u &&
            !target_node_still_hittable(
                router,
                router->capture_target)) {
            router->capture_target = 0u;
        }
    }
}

bool graphics_input_router_init(
    struct aurora_graphics_input_router *router,
    struct aurora_software_compositor *compositor
) {
    if (router == NULL ||
        compositor == NULL ||
        !compositor->initialized) {
        return false;
    }

    clear_bytes(router, sizeof(*router));
    router->compositor = compositor;
    router->initialized = true;

    if (!software_compositor_set_scene_observer(
            compositor,
            graphics_input_scene_event,
            router)) {
        clear_bytes(router, sizeof(*router));
        return false;
    }

    return true;
}

bool graphics_input_register_target(
    struct aurora_graphics_input_router *router,
    uint64_t target_id,
    uint64_t node_id
) {
    if (router == NULL ||
        !router->initialized ||
        target_id == 0u ||
        node_id == 0u ||
        find_target(router, target_id) != NULL ||
        target_for_node(router, node_id) != 0u) {
        return false;
    }

    bool node_exists = false;
    for (uint32_t i = 0u; i < AURORA_COMPOSITOR_MAX_NODES; ++i) {
        if (router->compositor->nodes[i].used &&
            router->compositor->nodes[i].node_id == node_id) {
            node_exists = true;
            break;
        }
    }

    if (!node_exists) return false;

    for (uint32_t i = 0u; i < AURORA_GRAPHICS_INPUT_MAX_TARGETS; ++i) {
        if (!router->targets[i].used) {
            router->targets[i] =
                (struct aurora_graphics_input_target){
                    .target_id = target_id,
                    .node_id = node_id,
                    .used = true
                };
            return true;
        }
    }

    return false;
}

bool graphics_input_bind_window_policy(
    struct aurora_graphics_input_router *router,
    struct aurora_window_policy *policy
) {
    if (router == NULL || !router->initialized ||
        policy == NULL || !policy->initialized ||
        router->window_policy != NULL) return false;
    /* Never activate policy mode while unrelated targets have focus/capture. */
    if (router->keyboard_focus_target != 0u ||
        router->capture_target != 0u) return false;
    router->window_policy = policy;
    return true;
}

static bool target_matches_window_surface(
    struct aurora_graphics_input_router *router,
    const struct aurora_graphics_input_target *target,
    const struct aurora_window_toplevel *window
) {
    if (router == NULL || target == NULL || window == NULL ||
        window->surface == NULL ||
        window->surface->generation != window->surface_generation)
        return false;
    for (uint32_t i = 0u; i < AURORA_COMPOSITOR_MAX_NODES; ++i) {
        const struct aurora_compositor_node *node =
            &router->compositor->nodes[i];
        if (!node->used || !node->visible ||
            node->node_id != target->node_id ||
            node->surface_class != AURORA_COMPOSITOR_SURFACE_NORMAL)
            continue;
        struct aurora_graphics_surface *attached = NULL;
        return graphics_surface_lookup(&router->compositor->surface_caps,
                        node->surface_handle, AURORA_RIGHT_READ, &attached) &&
               attached == window->surface;
    }
    return false;
}

bool graphics_input_bind_window_target(
    struct aurora_graphics_input_router *router,
    uint64_t target_id,
    uint64_t window_id
) {
    if (router == NULL || !router->initialized ||
        router->window_policy == NULL || !window_id) return false;
    struct aurora_graphics_input_target *target =
        find_target(router, target_id);
    struct aurora_window_toplevel window;
    if (target == NULL || target->window_id != 0u ||
        !window_policy_read_toplevel(router->window_policy,
                                     window_id, &window) ||
        !target_node_still_hittable(router, target_id) ||
        !target_matches_window_surface(router, target, &window))
        return false;
    for (uint32_t i = 0u; i < AURORA_GRAPHICS_INPUT_MAX_TARGETS; ++i) {
        if (router->targets[i].used &&
            router->targets[i].window_id == window_id) return false;
    }
    /* Only trusted callers can map a compositor target to its window.
     * The surface-to-node match is verified by the compositor owner when
     * that target/node pair is provisioned, before this function is called. */
    target->window_id = window_id;
    return true;
}

bool graphics_input_sync_window_focus(
    struct aurora_graphics_input_router *router
) {
    if (router == NULL || !router->initialized ||
        router->window_policy == NULL) return false;
    uint64_t window_id = 0u;
    uint64_t target_id = 0u;
    if (window_policy_active_committed(router->window_policy, &window_id)) {
        for (uint32_t i = 0u; i < AURORA_GRAPHICS_INPUT_MAX_TARGETS; ++i) {
            struct aurora_graphics_input_target *t = &router->targets[i];
            if (t->used && t->window_id == window_id &&
                target_node_still_hittable(router, t->target_id)) {
                struct aurora_window_toplevel window;
                if (!window_policy_read_toplevel(router->window_policy,
                                                  window_id, &window) ||
                    !target_matches_window_surface(router, t, &window))
                    continue;
                target_id = t->target_id;
                break;
            }
        }
    }
    if (router->keyboard_focus_target != target_id) {
        /* Capture is never carried across an authority/focus transition. */
        router->capture_target = 0u;
    }
    router->keyboard_focus_target = target_id;
    return target_id != 0u;
}

bool graphics_input_unbind_window_target(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
) {
    if (router == NULL || !router->initialized ||
        router->window_policy == NULL) return false;
    struct aurora_graphics_input_target *target =
        find_target(router, target_id);
    if (target == NULL || target->window_id == 0u) return false;
    if (router->pointer_focus_target == target_id)
        router->pointer_focus_target = 0u;
    if (router->keyboard_focus_target == target_id)
        router->keyboard_focus_target = 0u;
    if (router->capture_target == target_id)
        router->capture_target = 0u;
    /* Old queued key/button events must not survive window re-use. */
    target->head = 0u;
    target->tail = 0u;
    target->window_id = 0u;
    return true;
}

bool graphics_input_unregister_target(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
) {
    struct aurora_graphics_input_target *target =
        find_target(router, target_id);

    if (target == NULL) return false;

    if (router->pointer_focus_target == target_id) {
        router->pointer_focus_target = 0u;
    }
    if (router->keyboard_focus_target == target_id) {
        router->keyboard_focus_target = 0u;
    }
    if (router->capture_target == target_id) {
        router->capture_target = 0u;
    }

    *target = (struct aurora_graphics_input_target){0};
    return true;
}

bool graphics_input_set_keyboard_focus(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
) {
    if (router == NULL || !router->initialized) return false;

    if (target_id != 0u &&
        (!target_node_still_hittable(router, target_id))) {
        return false;
    }

    router->keyboard_focus_target = target_id;
    return true;
}

bool graphics_input_request_capture(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
) {
    if (router == NULL ||
        !router->initialized ||
        target_id == 0u ||
        router->pointer_focus_target != target_id ||
        !target_node_still_hittable(router, target_id)) {
        return false;
    }

    if (router->capture_target != 0u &&
        router->capture_target != target_id) {
        return false;
    }

    router->capture_target = target_id;
    return true;
}

bool graphics_input_release_capture(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
) {
    if (router == NULL ||
        !router->initialized ||
        target_id == 0u ||
        router->capture_target != target_id) {
        return false;
    }

    router->capture_target = 0u;
    return true;
}

bool graphics_input_set_capture(
    struct aurora_graphics_input_router *router,
    uint64_t target_id
) {
    return graphics_input_request_capture(
        router,
        target_id
    );
}

void graphics_input_revoke_capture(
    struct aurora_graphics_input_router *router
) {
    if (router != NULL) {
        router->capture_target = 0u;
    }
}

void graphics_input_revoke_all_focus(
    struct aurora_graphics_input_router *router
) {
    if (router == NULL) return;
    router->pointer_focus_target = 0u;
    router->keyboard_focus_target = 0u;
    router->capture_target = 0u;
}

void graphics_input_revoke_session(
    struct aurora_graphics_input_router *router
) {
    if (router == NULL || !router->initialized) return;

    graphics_input_revoke_all_focus(router);

    for (uint32_t i = 0u; i < AURORA_GRAPHICS_INPUT_MAX_TARGETS; ++i) {
        router->targets[i] =
            (struct aurora_graphics_input_target){0};
    }
}

static void clamp_pointer(
    struct aurora_graphics_input_router *router
) {
    const struct aurora_display_mode *mode =
        display_mode_at(router->compositor->output_index, 0u);

    if (mode == NULL || mode->width == 0u || mode->height == 0u) {
        router->pointer_x = 0;
        router->pointer_y = 0;
        return;
    }

    int64_t max_x =
        mode->width - 1u > INT32_MAX
            ? INT32_MAX
            : (int64_t)mode->width - 1;
    int64_t max_y =
        mode->height - 1u > INT32_MAX
            ? INT32_MAX
            : (int64_t)mode->height - 1;

    if (router->pointer_x < 0) router->pointer_x = 0;
    if (router->pointer_y < 0) router->pointer_y = 0;
    if ((int64_t)router->pointer_x > max_x) {
        router->pointer_x = (int32_t)max_x;
    }
    if ((int64_t)router->pointer_y > max_y) {
        router->pointer_y = (int32_t)max_y;
    }
}

static uint64_t update_pointer_focus(
    struct aurora_graphics_input_router *router
) {
    if (router->capture_target != 0u) {
        if (target_node_still_hittable(
                router,
                router->capture_target)) {
            router->pointer_focus_target =
                router->capture_target;
            return router->capture_target;
        }

        router->capture_target = 0u;
    }

    uint64_t node_id = 0u;
    if (!software_compositor_hit_test(
            router->compositor,
            router->pointer_x,
            router->pointer_y,
            &node_id)) {
        router->pointer_focus_target = 0u;
        return 0u;
    }

    uint64_t selected_target = target_for_node(router, node_id);
    if (router->window_policy != NULL && selected_target != 0u) {
        struct aurora_graphics_input_target *target =
            find_target(router, selected_target);
        uint64_t selected_window = 0u;
        if (target == NULL || target->window_id == 0u ||
            !window_policy_hit_test_committed(
                router->window_policy, router->pointer_x,
                router->pointer_y, &selected_window) ||
            selected_window != target->window_id) {
            selected_target = 0u;
        } else {
            struct aurora_window_toplevel window;
            if (!window_policy_read_toplevel(router->window_policy,
                                              selected_window, &window) ||
                !target_matches_window_surface(router, target, &window))
                selected_target = 0u;
        }
    }
    router->pointer_focus_target = selected_target;
    return selected_target;
}

bool graphics_input_route_event(
    struct aurora_graphics_input_router *router,
    const struct aurora_input_event *event
) {
    if (router == NULL ||
        !router->initialized ||
        event == NULL) {
        return false;
    }

    if (event->type == AURORA_INPUT_EVENT_POINTER_RELATIVE) {
        int64_t nx =
            (int64_t)router->pointer_x + event->delta_x;
        int64_t ny =
            (int64_t)router->pointer_y + event->delta_y;

        router->pointer_x =
            nx < INT32_MIN ? INT32_MIN :
            nx > INT32_MAX ? INT32_MAX :
            (int32_t)nx;
        router->pointer_y =
            ny < INT32_MIN ? INT32_MIN :
            ny > INT32_MAX ? INT32_MAX :
            (int32_t)ny;

        clamp_pointer(router);
        uint64_t target_id = update_pointer_focus(router);
        struct aurora_graphics_input_target *target =
            find_target(router, target_id);
        return target == NULL ? true : enqueue(target, event);
    }

    if (event->type == AURORA_INPUT_EVENT_POINTER_ABSOLUTE) {
        router->pointer_x = event->absolute_x;
        router->pointer_y = event->absolute_y;
        clamp_pointer(router);
        uint64_t target_id = update_pointer_focus(router);
        struct aurora_graphics_input_target *target =
            find_target(router, target_id);
        return target == NULL ? true : enqueue(target, event);
    }

    if (event->type == AURORA_INPUT_EVENT_POINTER_BUTTON ||
        event->type == AURORA_INPUT_EVENT_SCROLL) {
        uint64_t target_id = update_pointer_focus(router);
        struct aurora_graphics_input_target *target =
            find_target(router, target_id);
        return target == NULL ? true : enqueue(target, event);
    }

    if (event->type == AURORA_INPUT_EVENT_KEY) {
        if (router->window_policy != NULL)
            (void)graphics_input_sync_window_focus(router);
        if (router->keyboard_focus_target != 0u &&
            !target_node_still_hittable(
                router,
                router->keyboard_focus_target)) {
            router->keyboard_focus_target = 0u;
        }

        struct aurora_graphics_input_target *target =
            find_target(
                router,
                router->keyboard_focus_target
            );
        return target == NULL ? true : enqueue(target, event);
    }

    return true;
}

bool graphics_input_poll_target(
    struct aurora_graphics_input_router *router,
    uint64_t target_id,
    struct aurora_input_event *out_event
) {
    struct aurora_graphics_input_target *target =
        find_target(router, target_id);

    if (target == NULL || out_event == NULL ||
        target->tail == target->head) {
        return false;
    }

    *out_event = target->queue[target->tail];
    target->tail =
        (target->tail + 1u) %
        AURORA_GRAPHICS_INPUT_QUEUE_CAPACITY;
    return true;
}

static bool fill_buffer(
    struct aurora_graphics_buffer *buffer,
    uint32_t value
) {
    if (buffer == NULL || buffer->memory == NULL) return false;

    for (uint64_t page = 0u;
         page < buffer->memory->page_count;
         ++page) {
        uint64_t physical = 0u;
        if (!memory_object_page_at(
                buffer->memory,
                (uint32_t)page,
                &physical)) {
            return false;
        }

        uint8_t *bytes =
            (uint8_t *)pmm_phys_to_virt(physical);

        for (uint32_t i = 0u; i < AURORA_PAGE_SIZE; i += 4u) {
            *(uint32_t *)(void *)(bytes + i) = value;
        }
    }
    return true;
}

bool graphics_input_router_selftest(void) {
    /*
     * Queue pressure may coalesce only adjacent motion. Key/button ordering
     * must remain intact and a full queue must still reject other event types.
     */
    static struct aurora_graphics_input_target pressure;
    pressure = (struct aurora_graphics_input_target){
        .target_id = 999u,
        .node_id = 999u,
        .used = true
    };
    const struct aurora_input_event pressure_key = {
        .type = AURORA_INPUT_EVENT_KEY,
        .source = AURORA_INPUT_SOURCE_SYNTHETIC,
        .synthetic = true,
        .key = AURORA_KEY_A,
        .pressed = true
    };
    const struct aurora_input_event motion_a = {
        .type = AURORA_INPUT_EVENT_POINTER_RELATIVE,
        .source = AURORA_INPUT_SOURCE_SYNTHETIC,
        .synthetic = true,
        .delta_x = 4,
        .delta_y = -3
    };
    const struct aurora_input_event motion_b = {
        .type = AURORA_INPUT_EVENT_POINTER_RELATIVE,
        .source = AURORA_INPUT_SOURCE_SYNTHETIC,
        .synthetic = true,
        .delta_x = 7,
        .delta_y = 5
    };

    for (uint32_t i = 0u;
         i < AURORA_GRAPHICS_INPUT_QUEUE_CAPACITY - 2u;
         ++i) {
        if (!enqueue(&pressure, &pressure_key)) {
            return false;
        }
    }

    if (!enqueue(&pressure, &motion_a) ||
        !enqueue(&pressure, &motion_b) ||
        enqueue(&pressure, &pressure_key)) {
        return false;
    }

    for (uint32_t i = 0u;
         i < AURORA_GRAPHICS_INPUT_QUEUE_CAPACITY - 2u;
         ++i) {
        struct aurora_input_event queued = {0};

        if (pressure.tail == pressure.head) return false;
        queued = pressure.queue[pressure.tail];
        pressure.tail =
            (pressure.tail + 1u) %
            AURORA_GRAPHICS_INPUT_QUEUE_CAPACITY;

        if (queued.type != AURORA_INPUT_EVENT_KEY) {
            return false;
        }
    }

    if (pressure.tail == pressure.head) return false;
    struct aurora_input_event coalesced =
        pressure.queue[pressure.tail];
    pressure.tail =
        (pressure.tail + 1u) %
        AURORA_GRAPHICS_INPUT_QUEUE_CAPACITY;

    if (coalesced.type != AURORA_INPUT_EVENT_POINTER_RELATIVE ||
        coalesced.delta_x != 11 ||
        coalesced.delta_y != 2 ||
        pressure.tail != pressure.head) {
        return false;
    }

    const struct aurora_display_mode *mode =
        display_mode_at(0u, 0u);

    if (mode == NULL || mode->width < 64u || mode->height < 32u) {
        return false;
    }

    static struct aurora_software_compositor compositor;
    static struct aurora_cap_table caps;
    static struct aurora_cap_table authority_caps;
    static struct aurora_graphics_input_router router;

    if (!software_compositor_init(&compositor, 0u)) return false;
    cap_table_init(&caps);
    cap_table_init(&authority_caps);

    struct aurora_graphics_buffer *a_buf =
        graphics_buffer_create(16u, 16u, &mode->format);
    struct aurora_graphics_buffer *b_buf =
        graphics_buffer_create(16u, 16u, &mode->format);
    struct aurora_graphics_surface *a_surface =
        graphics_surface_create();
    struct aurora_graphics_surface *b_surface =
        graphics_surface_create();

    if (a_buf == NULL || b_buf == NULL ||
        a_surface == NULL || b_surface == NULL ||
        !fill_buffer(a_buf, 0x00FF0000u) ||
        !fill_buffer(b_buf, 0x0000FF00u)) {
        return false;
    }

    aurora_cap_handle ab =
        graphics_buffer_grant(&caps, a_buf, AURORA_RIGHT_READ);
    aurora_cap_handle bb =
        graphics_buffer_grant(&caps, b_buf, AURORA_RIGHT_READ);
    aurora_cap_handle as =
        graphics_surface_grant(
            &caps, a_surface,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE);
    aurora_cap_handle bs =
        graphics_surface_grant(
            &caps, b_surface,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE);

    struct aurora_graphics_rect damage = {
        .x = 0u, .y = 0u, .width = 16u, .height = 16u
    };
    uint64_t commit = 0u;

    if (ab == AURORA_CAP_INVALID ||
        bb == AURORA_CAP_INVALID ||
        as == AURORA_CAP_INVALID ||
        bs == AURORA_CAP_INVALID ||
        !graphics_surface_attach(&caps, as, ab) ||
        !graphics_surface_damage(&caps, as, &damage) ||
        !graphics_surface_commit(&caps, as, &commit) ||
        !graphics_surface_attach(&caps, bs, bb) ||
        !graphics_surface_damage(&caps, bs, &damage) ||
        !graphics_surface_commit(&caps, bs, &commit)) {
        return false;
    }

    uint64_t an = 0u;
    uint64_t bn = 0u;

    if (!software_compositor_add_surface(
            &compositor, a_surface,
            4, 4, 0, 255u, &an) ||
        !software_compositor_add_surface(
            &compositor, b_surface,
            28, 4, 0, 255u, &bn)) {
        return false;
    }

    if (!graphics_input_router_init(&router, &compositor) ||
        !graphics_input_register_target(&router, 101u, an) ||
        !graphics_input_register_target(&router, 202u, bn) ||
        !graphics_input_set_keyboard_focus(&router, 101u)) {
        return false;
    }

    const struct aurora_display_output *output =
        display_output_at(0u);

    aurora_cap_handle display_control =
        output == NULL
            ? AURORA_CAP_INVALID
            : cap_grant(
                &authority_caps,
                (void *)output,
                AURORA_CAP_DISPLAY,
                AURORA_RIGHT_CONTROL
            );

    if (display_control == AURORA_CAP_INVALID) {
        return false;
    }

    const struct aurora_input_event to_a = {
        .type = AURORA_INPUT_EVENT_POINTER_ABSOLUTE,
        .source = AURORA_INPUT_SOURCE_SYNTHETIC,
        .synthetic = true,
        .absolute_x = 8,
        .absolute_y = 8
    };
    const struct aurora_input_event click = {
        .type = AURORA_INPUT_EVENT_POINTER_BUTTON,
        .source = AURORA_INPUT_SOURCE_SYNTHETIC,
        .synthetic = true,
        .button = AURORA_POINTER_BUTTON_LEFT,
        .pressed = true
    };
    const struct aurora_input_event key = {
        .type = AURORA_INPUT_EVENT_KEY,
        .source = AURORA_INPUT_SOURCE_SYNTHETIC,
        .synthetic = true,
        .key = AURORA_KEY_A,
        .pressed = true
    };
    const struct aurora_input_event to_b = {
        .type = AURORA_INPUT_EVENT_POINTER_ABSOLUTE,
        .source = AURORA_INPUT_SOURCE_SYNTHETIC,
        .synthetic = true,
        .absolute_x = 32,
        .absolute_y = 8
    };

    if (!graphics_input_route_event(&router, &to_a) ||
        router.pointer_focus_target != 101u ||
        !graphics_input_request_capture(&router, 101u) ||
        graphics_input_release_capture(&router, 202u) ||
        !graphics_input_route_event(&router, &click) ||
        !graphics_input_route_event(&router, &key) ||
        !graphics_input_route_event(&router, &to_b) ||
        router.pointer_focus_target != 101u ||
        !graphics_input_route_event(&router, &click) ||
        !graphics_input_release_capture(&router, 101u) ||
        router.capture_target != 0u ||
        !graphics_input_route_event(&router, &to_b) ||
        router.pointer_focus_target != 202u) {
        return false;
    }

    struct aurora_input_event event = {0};

    /* A receives motion, click and key, but not B's final click. */
    uint32_t a_count = 0u;
    bool a_key = false;
    while (graphics_input_poll_target(&router, 101u, &event)) {
        ++a_count;
        if (event.type == AURORA_INPUT_EVENT_KEY) a_key = true;
    }

    uint32_t b_count = 0u;
    bool b_click = false;
    while (graphics_input_poll_target(&router, 202u, &event)) {
        ++b_count;
        if (event.type == AURORA_INPUT_EVENT_POINTER_BUTTON) b_click = true;
    }

    bool isolated =
        a_count == 5u &&
        a_key &&
        b_count == 1u &&
        !b_click &&
        router.pointer_focus_target == 202u &&
        router.keyboard_focus_target == 101u;

    if (!isolated ||
        !graphics_input_set_keyboard_focus(&router, 101u) ||
        !graphics_input_route_event(&router, &to_a) ||
        !graphics_input_request_capture(&router, 101u)) {
        return false;
    }

    if (!software_compositor_set_node(
            &compositor,
            an,
            4, 4, 0, 255u, false) ||
        router.pointer_focus_target != 0u ||
        router.keyboard_focus_target != 0u ||
        router.capture_target != 0u ||
        find_target(&router, 101u) == NULL ||
        !software_compositor_set_node(
            &compositor,
            an,
            4, 4, 0, 255u, true) ||
        !graphics_input_route_event(&router, &to_a) ||
        router.pointer_focus_target != 101u ||
        !graphics_input_set_keyboard_focus(&router, 101u) ||
        !graphics_input_request_capture(&router, 101u)) {
        return false;
    }

    if (!software_compositor_set_secure_scene(
            &compositor,
            &authority_caps,
            display_control,
            true) ||
        router.pointer_focus_target != 0u ||
        router.keyboard_focus_target != 0u ||
        router.capture_target != 0u) {
        return false;
    }

    if (!software_compositor_set_secure_scene(
            &compositor,
            &authority_caps,
            display_control,
            false) ||
        !graphics_input_route_event(&router, &to_b) ||
        router.pointer_focus_target != 202u ||
        !graphics_input_set_keyboard_focus(&router, 202u) ||
        !graphics_input_request_capture(&router, 202u)) {
        return false;
    }

    if (!software_compositor_remove_surface(&compositor, bn) ||
        find_target(&router, 202u) != NULL ||
        router.pointer_focus_target != 0u ||
        router.keyboard_focus_target != 0u ||
        router.capture_target != 0u) {
        return false;
    }

    if (!graphics_input_route_event(&router, &to_a) ||
        router.pointer_focus_target != 101u ||
        !graphics_input_set_keyboard_focus(&router, 101u) ||
        !graphics_input_request_capture(&router, 101u) ||
        !software_compositor_destroy(&compositor) ||
        find_target(&router, 101u) != NULL ||
        router.pointer_focus_target != 0u ||
        router.keyboard_focus_target != 0u ||
        router.capture_target != 0u) {
        return false;
    }

    uint32_t asg = a_surface->generation;
    uint32_t bsg = b_surface->generation;
    uint32_t abg = a_buf->generation;
    uint32_t bbg = b_buf->generation;

    cap_table_destroy(&authority_caps);
    cap_table_destroy(&caps);

    return
        graphics_surface_release_owner(a_surface, asg) &&
        graphics_surface_release_owner(b_surface, bsg) &&
        graphics_buffer_release_owner(a_buf, abg) &&
        graphics_buffer_release_owner(b_buf, bbg);
}
