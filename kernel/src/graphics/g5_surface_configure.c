#include <aurora/g5_surface_configure.h>
#include <stddef.h>
#include <stdint.h>

static bool live(const struct g5_surface_configure *s,
                 const struct g5_session_context *ctx) {
    return s != NULL && s->active && ctx != NULL &&
        g5_session_context_authorized(ctx,s->session_generation) &&
        ctx->revision == s->session_revision;
}

bool g5_surface_configure_bind(struct g5_surface_configure *s,
    const struct g5_session_context *ctx,uint64_t object_generation) {
    if(s==NULL || ctx==NULL || !ctx->active || !ctx->generation ||
       !ctx->revision || !object_generation || s->active) return false;
    *s=(struct g5_surface_configure){0};
    s->session_generation=ctx->generation;
    s->session_revision=ctx->revision;
    s->object_generation=object_generation;
    s->next_serial=1;
    s->active=true;
    return true;
}

bool g5_surface_configure_issue(struct g5_surface_configure *s,
    const struct g5_session_context *ctx,
    uint32_t width,uint32_t height,uint64_t *out_serial) {
    if(out_serial != NULL)*out_serial=0;
    if(!live(s,ctx) || out_serial==NULL ||
       !width || width>G5_SURFACE_MAX_DIMENSION ||
       !height || height>G5_SURFACE_MAX_DIMENSION ||
       s->next_serial==UINT64_MAX || !s->next_serial) return false;
    const uint64_t serial=s->next_serial++;
    s->pending_serial=serial;
    s->pending_width=width;
    s->pending_height=height;
    *out_serial=serial;
    return true;
}

bool g5_surface_configure_ack(struct g5_surface_configure *s,
    const struct g5_session_context *ctx,
    uint64_t serial,uint64_t object_generation) {
    if(!live(s,ctx) || !serial || !s->pending_serial ||
       serial!=s->pending_serial ||
       object_generation!=s->object_generation) return false;
    s->accepted_serial=serial;
    s->accepted_width=s->pending_width;
    s->accepted_height=s->pending_height;
    s->pending_serial=0;
    s->pending_width=0;
    s->pending_height=0;
    return true;
}

bool g5_surface_configure_ready(const struct g5_surface_configure *s,
    const struct g5_session_context *ctx) {
    return live(s,ctx) && s->accepted_serial!=0 && !s->pending_serial;
}

void g5_surface_configure_revoke(struct g5_surface_configure *s) {
    if(s==NULL)return;
    *s=(struct g5_surface_configure){0};
}
