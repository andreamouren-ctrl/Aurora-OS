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
    if (e->window_id)
        (void)graphics_input_unregister_target(&scene->input_router,
                                                 e->window_id);
    if (e->window_id)
        (void)window_policy_destroy_toplevel(&scene->window_policy,
                                              e->window_id);
    if (e->slot < G5_SURFACE_REGISTRY_CAPACITY &&
        scene->bridge.node_ids[e->slot])
        (void)g5_compositor_bridge_detach(&scene->bridge, e->slot);
    if (e->slot < G5_SURFACE_REGISTRY_CAPACITY &&
        scene->frame.registry.entries[e->slot].occupied)
        (void)g5_surface_registry_detach(&scene->frame.registry, e->slot);
    if (e->user_surface != AURORA_CAP_INVALID && e->owner)
        (void)cap_revoke(&e->owner->capabilities, e->user_surface);
    if (e->user_buffer != AURORA_CAP_INVALID && e->owner)
        (void)cap_revoke(&e->owner->capabilities, e->user_buffer);
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
    e->active = true;
    return true;
failure:
    g5_shell_scene_detach_second(scene);
    return false;
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
    cap_table_init(&scene->kernel_caps);
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
    uint64_t node=0u;
    if (!g5_compositor_bridge_attach(
            &scene->bridge,scene->slot,48,48,0,&node) || node==0u)
        goto failure;
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
    scene->x=48;
    scene->y=48;
    scene->active=true;
    return true;
failure:
    g5_shell_scene_end(scene);
    return false;
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
        scene->surface->committed.commit_serial!=commit_serial) {
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
    scene->last_display_serial=serial;
    return true;
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
        read_u32_le(payload+8u)!=G5_SHELL_SCENE_WIDTH ||
        read_u32_le(payload+12u)!=G5_SHELL_SCENE_HEIGHT)
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
                                        G5_SHELL_SCENE_WIDTH,
                                        G5_SHELL_SCENE_HEIGHT) ||
        x < 0 || y < 0 ||
        (uint64_t)(uint32_t)x + G5_SHELL_SCENE_WIDTH >
            scene->window_policy.output_width ||
        (uint64_t)(uint32_t)y + G5_SHELL_SCENE_HEIGHT >
            scene->window_policy.output_height)
        return false;
    /* Mutate the trusted policy before publishing pixels. A failed
     * compositor transaction restores both the old policy placement
     * and compositor position; never present an untracked move. */
    if (!window_policy_move(&scene->window_policy,scene->window_id,
                            x,y,G5_SHELL_SCENE_WIDTH,G5_SHELL_SCENE_HEIGHT))
        return false;
    if (!software_compositor_set_node(
            &scene->compositor,node,x,y,0,255u,true)) {
        (void)window_policy_move(&scene->window_policy,scene->window_id,
                                 old_x,old_y,
                                 G5_SHELL_SCENE_WIDTH,G5_SHELL_SCENE_HEIGHT);
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
                                 G5_SHELL_SCENE_WIDTH,G5_SHELL_SCENE_HEIGHT);
        return false;
    }
    scene->x=x;
    scene->y=y;
    scene->last_display_serial=serial;
    return true;
}
