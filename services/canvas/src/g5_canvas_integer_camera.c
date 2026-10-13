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
static wide round_div(wide n, wide d) {
    /* d strictly positive. Round nearest, ties away from zero. */
    return n>=0 ? (n+d/2)/d : -((-n+d/2)/d);
}
bool g5_icam_init(struct g5_integer_camera *c,int64_t x,int64_t y,
                   uint32_t zoom,uint32_t w,uint32_t h) {
    if (!c) return false;
    struct g5_integer_camera n={x,y,zoom,w,h,1};
    if (!valid(&n)) return false;
    *c=n; return true;
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
static bool project_axis(int64_t p,int64_t center,uint32_t zoom,
                         uint32_t extent,int32_t *out) {
    /* Pixel midpoint in half-pixel units, supports odd viewport sizes. */
    const wide delta=(wide)p-(wide)center;
    const wide half=round_div(delta*zoom*2,(wide)1024*65536)+(wide)extent;
    const wide pixel=round_div(half,2);
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
