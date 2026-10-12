#include <aurora/g5_canvas_camera.h>
#include <math.h>
#include <limits.h>

/* __int128 is intentionally confined to user-space implementation. Explicit
 * GCC/Clang extension prevents undefined signed int64 overflow before float
 * conversion, including opposite INT64_MIN / INT64_MAX endpoints. */
__extension__ typedef __int128 g5_wide_t;

static bool valid_size(uint32_t w, uint32_t h) {
    return w > 0u && h > 0u &&
           w <= G5_CAMERA_MAX_VIEWPORT && h <= G5_CAMERA_MAX_VIEWPORT;
}
static bool valid_zoom(double z) {
    return isfinite(z) && z >= G5_CAMERA_MIN_ZOOM &&
           z <= G5_CAMERA_MAX_ZOOM;
}
static bool valid_scale(double d) {
    return isfinite(d) && d >= G5_CAMERA_MIN_DEVICE_SCALE &&
           d <= G5_CAMERA_MAX_DEVICE_SCALE;
}
static bool advance_revision(struct g5_camera *c) {
    if (c->revision == UINT64_MAX) return false;
    ++c->revision;
    return true;
}
bool g5_world_add_checked(int64_t a, int64_t b, int64_t *out) {
    if (!out) return false;
    g5_wide_t sum=(g5_wide_t)a+(g5_wide_t)b;
    if (sum < INT64_MIN || sum > INT64_MAX) return false;
    *out=(int64_t)sum;
    return true;
}
bool g5_world_translate_checked(struct g5_world_point p,
                                int64_t dx, int64_t dy,
                                struct g5_world_point *out) {
    struct g5_world_point next;
    if (!out || !g5_world_add_checked(p.x,dx,&next.x) ||
        !g5_world_add_checked(p.y,dy,&next.y)) return false;
    *out=next;
    return true;
}
bool g5_world_aabb_valid(const struct g5_world_aabb *b) {
    return b && b->min.x <= b->max.x && b->min.y <= b->max.y;
}
bool g5_camera_valid(const struct g5_camera *c) {
    return c && c->revision != 0u && valid_size(c->width_px,c->height_px) &&
           valid_zoom(c->zoom) && valid_scale(c->device_scale) &&
           isfinite(c->offset_x_units) && isfinite(c->offset_y_units) &&
           fabs(c->offset_x_units) < 1.0/G5_CANVAS_TICKS_PER_UNIT &&
           fabs(c->offset_y_units) < 1.0/G5_CANVAS_TICKS_PER_UNIT;
}
bool g5_camera_init(struct g5_camera *c,struct g5_world_point anchor,
                    uint32_t w,uint32_t h,double zoom,double scale) {
    if (!c || !valid_size(w,h) || !valid_zoom(zoom) ||
        !valid_scale(scale)) return false;
    struct g5_camera next={0};
    next.anchor=anchor; next.zoom=zoom; next.device_scale=scale;
    next.width_px=w; next.height_px=h; next.revision=1u;
    *c=next;
    return true;
}
/* Fold whole ticks out of transient floating offset while retaining residual
 * sub-tick motion. All computations stage into a copy for fail-atomic updates. */
static bool normalized_axis(int64_t *anchor,double *offset) {
    if (!isfinite(*offset)) return false;
    const double scaled=*offset*(double)G5_CANVAS_TICKS_PER_UNIT;
    /* Doubles beyond 2^52 cannot faithfully represent sub-tick changes. */
    if (!isfinite(scaled) || fabs(scaled) >= 4503599627370496.0)
        return false;
    const int64_t whole=(int64_t)scaled; /* truncation toward zero */
    int64_t next_anchor;
    if (!g5_world_add_checked(*anchor,whole,&next_anchor)) return false;
    const double residual=*offset-(double)whole/(double)G5_CANVAS_TICKS_PER_UNIT;
    if (!isfinite(residual) ||
        fabs(residual) >= 1.0/(double)G5_CANVAS_TICKS_PER_UNIT)
        return false;
    *anchor=next_anchor; *offset=residual;
    return true;
}
static bool apply_offset(struct g5_camera *c,double ox,double oy) {
    if (!g5_camera_valid(c) || c->revision==UINT64_MAX ||
        !isfinite(ox) || !isfinite(oy)) return false;
    struct g5_camera next=*c;
    if (!normalized_axis(&next.anchor.x,&ox) ||
        !normalized_axis(&next.anchor.y,&oy)) return false;
    next.offset_x_units=ox; next.offset_y_units=oy;
    if (!advance_revision(&next)) return false;
    *c=next;
    return true;
}
bool g5_camera_set_viewport(struct g5_camera *c,uint32_t w,uint32_t h) {
    if (!g5_camera_valid(c) || !valid_size(w,h) ||
        c->revision==UINT64_MAX) return false;
    struct g5_camera next=*c;
    next.width_px=w; next.height_px=h;
    if (!advance_revision(&next)) return false;
    *c=next; return true;
}
bool g5_camera_pan(struct g5_camera *c,double dx,double dy) {
    if (!g5_camera_valid(c) || !isfinite(dx) || !isfinite(dy) ||
        fabs(dx)>65536.0 || fabs(dy)>65536.0) return false;
    double denom=c->zoom*c->device_scale;
    return apply_offset(c,c->offset_x_units-dx/denom,
                         c->offset_y_units-dy/denom);
}
bool g5_camera_zoom_at(struct g5_camera *c,double factor,
                        double px,double py) {
    if (!g5_camera_valid(c) || !isfinite(factor) || factor<=0.0 ||
        !isfinite(px) || !isfinite(py) ||
        px<0.0 || py<0.0 ||
        px>(double)c->width_px || py>(double)c->height_px)
        return false;
    double next_zoom=c->zoom*factor;
    if (!valid_zoom(next_zoom) || c->revision==UINT64_MAX) return false;
    double old_denom=c->zoom*c->device_scale;
    double new_denom=next_zoom*c->device_scale;
    double ox=c->offset_x_units+
        (px-(double)c->width_px*0.5)*(1.0/old_denom-1.0/new_denom);
    double oy=c->offset_y_units+
        (py-(double)c->height_px*0.5)*(1.0/old_denom-1.0/new_denom);
    struct g5_camera next=*c;
    next.zoom=next_zoom;
    if (!apply_offset(&next,ox,oy)) return false;
    *c=next; return true;
}
static double project_axis(int64_t world,int64_t anchor,double offset,
                           double factor,double midpoint) {
    const g5_wide_t delta=(g5_wide_t)world-(g5_wide_t)anchor;
    return midpoint+(((double)delta/
        (double)G5_CANVAS_TICKS_PER_UNIT)-offset)*factor;
}
static bool round_screen(double v,int32_t *out) {
    if (!out || !isfinite(v) ||
        v<(double)INT32_MIN+0.5 || v>(double)INT32_MAX-0.5)
        return false;
    /* Truncation after sign-aware half addition: half away from zero. */
    *out=(int32_t)(v>=0.0?v+0.5:v-0.5);
    return true;
}
bool g5_camera_world_to_screen(const struct g5_camera *c,
                                struct g5_world_point p,
                                int32_t *x,int32_t *y) {
    if (!g5_camera_valid(c) || !x || !y) return false;
    const double f=c->zoom*c->device_scale;
    int32_t rx,ry;
    if (!round_screen(project_axis(p.x,c->anchor.x,c->offset_x_units,
                                   f,(double)c->width_px*0.5),&rx) ||
        !round_screen(project_axis(p.y,c->anchor.y,c->offset_y_units,
                                   f,(double)c->height_px*0.5),&ry))
        return false;
    *x=rx; *y=ry; return true;
}
static bool unproject_axis(int32_t pixel,int64_t anchor,
                           double offset,double factor,double midpoint,
                           int64_t *out) {
    if (!out) return false;
    const double delta=(((double)pixel-midpoint)/factor+offset)*
                       (double)G5_CANVAS_TICKS_PER_UNIT;
    if (!isfinite(delta) || fabs(delta)>=4503599627370496.0) return false;
    /* Rounding halfway away from zero is deterministic in the bounded
     * camera-local tick domain; anchor addition is checked in 128 bits. */
    const int64_t ticks=(int64_t)(delta>=0.0?delta+0.5:delta-0.5);
    return g5_world_add_checked(anchor,ticks,out);
}
bool g5_camera_screen_to_world(const struct g5_camera *c,
                                uint64_t revision,int32_t x,int32_t y,
                                struct g5_world_point *out) {
    if (!g5_camera_valid(c) || revision!=c->revision || !out) return false;
    const double f=c->zoom*c->device_scale;
    struct g5_world_point p;
    if (!unproject_axis(x,c->anchor.x,c->offset_x_units,f,
                        (double)c->width_px*0.5,&p.x) ||
        !unproject_axis(y,c->anchor.y,c->offset_y_units,f,
                        (double)c->height_px*0.5,&p.y)) return false;
    *out=p;return true;
}
bool g5_camera_aabb_visible(const struct g5_camera *c,
                             const struct g5_world_aabb *b,
                             uint64_t revision,bool *out_visible) {
    if (!g5_camera_valid(c) || revision!=c->revision ||
        !g5_world_aabb_valid(b) || !out_visible) return false;
    const double f=c->zoom*c->device_scale;
    const double left=project_axis(b->min.x,c->anchor.x,c->offset_x_units,
                                  f,(double)c->width_px*0.5);
    const double right=project_axis(b->max.x,c->anchor.x,c->offset_x_units,
                                   f,(double)c->width_px*0.5);
    const double top=project_axis(b->min.y,c->anchor.y,c->offset_y_units,
                                 f,(double)c->height_px*0.5);
    const double bottom=project_axis(b->max.y,c->anchor.y,c->offset_y_units,
                                    f,(double)c->height_px*0.5);
    if (!isfinite(left)||!isfinite(right)||!isfinite(top)||!isfinite(bottom))
        return false;
    *out_visible=right>=-1.0 && bottom>=-1.0 &&
                 left<=(double)c->width_px+1.0 &&
                 top<=(double)c->height_px+1.0;
    return true;
}
bool g5_camera_reference_visible_set(const struct g5_camera *c,
                                     uint64_t revision,const uint64_t *ids,
                                     const struct g5_world_aabb *boxes,
                                     size_t count,uint64_t *out_ids,
                                     size_t capacity,size_t *out_count) {
    if (!g5_camera_valid(c) || revision!=c->revision || !out_count ||
        (count && (!ids || !boxes)) || (capacity && !out_ids))
        return false;
    /* Validate before writing to ensure a failed call cannot expose a
     * partially calculated visible set. */
    size_t visible=0;
    for (size_t i=0;i<count;++i) {
        bool v=false;
        if (ids[i]==0 || !g5_camera_aabb_visible(c,&boxes[i],revision,&v))
            return false;
        if (v) {
            if (visible==capacity) return false;
            ++visible;
        }
    }
    size_t index=0;
    for (size_t i=0;i<count;++i) {
        bool v=false;
        (void)g5_camera_aabb_visible(c,&boxes[i],revision,&v);
        if (v) out_ids[index++]=ids[i];
    }
    *out_count=visible;
    return true;
}
