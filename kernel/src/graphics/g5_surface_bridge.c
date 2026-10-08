#include <aurora/g5_surface_bridge.h>
#include <aurora/capability_abi.h>
#include <stddef.h>

static bool get_surface(struct g5_surface_bridge *b,
    const struct g5_session_context *session, uint64_t rights,
    struct aurora_graphics_surface **out) {
    if (!b || !b->owner_caps || !b->surface_handle || !out ||
        !g5_session_context_authorized(session,b->configure.session_generation) ||
        !b->configure.active ||
        session->revision != b->configure.session_revision ||
        !graphics_surface_lookup(b->owner_caps,b->surface_handle,rights,out))
        return false;
    return (*out)->object_id==b->object_id &&
        (*out)->generation==b->object_generation &&
        !(*out)->destroy_requested &&
        (*out)->state!=AURORA_GRAPHICS_SURFACE_FREE;
}

bool g5_surface_bridge_bind(struct g5_surface_bridge *b,
    const struct g5_session_context *session, struct aurora_cap_table *caps,
    aurora_cap_handle handle) {
    struct aurora_graphics_surface *surface=NULL;
    if (!b || !caps || handle==AURORA_CAP_INVALID || b->configure.active ||
        !session || !g5_session_context_authorized(session,session->generation) ||
        !graphics_surface_lookup(caps,handle,AURORA_RIGHT_READ,&surface) ||
        surface->destroy_requested ||
        surface->state==AURORA_GRAPHICS_SURFACE_FREE ||
        !surface->generation || !surface->object_id) return false;
    if (!g5_surface_configure_bind(&b->configure,session,surface->generation))
        return false;
    b->owner_caps=caps;
    b->surface_handle=handle;
    b->object_id=surface->object_id;
    b->object_generation=surface->generation;
    return true;
}

bool g5_surface_bridge_configure(struct g5_surface_bridge *b,
    const struct g5_session_context *session,uint32_t width,uint32_t height,
    uint64_t *out_serial) {
    struct aurora_graphics_surface *surface=NULL;
    if (out_serial) *out_serial=0;
    return get_surface(b,session,AURORA_RIGHT_CONTROL,&surface) &&
        g5_surface_configure_issue(&b->configure,session,width,height,out_serial);
}
bool g5_surface_bridge_ack(struct g5_surface_bridge *b,
    const struct g5_session_context *session,uint64_t serial) {
    struct aurora_graphics_surface *surface=NULL;
    return get_surface(b,session,AURORA_RIGHT_READ,&surface) &&
        g5_surface_configure_ack(&b->configure,session,serial,b->object_generation);
}
bool g5_surface_bridge_read_committed(struct g5_surface_bridge *b,
    const struct g5_session_context *session,
    struct aurora_graphics_surface_snapshot *out) {
    struct aurora_graphics_surface *surface=NULL;
    if (!out || !get_surface(b,session,AURORA_RIGHT_READ,&surface) ||
        !g5_surface_configure_ready(&b->configure,session)) return false;
    return graphics_surface_read_committed(b->owner_caps,b->surface_handle,out);
}
void g5_surface_bridge_revoke(struct g5_surface_bridge *b) {
    if (!b) return;
    g5_surface_configure_revoke(&b->configure);
    b->owner_caps=NULL;
    b->surface_handle=AURORA_CAP_INVALID;
    b->object_id=0;
    b->object_generation=0;
}
