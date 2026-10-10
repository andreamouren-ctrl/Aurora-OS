#include <aurora/g5_shell_scene.h>
#include <aurora/display.h>
#include <aurora/log.h>
#include <aurora/capability_abi.h>
#include <stddef.h>

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8u) |
           ((uint32_t)p[2]<<16u) | ((uint32_t)p[3]<<24u);
}

static uint64_t read_u64_le(const uint8_t *p) {
    uint64_t value=0u;
    for (unsigned i=0u;i<8u;++i)
        value|=((uint64_t)p[i])<<(i*8u);
    return value;
}

void g5_shell_scene_detach_second(struct g5_shell_scene *scene) {
    if (scene == NULL) return;
    struct g5_shell_extra_client *e = &scene->extra;
    /* An unallocated slot is zero-initialized, and slot 0 belongs
     * to the primary Shell window: never detach it on failed start. */
    if (e->owner == NULL) return;
    if (e->window_id && scene->input_router.initialized) {
        (void)graphics_input_unbind_window_target(&scene->input_router,
                                                   e->window_id);
        (void)graphics_input_unregister_target(&scene->input_router,
                                                e->window_id);
    }
    if (e->window_id)
        (void)window_policy_destroy_toplevel(&scene->window_policy,
                                              e->window_id);
    if (e->slot < G5_SURFACE_REGISTRY_CAPACITY &&
        scene->bridge.node_ids[e->slot])
        (void)g5_compositor_bridge_detach(&scene->bridge, e->slot);
    if (e->slot < G5_SURFACE_REGISTRY_CAPACITY &&
        scene->frame.registry.entries[e->slot].occupied)
        (void)g5_surface_registry_detach(&scene->frame.registry, e->slot);
    if (e->pending_resize_handle != AURORA_CAP_INVALID &&
        e->pending_resize_buffer && e->owner)
        (void)cap_revoke(&e->owner->capabilities,
                         e->pending_resize_handle);
    if (e->pending_resize_buffer)
        (void)graphics_buffer_release_owner(
            e->pending_resize_buffer,
            e->pending_resize_buffer->generation);
    if (e->user_surface != AURORA_CAP_INVALID && e->owner)
        (void)cap_revoke(&e->owner->capabilities, e->user_surface);
    if (e->user_buffer != AURORA_CAP_INVALID && e->owner)
        (void)cap_revoke(&e->owner->capabilities, e->user_buffer);
    if (e->kernel_surface != AURORA_CAP_INVALID)
        (void)graphics_surface_detach_buffers(&scene->kernel_caps,
                                               e->kernel_surface);
    if (e->kernel_surface != AURORA_CAP_INVALID)
        (void)cap_revoke(&scene->kernel_caps, e->kernel_surface);
    if (e->surface)
        (void)graphics_surface_release_owner(e->surface,
                                              e->surface->generation);
    if (e->buffer)
        (void)graphics_buffer_release_owner(e->buffer,
                                             e->buffer->generation);
    *e = (struct g5_shell_extra_client){0};
    e->slot = UINT32_MAX;
    e->kernel_surface = AURORA_CAP_INVALID;
    e->user_buffer = AURORA_CAP_INVALID;
    e->user_surface = AURORA_CAP_INVALID;
    e->pending_resize_handle = AURORA_CAP_INVALID;
}

bool g5_shell_scene_attach_second(struct g5_shell_scene *scene,
                                   struct aurora_process *owner) {
    if (!scene || !scene->active || !owner ||
        owner == scene->owner || scene->extra.active ||
        scene->extra.owner || !scene->compositor.initialized ||
        !scene->input_router.initialized)
        return false;
    const struct aurora_display_mode *mode = display_mode_at(0u, 0u);
    if (!mode || mode->format.bits_per_pixel != 32u) return false;
    struct g5_shell_extra_client *e = &scene->extra;
    *e = (struct g5_shell_extra_client){0};
    e->slot = UINT32_MAX;
    e->kernel_surface = AURORA_CAP_INVALID;
    e->user_buffer = AURORA_CAP_INVALID;
    e->user_surface = AURORA_CAP_INVALID;
    e->pending_resize_handle = AURORA_CAP_INVALID;
    e->owner = owner;
    e->buffer = graphics_buffer_create(
        G5_SHELL_SCENE_WIDTH, G5_SHELL_SCENE_HEIGHT, &mode->format);
    e->surface = graphics_surface_create();
    if (!e->buffer || !e->surface) goto failure;
    e->user_buffer = graphics_buffer_grant(&owner->capabilities, e->buffer,
                            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE |
                            AURORA_RIGHT_MAP);
    e->user_surface = graphics_surface_grant(&owner->capabilities, e->surface,
                            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE);
    e->kernel_surface = graphics_surface_grant(&scene->kernel_caps, e->surface,
                            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE |
                            AURORA_RIGHT_CONTROL);
    if (e->user_buffer == AURORA_CAP_INVALID ||
        e->user_surface == AURORA_CAP_INVALID ||
        e->kernel_surface == AURORA_CAP_INVALID ||
        !g5_surface_registry_attach(&scene->frame.registry,
                                     &scene->kernel_caps,
                                     e->kernel_surface, &e->slot) ||
        !g5_surface_bridge_configure(
            &scene->frame.registry.entries[e->slot].bridge,
            &scene->frame.registry.session, G5_SHELL_SCENE_WIDTH,
            G5_SHELL_SCENE_HEIGHT, &e->configure_serial) ||
        !g5_surface_bridge_ack(
            &scene->frame.registry.entries[e->slot].bridge,
            &scene->frame.registry.session, e->configure_serial) ||
        !window_policy_create_toplevel(&scene->window_policy,
                                        e->surface, &e->window_id))
        goto failure;
    uint64_t policy_serial = 0u, node = 0u;
    struct aurora_window_placement place = {0};
    if (!window_policy_configure(&scene->window_policy, e->window_id,
                                 G5_SHELL_SCENE_WIDTH,G5_SHELL_SCENE_HEIGHT,
                                 0u,&policy_serial) ||
        !window_policy_ack_configure(&scene->window_policy,
                                     e->window_id,policy_serial) ||
        !window_policy_place_initial(&scene->window_policy,e->window_id,
                                     G5_SHELL_SCENE_WIDTH,
                                     G5_SHELL_SCENE_HEIGHT,&place) ||
        !g5_compositor_bridge_attach(&scene->bridge,e->slot,
                                      place.x,place.y,place.z,&node) ||
        !graphics_input_register_target(&scene->input_router,
                                        e->window_id,node) ||
        !graphics_input_bind_window_target(&scene->input_router,
                                           e->window_id,e->window_id))
        goto failure;
    e->width = G5_SHELL_SCENE_WIDTH;
    e->height = G5_SHELL_SCENE_HEIGHT;
    e->active = true;
    return true;
failure:
    g5_shell_scene_detach_second(scene);
    return false;
}

bool g5_shell_scene_route_input(
    struct g5_shell_scene *scene,
    const struct aurora_input_event *event
) {
    if (scene == NULL || !scene->active || event == NULL ||
        !scene->input_router.initialized ||
        !g5_session_context_authorized(&scene->frame.registry.session,
                                       scene->generation))
        return false;
    return graphics_input_route_event(&scene->input_router,event);
}

bool g5_shell_scene_poll_input_second(
    struct g5_shell_scene *scene,
    struct aurora_process *sender,
    struct aurora_input_event *out_event
) {
    if (scene == NULL || !scene->active || !scene->extra.active ||
        sender == NULL || sender != scene->extra.owner ||
        out_event == NULL || !scene->input_router.initialized ||
        !g5_session_context_authorized(&scene->frame.registry.session,
                                       scene->generation))
        return false;
    return graphics_input_poll_target(&scene->input_router,
                                      scene->extra.window_id,out_event);
}

bool g5_shell_scene_close_second(
    struct g5_shell_scene *scene,
    struct aurora_process *sender
) {
    if (!scene || !scene->active || !scene->extra.active ||
        sender == NULL || sender != scene->extra.owner ||
        !g5_session_context_authorized(&scene->frame.registry.session,
                                       scene->generation))
        return false;
    g5_shell_scene_detach_second(scene);
    return true;
}

bool g5_shell_scene_configure_second(
    struct g5_shell_scene *scene,
    struct aurora_process *sender,
    uint32_t width, uint32_t height,
    uint64_t *out_serial
) {
    if (out_serial) *out_serial = 0u;
    if (!scene || !scene->active || !scene->extra.active ||
        !sender || sender != scene->extra.owner || !out_serial ||
        width == 0u || height == 0u ||
        width > scene->window_policy.output_width ||
        height > scene->window_policy.output_height ||
        scene->extra.pending_resize_buffer != NULL ||
        scene->extra.slot >= G5_SURFACE_REGISTRY_CAPACITY)
        return false;
    /* Do not overwrite an unacknowledged configure. After ACK, the
     * client must publish a frame for the current dimensions first. */
    if (!window_policy_configure_ready(&scene->window_policy,
                                        scene->extra.window_id,
                                        scene->extra.width,
                                        scene->extra.height))
        return false;
    struct aurora_window_toplevel placement = {0};
    if (!window_policy_read_toplevel(&scene->window_policy,
                                     scene->extra.window_id, &placement) ||
        placement.placement.x < 0 || placement.placement.y < 0 ||
        (uint64_t)(uint32_t)placement.placement.x + width >
            scene->window_policy.output_width ||
        (uint64_t)(uint32_t)placement.placement.y + height >
            scene->window_policy.output_height)
        return false;
    struct g5_surface_bridge *bridge =
        &scene->frame.registry.entries[scene->extra.slot].bridge;
    if (!g5_session_context_authorized(&scene->frame.registry.session,
                                       scene->generation))
        return false;
    /* Update both configuration authorities. A failed second stage
     * cannot be presented; revoke the extra client rather than keep
     * policy and surface geometry in divergent states. */
    uint64_t policy_serial = 0u;
    if (!g5_surface_bridge_configure(bridge,
                                     &scene->frame.registry.session,
                                     width,height,out_serial))
        return false;
    if (!window_policy_configure(&scene->window_policy,
                                 scene->extra.window_id,
                                 width,height,0u,&policy_serial)) {
        g5_shell_scene_detach_second(scene);
        *out_serial = 0u;
        return false;
    }
    scene->extra.configure_serial = *out_serial;
    scene->extra.width = width;
    scene->extra.height = height;
    /* Never retain a capture or focused queue across new geometry. */
    (void)graphics_input_sync_window_focus(&scene->input_router);
    return true;
}

bool g5_shell_scene_allocate_resize_buffer_second(
    struct g5_shell_scene *scene,
    struct aurora_process *sender,
    aurora_cap_handle *out_buffer
) {
    if (out_buffer) *out_buffer = AURORA_CAP_INVALID;
    if (!scene || !scene->active || !scene->extra.active ||
        !sender || sender != scene->extra.owner || !out_buffer ||
        scene->extra.pending_resize_buffer != NULL ||
        scene->extra.width == 0u || scene->extra.height == 0u ||
        scene->extra.slot >= G5_SURFACE_REGISTRY_CAPACITY ||
        g5_surface_configure_ready(
            &scene->frame.registry.entries[scene->extra.slot].bridge.configure,
            &scene->frame.registry.session))
        return false;
    const struct aurora_display_mode *mode = display_mode_at(0u,0u);
    if (!mode || !g5_session_context_authorized(
            &scene->frame.registry.session,scene->generation))
        return false;
    struct aurora_graphics_buffer *buffer =
        graphics_buffer_create(scene->extra.width,scene->extra.height,
                               &mode->format);
    if (!buffer) return false;
    aurora_cap_handle handle = graphics_buffer_grant(
        &sender->capabilities,buffer,
        AURORA_RIGHT_READ|AURORA_RIGHT_WRITE|AURORA_RIGHT_MAP);
    if (handle == AURORA_CAP_INVALID) {
        (void)graphics_buffer_release_owner(buffer,buffer->generation);
        return false;
    }
    scene->extra.pending_resize_buffer = buffer;
    scene->extra.pending_resize_handle = handle;
    *out_buffer = handle;
    return true;
}

bool g5_shell_scene_ack_second(
    struct g5_shell_scene *scene,
    struct aurora_process *sender,
    uint64_t serial
) {
    if (!scene || !scene->active || !scene->extra.active ||
        !sender || sender != scene->extra.owner ||
        serial == 0u || serial != scene->extra.configure_serial ||
        scene->extra.slot >= G5_SURFACE_REGISTRY_CAPACITY)
        return false;
    struct g5_surface_bridge *bridge =
        &scene->frame.registry.entries[scene->extra.slot].bridge;
    if (!g5_surface_bridge_ack(bridge,
                              &scene->frame.registry.session,serial))
        return false;
    struct aurora_window_toplevel window;
    if (!window_policy_read_toplevel(&scene->window_policy,
                                     scene->extra.window_id,&window))
        return false;
    return window_policy_ack_configure(&scene->window_policy,
                                       scene->extra.window_id,
                                       window.pending_configure.serial);
}

bool g5_shell_scene_move_second(
    struct g5_shell_scene *scene,
    struct aurora_process *sender,
    int32_t x,
    int32_t y
) {
    if (!scene || !scene->active || !scene->extra.active ||
        !sender || sender != scene->extra.owner ||
        scene->extra.last_display_serial == 0u ||
        scene->extra.slot >= G5_SURFACE_REGISTRY_CAPACITY ||
        !g5_session_context_authorized(&scene->frame.registry.session,
                                       scene->generation))
        return false;
    struct aurora_window_toplevel w;
    if (!window_policy_read_toplevel(&scene->window_policy,
                                     scene->extra.window_id,&w) ||
        !window_policy_configure_ready(&scene->window_policy,
                                        scene->extra.window_id,
                                        scene->extra.width,
                                        scene->extra.height))
        return false;
    uint64_t node = scene->bridge.node_ids[scene->extra.slot];
    if (!node || !window_policy_move(&scene->window_policy,
                                     scene->extra.window_id,x,y,
                                     scene->extra.width,
                                     scene->extra.height))
        return false;
    if (!software_compositor_set_node(&scene->compositor,node,x,y,
                                      w.placement.z,255u,true)) {
        (void)window_policy_move(&scene->window_policy,
                                 scene->extra.window_id,
                                 w.placement.x,w.placement.y,
                                 scene->extra.width,scene->extra.height);
        return false;
    }
    uint64_t serial = 0u;
    if (!software_compositor_compose_present(&scene->compositor,&serial)) {
        (void)software_compositor_set_node(&scene->compositor,node,
                w.placement.x,w.placement.y,w.placement.z,255u,true);
        (void)window_policy_move(&scene->window_policy,
                scene->extra.window_id,w.placement.x,w.placement.y,
                scene->extra.width,scene->extra.height);
        return false;
    }
    return true;
}

bool g5_shell_scene_publish_second(
    struct g5_shell_scene *scene,
    struct aurora_process *sender,
    uint64_t request_id,
    uint64_t commit_serial,
    uint64_t *out_display_serial
) {
    if (out_display_serial) *out_display_serial = 0u;
    if (!scene || !scene->active || !sender || !out_display_serial ||
        !request_id || !commit_serial || !scene->extra.active ||
        commit_serial <= scene->extra.last_commit_serial ||
        scene->extra.owner != sender || !scene->extra.surface ||
        scene->extra.slot >= G5_SURFACE_REGISTRY_CAPACITY ||
        !g5_session_context_authorized(&scene->frame.registry.session,
                                       scene->generation) ||
        scene->extra.surface->destroy_requested ||
        scene->extra.surface->state != AURORA_GRAPHICS_SURFACE_MAPPED ||
        scene->extra.surface->committed.buffer == NULL ||
        scene->extra.surface->committed.buffer->destroy_requested ||
        (scene->extra.surface->committed.buffer->state !=
             AURORA_GRAPHICS_BUFFER_COMMITTED &&
         scene->extra.surface->committed.buffer->state !=
             AURORA_GRAPHICS_BUFFER_IN_USE) ||
        scene->extra.surface->committed.buffer->width !=
             scene->extra.width ||
        scene->extra.surface->committed.buffer->height !=
             scene->extra.height ||
        (scene->extra.pending_resize_buffer != NULL &&
         scene->extra.surface->committed.buffer !=
             scene->extra.pending_resize_buffer) ||
        !window_policy_configure_ready(&scene->window_policy,
                                       scene->extra.window_id,
                                       scene->extra.width,
                                       scene->extra.height) ||
        scene->extra.surface->committed.commit_serial != commit_serial)
        return false;
    uint64_t config = 0u;
    if (!g5_frame_submission_request(&scene->frame,scene->extra.slot,
                                     request_id,&config) ||
        config != scene->extra.configure_serial)
        return false;
    if (!g5_compositor_bridge_present(&scene->bridge,scene->extra.slot,
                                       request_id,config,out_display_serial) ||
        *out_display_serial == 0u ||
        *out_display_serial <= scene->extra.last_display_serial)
        return false;
    /* Promote only an actually presented replacement buffer. The old
     * handle remains valid until compositor presentation has succeeded. */
    if (scene->extra.pending_resize_buffer != NULL) {
        if (scene->extra.surface->committed.buffer !=
            scene->extra.pending_resize_buffer)
            return false;
        if (scene->extra.user_buffer != AURORA_CAP_INVALID)
            (void)cap_revoke(&sender->capabilities,
                             scene->extra.user_buffer);
        if (scene->extra.buffer)
            (void)graphics_buffer_release_owner(
                scene->extra.buffer,scene->extra.buffer->generation);
        scene->extra.buffer = scene->extra.pending_resize_buffer;
        scene->extra.user_buffer = scene->extra.pending_resize_handle;
        scene->extra.pending_resize_buffer = NULL;
        scene->extra.pending_resize_handle = AURORA_CAP_INVALID;
    }
    scene->extra.last_display_serial = *out_display_serial;
    scene->extra.last_commit_serial = commit_serial;
    return true;
}

bool g5_shell_scene_close(struct g5_shell_scene *scene,
                          const struct g5_ipc_header *header,
                          const uint8_t *payload) {
    if (scene == NULL || !scene->active || scene->surface == NULL ||
        header == NULL || payload == NULL ||
        header->kind != G5_IPC_REQUEST ||
        header->operation != G5_OP_WINDOW_CLOSE ||
        header->payload_bytes != 8u ||
        header->session_generation != scene->generation ||
        header->object_generation != scene->surface->generation ||
        read_u64_le(payload) > 3u ||
        !g5_session_context_authorized(&scene->frame.registry.session,
                                       scene->generation))
        return false;
    g5_shell_scene_end(scene);
    return true;
}

void g5_shell_scene_end(struct g5_shell_scene *scene) {
    if (scene==NULL) return;
    g5_shell_scene_detach_second(scene);
    /* Stop input delivery and unregister target before removing the node. */
    if (scene->input_router.initialized) {
        graphics_input_revoke_session(&scene->input_router);
        scene->input_router.window_policy = NULL;
    }
    /* Revoke trusted window identity before freeing the backing surface. */
    if (scene->window_id != 0u)
        (void)window_policy_destroy_toplevel(&scene->window_policy,
                                             scene->window_id);
    window_policy_reset(&scene->window_policy);
    /* Detach the compositor before invalidating the buffer capability. */
    g5_compositor_bridge_revoke(&scene->bridge);
    g5_frame_delivery_revoke(&scene->delivery);
    g5_frame_submission_end(&scene->frame);
    if (scene->compositor.initialized)
        (void)software_compositor_destroy(&scene->compositor);
    if (scene->surface!=NULL &&
        scene->kernel_surface!=AURORA_CAP_INVALID)
        (void)graphics_surface_detach_buffers(
            &scene->kernel_caps,scene->kernel_surface);
    if (scene->owner!=NULL) {
        if (scene->pending_resize_handle!=AURORA_CAP_INVALID)
            (void)cap_revoke(&scene->owner->capabilities,
                             scene->pending_resize_handle);
        if (scene->user_surface!=AURORA_CAP_INVALID)
            (void)cap_revoke(&scene->owner->capabilities,scene->user_surface);
        if (scene->user_buffer!=AURORA_CAP_INVALID)
            (void)cap_revoke(&scene->owner->capabilities,scene->user_buffer);
    }
    if (scene->kernel_surface!=AURORA_CAP_INVALID)
        (void)cap_revoke(&scene->kernel_caps,scene->kernel_surface);
    if (scene->surface!=NULL)
        (void)graphics_surface_release_owner(
            scene->surface,scene->surface->generation);
    if (scene->pending_resize_buffer!=NULL)
        (void)graphics_buffer_release_owner(
            scene->pending_resize_buffer,
            scene->pending_resize_buffer->generation);
    if (scene->buffer!=NULL)
        (void)graphics_buffer_release_owner(
            scene->buffer,scene->buffer->generation);
    *scene=(struct g5_shell_scene){0};
}

bool g5_shell_scene_begin(struct g5_shell_scene *scene,
                          struct aurora_process *owner,uint64_t generation) {
    if (scene==NULL || owner==NULL || generation==0u ||
        scene->active || scene->owner!=NULL) return false;
    const struct aurora_display_mode *mode=display_mode_at(0u,0u);
    if (mode==NULL || mode->format.bits_per_pixel!=32u) return false;
    *scene=(struct g5_shell_scene){0};
    scene->kernel_surface=AURORA_CAP_INVALID;
    scene->user_surface=AURORA_CAP_INVALID;
    scene->user_buffer=AURORA_CAP_INVALID;
    scene->slot=UINT32_MAX;
    scene->owner=owner;
    scene->generation=generation;
    scene->width=G5_SHELL_SCENE_WIDTH;
    scene->height=G5_SHELL_SCENE_HEIGHT;
    scene->pending_resize_handle=AURORA_CAP_INVALID;
    cap_table_init(&scene->kernel_caps);
    log_line("[g5-wp04-gate] begin scene surface allocation");
    scene->buffer=graphics_buffer_create(
        G5_SHELL_SCENE_WIDTH,G5_SHELL_SCENE_HEIGHT,&mode->format);
    scene->surface=graphics_surface_create();
    if (scene->buffer==NULL || scene->surface==NULL) goto failure;
    scene->user_buffer=graphics_buffer_grant(&owner->capabilities,
        scene->buffer,AURORA_RIGHT_READ|AURORA_RIGHT_WRITE|AURORA_RIGHT_MAP);
    scene->user_surface=graphics_surface_grant(&owner->capabilities,
        scene->surface,AURORA_RIGHT_READ|AURORA_RIGHT_WRITE);
    scene->kernel_surface=graphics_surface_grant(&scene->kernel_caps,
        scene->surface,AURORA_RIGHT_READ|AURORA_RIGHT_WRITE|
                       AURORA_RIGHT_CONTROL);
    if (scene->user_buffer==AURORA_CAP_INVALID ||
        scene->user_surface==AURORA_CAP_INVALID ||
        scene->kernel_surface==AURORA_CAP_INVALID ||
        !g5_frame_submission_begin(&scene->frame,generation) ||
        !g5_surface_registry_attach(&scene->frame.registry,
            &scene->kernel_caps,scene->kernel_surface,&scene->slot) ||
        !g5_surface_bridge_configure(
            &scene->frame.registry.entries[scene->slot].bridge,
            &scene->frame.registry.session,
            G5_SHELL_SCENE_WIDTH,G5_SHELL_SCENE_HEIGHT,
            &scene->configure_serial) ||
        !g5_surface_bridge_ack(
            &scene->frame.registry.entries[scene->slot].bridge,
            &scene->frame.registry.session,scene->configure_serial) ||
        !g5_frame_delivery_bind(&scene->delivery,&scene->frame) ||
        !software_compositor_init(&scene->compositor,0u) ||
        !g5_compositor_bridge_bind(
            &scene->bridge,&scene->delivery,&scene->compositor))
        goto failure;
    log_line("[g5-wp04-gate] scene bridge and compositor initialized");
    uint64_t node=0u;
    if (!g5_compositor_bridge_attach(
            &scene->bridge,scene->slot,48,48,0,&node) || node==0u)
        goto failure;
    log_line("[g5-wp04-gate] scene compositor node attached");
    /* The Ring 3 surface is now also tracked by the trusted WP-04 policy.
     * This preserves the existing verified WP-03 scene/display flow. */
    uint64_t policy_serial = 0u;
    if (!window_policy_init(&scene->window_policy,
                            (uint32_t)mode->width,
                            (uint32_t)mode->height) ||
        !window_policy_create_toplevel(&scene->window_policy,
                                       scene->surface,&scene->window_id) ||
        !window_policy_configure(&scene->window_policy,
                                 scene->window_id,
                                 G5_SHELL_SCENE_WIDTH,
                                 G5_SHELL_SCENE_HEIGHT,0u,&policy_serial) ||
        !window_policy_ack_configure(&scene->window_policy,
                                     scene->window_id,policy_serial))
        goto failure;
    struct aurora_window_placement initial_placement = {0};
    if (!window_policy_place_initial(&scene->window_policy,
                                     scene->window_id,
                                     G5_SHELL_SCENE_WIDTH,
                                     G5_SHELL_SCENE_HEIGHT,
                                     &initial_placement) ||
        !window_policy_move(&scene->window_policy,scene->window_id,
                            48,48,G5_SHELL_SCENE_WIDTH,G5_SHELL_SCENE_HEIGHT))
        goto failure;
    log_line("[g5-wp04-gate] scene window policy registered");
    /* The trusted receiver registers its real compositor node, surface
     * and window as one input authority. No Ring 3 ID is trusted here. */
    if (!graphics_input_router_init(&scene->input_router,
                                    &scene->compositor) ||
        !graphics_input_register_target(&scene->input_router,
                                         scene->window_id,node) ||
        !graphics_input_bind_window_policy(&scene->input_router,
                                           &scene->window_policy) ||
        !graphics_input_bind_window_target(&scene->input_router,
                                           scene->window_id,
                                           scene->window_id))
        goto failure;
    log_line("[g5-wp04-gate] scene input router registered");
    scene->x=48;
    scene->y=48;
    scene->active=true;
    return true;
failure:
    log_line("[g5-wp04-gate] scene initialization failed; revoking");
    g5_shell_scene_end(scene);
    return false;
}

bool g5_shell_scene_configure_primary(struct g5_shell_scene *scene,
                                      uint32_t width,uint32_t height,
                                      uint64_t *out_serial) {
    if (out_serial) *out_serial=0u;
    if (!scene || !scene->active || !scene->owner || !out_serial ||
        scene->pending_resize_buffer || !width || !height ||
        width>scene->window_policy.output_width ||
        height>scene->window_policy.output_height ||
        scene->x<0 || scene->y<0 ||
        (uint64_t)(uint32_t)scene->x+width>
            scene->window_policy.output_width ||
        (uint64_t)(uint32_t)scene->y+height>
            scene->window_policy.output_height ||
        scene->slot>=G5_SURFACE_REGISTRY_CAPACITY ||
        !window_policy_configure_ready(
            &scene->window_policy,scene->window_id,
            scene->width,scene->height) ||
        !g5_session_context_authorized(
            &scene->frame.registry.session,scene->generation))
        return false;
    struct g5_surface_bridge *bridge=
        &scene->frame.registry.entries[scene->slot].bridge;
    if (!g5_surface_bridge_configure(
            bridge,&scene->frame.registry.session,width,height,out_serial))
        return false;
    uint64_t policy_serial=0u;
    if (!window_policy_configure(
            &scene->window_policy,scene->window_id,width,height,
            AURORA_WINDOW_STATE_NONE,&policy_serial)) {
        g5_shell_scene_end(scene); /* fail closed on divergent configures */
        *out_serial=0u;
        return false;
    }
    scene->configure_serial=*out_serial;
    scene->width=width;
    scene->height=height;
    (void)graphics_input_sync_window_focus(&scene->input_router);
    return true;
}

bool g5_shell_scene_allocate_resize_buffer_primary(
    struct g5_shell_scene *scene,aurora_cap_handle *out_handle) {
    if (out_handle) *out_handle=AURORA_CAP_INVALID;
    if (!scene || !scene->active || !scene->owner || !out_handle ||
        scene->pending_resize_buffer || scene->slot>=G5_SURFACE_REGISTRY_CAPACITY ||
        !scene->width || !scene->height ||
        g5_surface_configure_ready(
            &scene->frame.registry.entries[scene->slot].bridge.configure,
            &scene->frame.registry.session))
        return false;
    const struct aurora_display_mode *mode=display_mode_at(0u,0u);
    if (!mode || !g5_session_context_authorized(
            &scene->frame.registry.session,scene->generation))
        return false;
    struct aurora_graphics_buffer *buffer=
        graphics_buffer_create(scene->width,scene->height,&mode->format);
    if (!buffer) return false;
    aurora_cap_handle handle=graphics_buffer_grant(
        &scene->owner->capabilities,buffer,
        AURORA_RIGHT_READ|AURORA_RIGHT_WRITE|AURORA_RIGHT_MAP);
    if (handle==AURORA_CAP_INVALID) {
        (void)graphics_buffer_release_owner(buffer,buffer->generation);
        return false;
    }
    scene->pending_resize_buffer=buffer;
    scene->pending_resize_handle=handle;
    *out_handle=handle;
    return true;
}

bool g5_shell_scene_ack_primary(struct g5_shell_scene *scene,
                                uint64_t serial) {
    if (!scene || !scene->active || !scene->owner ||
        !serial || serial!=scene->configure_serial ||
        scene->slot>=G5_SURFACE_REGISTRY_CAPACITY)
        return false;
    struct g5_surface_bridge *bridge=
        &scene->frame.registry.entries[scene->slot].bridge;
    if (!g5_surface_bridge_ack(
            bridge,&scene->frame.registry.session,serial))
        return false;
    struct aurora_window_toplevel window;
    if (!window_policy_read_toplevel(&scene->window_policy,
                                     scene->window_id,&window))
        return false;
    return window_policy_ack_configure(
        &scene->window_policy,scene->window_id,
        window.pending_configure.serial);
}

bool g5_shell_scene_publish_primary_resized(struct g5_shell_scene *scene,
                                           uint64_t request_id,
                                           uint64_t commit_serial) {
    if (!scene || !scene->active || !scene->surface ||
        !scene->pending_resize_buffer || !request_id || !commit_serial)
        return false;
    struct g5_ipc_header header={0};
    header.kind=G5_IPC_REQUEST;
    header.operation=G5_OP_SCENE_PUBLISH;
    header.payload_bytes=16u;
    header.request_id=request_id;
    header.session_generation=scene->generation;
    header.object_generation=scene->surface->generation;
    uint8_t payload[16]={0};
    uint64_t fields[2]={scene->surface->object_id,commit_serial};
    for (uint32_t field=0u;field<2u;++field)
        for (uint32_t byte=0u;byte<8u;++byte)
            payload[field*8u+byte]=(uint8_t)(fields[field]>>(8u*byte));
    return g5_shell_scene_publish(scene,&header,payload);
}

bool g5_shell_scene_publish(struct g5_shell_scene *scene,
                            const struct g5_ipc_header *header,
                            const uint8_t *payload) {
    if (scene==NULL || !scene->active || !scene->owner ||
        !scene->surface || !header || !payload ||
        header->operation!=G5_OP_SCENE_PUBLISH ||
        header->session_generation!=scene->generation ||
        header->object_generation!=scene->surface->generation ||
        header->payload_bytes!=16u ||
        !g5_session_context_authorized(&scene->frame.registry.session,
                                       scene->generation)) {
        log_line("[g5-shell-diagnostic] scene publish failed identity/session guard");
        return false;
    }
    uint64_t object_id=read_u64_le(payload);
    uint64_t commit_serial=read_u64_le(payload+8u);
    if (object_id!=scene->surface->object_id ||
        commit_serial==0u ||
        commit_serial<=scene->last_commit_serial ||
        scene->surface->committed.commit_serial!=commit_serial ||
        scene->surface->state!=AURORA_GRAPHICS_SURFACE_MAPPED ||
        scene->surface->committed.buffer==NULL ||
        scene->surface->committed.buffer->destroy_requested ||
        scene->surface->committed.buffer->width!=scene->width ||
        scene->surface->committed.buffer->height!=scene->height ||
        (scene->pending_resize_buffer!=NULL &&
         scene->surface->committed.buffer!=scene->pending_resize_buffer) ||
        !window_policy_configure_ready(
            &scene->window_policy,scene->window_id,
            scene->width,scene->height)) {
        log_write("[g5-shell-diagnostic] scene commit mismatch ID/commit/actual: ");
        log_u64(object_id);log_write("/");
        log_u64(commit_serial);log_write("/");
        log_u64(scene->surface->committed.commit_serial);log_line("");
        return false;
    }
    uint64_t config=0u,serial=0u;
    if (!g5_frame_submission_request(
            &scene->frame,scene->slot,header->request_id,&config)) {
        log_line("[g5-shell-diagnostic] scene frame submission rejected");
        return false;
    }
    if (config!=scene->configure_serial) {
        log_line("[g5-shell-diagnostic] scene configure serial mismatch");
        return false;
    }
    if (!g5_compositor_bridge_present(
            &scene->bridge,scene->slot,header->request_id,config,&serial)) {
        log_line("[g5-shell-diagnostic] scene compositor bridge present failed");
        return false;
    }
    if (serial==0u || serial<=scene->last_display_serial) {
        log_line("[g5-shell-diagnostic] scene display serial missing/stale");
        return false;
    }
    /* Do not revoke old buffer access until a frame with the new buffer
     * was accepted by the compositor and actually displayed. */
    if (scene->pending_resize_buffer) {
        if (scene->user_buffer!=AURORA_CAP_INVALID)
            (void)cap_revoke(&scene->owner->capabilities,
                             scene->user_buffer);
        if (scene->buffer)
            (void)graphics_buffer_release_owner(
                scene->buffer,scene->buffer->generation);
        scene->buffer=scene->pending_resize_buffer;
        scene->user_buffer=scene->pending_resize_handle;
        scene->pending_resize_buffer=NULL;
        scene->pending_resize_handle=AURORA_CAP_INVALID;
    }
    scene->last_display_serial=serial;
    scene->last_commit_serial=commit_serial;
    return true;
}

bool g5_shell_scene_move_primary(struct g5_shell_scene *scene,
                                 int32_t x,int32_t y) {
    if (!scene || !scene->active || !scene->surface ||
        scene->generation==0u) return false;
    struct g5_ipc_header request = {0};
    request.kind=G5_IPC_REQUEST;
    request.operation=G5_OP_WINDOW_PLACE;
    request.payload_bytes=24u;
    request.session_generation=scene->generation;
    request.object_generation=scene->surface->generation;
    uint8_t payload[24]={0};
    uint32_t fields[4]={(uint32_t)x,(uint32_t)y,
                        scene->width,scene->height};
    for (uint32_t field=0u;field<4u;++field)
        for (uint32_t byte=0u;byte<4u;++byte)
            payload[field*4u+byte]=(uint8_t)(fields[field]>>(8u*byte));
    for (uint32_t byte=0u;byte<8u;++byte)
        payload[16u+byte]=(uint8_t)(
            scene->surface->object_id>>(8u*byte));
    return g5_shell_scene_place(scene,&request,payload);
}

bool g5_shell_scene_place(struct g5_shell_scene *scene,
                          const struct g5_ipc_header *header,
                          const uint8_t *payload) {
    if (!scene || !scene->active || !scene->surface ||
        !header || !payload || !scene->last_display_serial ||
        header->operation!=G5_OP_WINDOW_PLACE ||
        header->payload_bytes!=24u ||
        header->session_generation!=scene->generation ||
        header->object_generation!=scene->surface->generation ||
        read_u64_le(payload+16u)!=scene->surface->object_id ||
        read_u32_le(payload+8u)!=scene->width ||
        read_u32_le(payload+12u)!=scene->height)
        return false;
    int32_t x=(int32_t)read_u32_le(payload);
    int32_t y=(int32_t)read_u32_le(payload+4u);
    uint64_t node=scene->bridge.node_ids[scene->slot];
    if (!node || !g5_session_context_authorized(
            &scene->frame.registry.session,scene->generation))
        return false;
    int32_t old_x=scene->x,old_y=scene->y;
    if (!window_policy_configure_ready(&scene->window_policy,
                                        scene->window_id,
                                        scene->width,
                                        scene->height) ||
        x < 0 || y < 0 ||
        (uint64_t)(uint32_t)x + scene->width >
            scene->window_policy.output_width ||
        (uint64_t)(uint32_t)y + scene->height >
            scene->window_policy.output_height)
        return false;
    /* Mutate the trusted policy before publishing pixels. A failed
     * compositor transaction restores both the old policy placement
     * and compositor position; never present an untracked move. */
    if (!window_policy_move(&scene->window_policy,scene->window_id,
                            x,y,scene->width,scene->height))
        return false;
    if (!software_compositor_set_node(
            &scene->compositor,node,x,y,0,255u,true)) {
        (void)window_policy_move(&scene->window_policy,scene->window_id,
                                 old_x,old_y,
                                 scene->width,scene->height);
        return false;
    }
    uint64_t serial=0u;
    if (!software_compositor_compose_present(
            &scene->compositor,&serial) ||
        serial<=scene->last_display_serial) {
        (void)software_compositor_set_node(
            &scene->compositor,node,old_x,old_y,0,255u,true);
        (void)window_policy_move(&scene->window_policy,scene->window_id,
                                 old_x,old_y,
                                 scene->width,scene->height);
        return false;
    }
    scene->x=x;
    scene->y=y;
    scene->last_display_serial=serial;
    return true;
}
