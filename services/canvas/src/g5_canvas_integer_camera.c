#include <aurora/g5_canvas_integer_camera.h>
#include <limits.h>
__extension__ typedef __int128 wide;
static bool valid(const struct g5_integer_camera *c) {
    return c && c->revision && c->zoom_q16>=G5_ICAM_MIN_ZOOM &&
        c->zoom_q16<=G5_ICAM_MAX_ZOOM && c->width_px>0 && c->height_px>0 &&
        c->width_px<=16384 && c->height_px<=16384;
}
static bool fit(wide v, int64_t *out) {
    if (!out || v<INT64_MIN || v>INT64_MAX) return false;
    *out=(int64_t)v; return true;
}
__extension__ typedef unsigned __int128 uwide;
/* Compiler-independent 128/128 unsigned division. This deliberately avoids
 * GCC/Clang __divti3 and __udivti3 in the freestanding Ring 3 object.
 * Precondition: denominator != 0; all camera callers enforce this. */
static uwide divide_unsigned(uwide numerator,uwide denominator) {
    uwide quotient=0, remainder=0;
    for (unsigned bit=128;bit>0;--bit) {
        remainder=(remainder<<1)|((numerator>>(bit-1))&1u);
        if (remainder>=denominator) {
            remainder-=denominator;
            quotient|=((uwide)1<<(bit-1));
        }
    }
    return quotient;
}
static wide round_div(wide n,wide d) {
    /* All current operands are well inside signed 128-bit range.
     * Round nearest, ties away from zero, without a compiler runtime. */
    const bool negative=n<0;
    const uwide magnitude=negative?(uwide)(-n):(uwide)n;
    const uwide divisor=(uwide)d;
    const uwide rounded=divide_unsigned(magnitude+divisor/2u,divisor);
    return negative?-(wide)rounded:(wide)rounded;
}
bool g5_icam_init(struct g5_integer_camera *c,int64_t x,int64_t y,
                   uint32_t zoom,uint32_t w,uint32_t h) {
    if (!c) return false;
    struct g5_integer_camera n={x,y,zoom,w,h,1};
    if (!valid(&n)) return false;
    *c=n; return true;
}
bool g5_icam_set_viewport(struct g5_integer_camera *c,uint32_t w,uint32_t h) {
    if (!valid(c) || c->revision==UINT64_MAX ||
        !w || !h || w>16384 || h>16384) return false;
    c->width_px=w; c->height_px=h; ++c->revision;
    return true;
}
bool g5_icam_pan(struct g5_integer_camera *c,int32_t dx,int32_t dy) {
    if (!valid(c) || c->revision==UINT64_MAX ||
        dx>65536 || dx< -65536 || dy>65536 || dy< -65536) return false;
    const wide denom=c->zoom_q16;
    int64_t nx,ny;
    if (!fit((wide)c->center_x_ticks-round_div((wide)dx*1024*65536,denom),&nx) ||
        !fit((wide)c->center_y_ticks-round_div((wide)dy*1024*65536,denom),&ny))
        return false;
    c->center_x_ticks=nx; c->center_y_ticks=ny; ++c->revision;
    return true;
}
bool g5_icam_zoom_at(struct g5_integer_camera *c,uint32_t factor,
                      int32_t px,int32_t py) {
    if (!valid(c) || !factor || c->revision==UINT64_MAX ||
        px<0 || py<0 || (uint32_t)px>c->width_px ||
        (uint32_t)py>c->height_px) return false;
    const wide scaled=round_div((wide)c->zoom_q16*factor,G5_ICAM_ONE);
    if (scaled<G5_ICAM_MIN_ZOOM || scaled>G5_ICAM_MAX_ZOOM) return false;
    const uint32_t next_zoom=(uint32_t)scaled;
    const wide xoff=(wide)px*2-c->width_px;
    const wide yoff=(wide)py*2-c->height_px;
    const wide numerator=(wide)1024*65536;
    int64_t nx,ny;
    /* Keep the same world point under the pointer before and after zoom.
     * Staging both axes ensures failure leaves the camera unchanged. */
    const wide old_x=round_div(xoff*numerator,(wide)c->zoom_q16*2);
    const wide new_x=round_div(xoff*numerator,(wide)next_zoom*2);
    const wide old_y=round_div(yoff*numerator,(wide)c->zoom_q16*2);
    const wide new_y=round_div(yoff*numerator,(wide)next_zoom*2);
    if (!fit((wide)c->center_x_ticks+old_x-new_x,&nx) ||
        !fit((wide)c->center_y_ticks+old_y-new_y,&ny)) return false;
    c->center_x_ticks=nx; c->center_y_ticks=ny;
    c->zoom_q16=next_zoom; ++c->revision;
    return true;
}
static bool project_axis(int64_t p,int64_t center,uint32_t zoom,
                         uint32_t extent,int32_t *out) {
    /* Pixel midpoint in half-pixel units, supports odd viewport sizes. */
    const wide delta=(wide)p-(wide)center;
    /* Single rounding avoids half-pixel double-rounding artifacts.
     * The viewport midpoint is represented exactly as extent/2. */
    const wide denom=(wide)1024*65536*2;
    const wide pixel=round_div(delta*zoom*2+(wide)extent*1024*65536,denom);
    if (!out || pixel<INT32_MIN || pixel>INT32_MAX) return false;
    *out=(int32_t)pixel; return true;
}
bool g5_icam_project(const struct g5_integer_camera *c,int64_t x,int64_t y,
                      int32_t *px,int32_t *py) {
    int32_t a,b;
    if (!valid(c) || !px || !py ||
        !project_axis(x,c->center_x_ticks,c->zoom_q16,c->width_px,&a) ||
        !project_axis(y,c->center_y_ticks,c->zoom_q16,c->height_px,&b))
        return false;
    *px=a; *py=b; return true;
}
static bool unproject_axis(int32_t pixel,int64_t center,uint32_t zoom,
                           uint32_t extent,int64_t *out) {
    const wide offset=(wide)pixel*2-(wide)extent;
    return fit((wide)center+round_div(offset*(wide)1024*65536,
                                       (wide)zoom*2),out);
}
bool g5_icam_unproject(const struct g5_integer_camera *c,uint64_t revision,
                        int32_t px,int32_t py,int64_t *x,int64_t *y) {
    int64_t a,b;
    if (!valid(c) || revision!=c->revision || !x || !y ||
        !unproject_axis(px,c->center_x_ticks,c->zoom_q16,c->width_px,&a) ||
        !unproject_axis(py,c->center_y_ticks,c->zoom_q16,c->height_px,&b))
        return false;
    *x=a; *y=b; return true;
}

bool g5_icam_project_rect(const struct g5_integer_camera *c,
                           int64_t x,int64_t y,uint32_t w,uint32_t h,
                           struct g5_icam_screen_rect *out) {
    if (!valid(c) || !out || !w || !h) return false;
    int64_t right,bottom;
    if (!fit((wide)x+w,&right) || !fit((wide)y+h,&bottom))
        return false;
    int32_t left_px,top_px,right_px,bottom_px;
    if (!project_axis(x,c->center_x_ticks,c->zoom_q16,
                      c->width_px,&left_px) ||
        !project_axis(y,c->center_y_ticks,c->zoom_q16,
                      c->height_px,&top_px) ||
        !project_axis(right,c->center_x_ticks,c->zoom_q16,
                      c->width_px,&right_px) ||
        !project_axis(bottom,c->center_y_ticks,c->zoom_q16,
                      c->height_px,&bottom_px))
        return false;
    const int64_t projected_width=(int64_t)right_px-left_px;
    const int64_t projected_height=(int64_t)bottom_px-top_px;
    if (projected_width<=0 || projected_height<=0 ||
        projected_width>INT32_MAX || projected_height>INT32_MAX)
        return false;
    const struct g5_icam_screen_rect result={
        left_px,top_px,(int32_t)projected_width,(int32_t)projected_height
    };
    *out=result;
    return true;
}

bool g5_icam_clip_to_viewport(
    const struct g5_integer_camera *c,
    const struct g5_icam_screen_rect *r,
    struct g5_icam_clip_rect *out) {
    if (!valid(c) || !r || !out ||
        r->width_px<=0 || r->height_px<=0) return false;
    const int64_t right=(int64_t)r->x_px+r->width_px;
    const int64_t bottom=(int64_t)r->y_px+r->height_px;
    const int64_t left=r->x_px>0?r->x_px:0;
    const int64_t top=r->y_px>0?r->y_px:0;
    const int64_t clipped_right=right<(int64_t)c->width_px ?
                                right:(int64_t)c->width_px;
    const int64_t clipped_bottom=bottom<(int64_t)c->height_px ?
                                 bottom:(int64_t)c->height_px;
    if (clipped_right<=left || clipped_bottom<=top) return false;
    const struct g5_icam_clip_rect clipped={
        (int32_t)left,(int32_t)top,
        (int32_t)(clipped_right-left),(int32_t)(clipped_bottom-top),
        (int32_t)(left-r->x_px),(int32_t)(top-r->y_px)
    };
    *out=clipped;
    return true;
}

bool g5_icam_hit_world_rect(const struct g5_integer_camera *c,
                             uint64_t revision,int32_t px,int32_t py,
                             int64_t x,int64_t y,uint32_t w,uint32_t h) {
    if (!valid(c) || revision!=c->revision ||
        px<0 || py<0 || (uint32_t)px>=c->width_px ||
        (uint32_t)py>=c->height_px || !w || !h)
        return false;
    int64_t world_x,world_y;
    if (!g5_icam_unproject(c,revision,px,py,&world_x,&world_y))
        return false;
    return (wide)world_x>=(wide)x &&
           (wide)world_y>=(wide)y &&
           (wide)world_x<(wide)x+w &&
           (wide)world_y<(wide)y+h;
}

bool g5_icam_project_pair(
    const struct g5_integer_camera *camera,uint64_t expected_revision,
    const struct g5_icam_world_rect *first,
    const struct g5_icam_world_rect *second,
    struct g5_icam_screen_rect *first_out,
    struct g5_icam_screen_rect *second_out) {
    if (!camera || !first || !second || !first_out || !second_out ||
        first_out==second_out || camera->revision!=expected_revision)
        return false;
    struct g5_icam_screen_rect a,b;
    if (!g5_icam_project_rect(camera,first->x_ticks,first->y_ticks,
                              first->width_ticks,first->height_ticks,&a) ||
        !g5_icam_project_rect(camera,second->x_ticks,second->y_ticks,
                              second->width_ticks,second->height_ticks,&b))
        return false;
    *first_out=a;
    *second_out=b;
    return true;
}

/* Ring 3 is the sole owner of the committed camera. This accepts an
 * authenticated compositor acknowledgement's complete snapshot only when
 * it belongs to the currently outstanding camera revision. No partial
 * publication is possible, and neither a stale nor an exhausted revision
 * can replace a live camera. */
bool g5_icam_accept_snapshot(struct g5_integer_camera *committed,
                              uint64_t expected_revision,
                              const struct g5_integer_camera *snapshot) {
    if (!valid(committed) || !valid(snapshot) ||
        expected_revision==UINT64_MAX ||
        committed->revision!=expected_revision ||
        snapshot->revision!=expected_revision+1u ||
        snapshot->revision==UINT64_MAX ||
        snapshot->width_px!=committed->width_px ||
        snapshot->height_px!=committed->height_px)
        return false;
    *committed=*snapshot;
    return true;
}
