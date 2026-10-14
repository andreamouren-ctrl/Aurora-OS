#include <aurora/g5_canvas_session_camera.h>
#include <math.h>
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
