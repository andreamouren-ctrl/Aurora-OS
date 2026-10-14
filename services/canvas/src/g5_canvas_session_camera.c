#include <aurora/g5_canvas_session_camera.h>
#include <math.h>
void g5_canvas_session_camera_revoke(struct g5_canvas_session_camera *s) {
    if (!s) return;
    *s=(struct g5_canvas_session_camera){0};
}
static bool authorized(const struct g5_canvas_session_camera *s,
    uint64_t session,uint64_t scene,uint64_t camera) {
    return s && s->session_generation!=0 && s->scene_revision!=0 &&
        session==s->session_generation && scene==s->scene_revision &&
        camera==s->camera.revision && g5_camera_valid(&s->camera);
}
bool g5_canvas_session_camera_init(struct g5_canvas_session_camera *s,
    uint64_t session,uint64_t scene,struct g5_world_point origin,
    uint32_t width,uint32_t height) {
    if (!s || !session || !scene) return false;
    struct g5_canvas_session_camera next={0};
    if (!g5_camera_init(&next.camera,origin,width,height,1.0,1.0))
        return false;
    next.session_generation=session;
    next.scene_revision=scene;
    *s=next;
    return true;
}
bool g5_canvas_session_camera_pan(struct g5_canvas_session_camera *s,
    uint64_t session,uint64_t scene,uint64_t camera,double dx,double dy) {
    if (!authorized(s,session,scene,camera) ||
        !isfinite(dx) || !isfinite(dy) || (dx==0.0 && dy==0.0))
        return false;
    struct g5_canvas_session_camera next=*s;
    if (!g5_camera_pan(&next.camera,dx,dy)) return false;
    *s=next;
    return true;
}
bool g5_canvas_session_camera_zoom(struct g5_canvas_session_camera *s,
    uint64_t session,uint64_t scene,uint64_t camera,
    double factor,double x,double y) {
    if (!authorized(s,session,scene,camera) ||
        !isfinite(factor) || factor==1.0) return false;
    struct g5_canvas_session_camera next=*s;
    if (!g5_camera_zoom_at(&next.camera,factor,x,y)) return false;
    *s=next;
    return true;
}

bool g5_canvas_session_camera_stage_pan(
    const struct g5_canvas_session_camera *current,
    uint64_t session,uint64_t scene,uint64_t camera,
    double dx,double dy,struct g5_canvas_session_camera *staged) {
    if (!staged || staged==current ||
        !authorized(current,session,scene,camera) ||
        !isfinite(dx) || !isfinite(dy) || (dx==0.0 && dy==0.0))
        return false;
    struct g5_canvas_session_camera next=*current;
    if (!g5_camera_pan(&next.camera,dx,dy)) return false;
    *staged=next;
    return true;
}
bool g5_canvas_session_camera_stage_zoom(
    const struct g5_canvas_session_camera *current,
    uint64_t session,uint64_t scene,uint64_t camera,
    double factor,double x,double y,
    struct g5_canvas_session_camera *staged) {
    if (!staged || staged==current ||
        !authorized(current,session,scene,camera) ||
        !isfinite(factor) || factor==1.0)
        return false;
    struct g5_canvas_session_camera next=*current;
    if (!g5_camera_zoom_at(&next.camera,factor,x,y)) return false;
    *staged=next;
    return true;
}
bool g5_canvas_session_camera_commit(
    struct g5_canvas_session_camera *current,
    const struct g5_canvas_session_camera *staged,
    uint64_t session,uint64_t scene,uint64_t camera) {
    if (!current || !staged || current==staged ||
        !authorized(current,session,scene,camera) ||
        staged->session_generation!=session ||
        staged->scene_revision!=scene ||
        !g5_camera_valid(&staged->camera) ||
        camera==UINT64_MAX || staged->camera.revision!=camera+1u)
        return false;
    *current=*staged;
    return true;
}

bool g5_canvas_session_camera_project_nodes(
    const struct g5_canvas_session_camera *state,
    uint64_t session,uint64_t scene,uint64_t camera,
    const struct g5_world_point *points,size_t count,
    int32_t *out_x,int32_t *out_y,size_t capacity) {
    /* The existing software compositor is bounded to 64 scene nodes. */
    int32_t x[64],y[64];
    if (!authorized(state,session,scene,camera) || count>64 ||
        count>capacity || (count && (!points || !out_x || !out_y)) ||
        (count && out_x==out_y))
        return false;
    for (size_t i=0;i<count;++i) {
        if (!g5_camera_world_to_screen(&state->camera,points[i],
                                        &x[i],&y[i]))
            return false;
    }
    for (size_t i=0;i<count;++i) {
        out_x[i]=x[i];
        out_y[i]=y[i];
    }
    return true;
}
bool g5_canvas_session_camera_present_pan(
    struct g5_canvas_session_camera *current,
    uint64_t session,uint64_t scene,uint64_t camera,
    double dx,double dy,g5_canvas_session_present_fn present,void *context) {
    struct g5_canvas_session_camera staged;
    if (!present ||
        !g5_canvas_session_camera_stage_pan(current,session,scene,camera,
                                             dx,dy,&staged))
        return false;
    /* A synchronous presenter may fail; no camera state is published then.
     * The caller must serialize presentation against session teardown. */
    if (!present(context,&staged)) return false;
    return g5_canvas_session_camera_commit(current,&staged,
                                            session,scene,camera);
}
bool g5_canvas_session_camera_present_zoom(
    struct g5_canvas_session_camera *current,
    uint64_t session,uint64_t scene,uint64_t camera,
    double factor,double x,double y,
    g5_canvas_session_present_fn present,void *context) {
    struct g5_canvas_session_camera staged;
    if (!present ||
        !g5_canvas_session_camera_stage_zoom(current,session,scene,camera,
                                              factor,x,y,&staged))
        return false;
    /* A synchronous presenter may fail; no camera state is published then.
     * The caller must serialize presentation against session teardown. */
    if (!present(context,&staged)) return false;
    return g5_canvas_session_camera_commit(current,&staged,
                                            session,scene,camera);
}
