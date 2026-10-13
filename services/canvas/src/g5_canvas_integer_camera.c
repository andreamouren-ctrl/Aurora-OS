#include <aurora/g5_canvas_integer_camera.h>
#include <stddef.h>

/* Clang may lower ordinary structure copies/zero initialization into
 * compiler runtime calls even with -ffreestanding -fno-builtin.
 * Weak fallback definitions keep this standalone Ring 3 object linkable
 * without libc; a platform implementation can override both symbols.
 * Volatile byte writes prevent recursive loop-idiom recognition. */
__attribute__((weak)) void *memset(void *destination,int value,size_t length) {
    volatile unsigned char *p=(volatile unsigned char *)destination;
    for(size_t i=0;i<length;++i)p[i]=(unsigned char)value;
    return destination;
}
__attribute__((weak)) void *memcpy(void *destination,const void *source,
                                    size_t length) {
    volatile unsigned char *d=(volatile unsigned char *)destination;
    const volatile unsigned char *s=(const volatile unsigned char *)source;
    for(size_t i=0;i<length;++i)d[i]=s[i];
    return destination;
}

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

/* Bounded stack staging avoids allocation and prevents a partially
 * updated compositor scene if any geometry is malformed or overflows. */
bool g5_icam_project_scene(
    const struct g5_integer_camera *camera,uint64_t expected_revision,
    const struct g5_icam_scene_item *items,uint32_t count,
    struct g5_icam_projected_item *out) {
    if (!valid(camera) || camera->revision!=expected_revision ||
        !items || !out || !count || count>G5_ICAM_SCENE_MAX_RECTS)
        return false;
    struct g5_icam_projected_item staged[G5_ICAM_SCENE_MAX_RECTS];
    for (uint32_t i=0;i<count;++i) {
        const struct g5_icam_world_rect *w=&items[i].world;
        if (!items[i].surface_id ||
            !g5_icam_project_rect(camera,w->x_ticks,w->y_ticks,
                                  w->width_ticks,w->height_ticks,
                                  &staged[i].projected))
            return false;
        for (uint32_t j=0;j<i;++j)
            if (items[j].surface_id==items[i].surface_id)
                return false;
        staged[i].surface_id=items[i].surface_id;
        staged[i].visible=g5_icam_clip_to_viewport(
            camera,&staged[i].projected,&staged[i].clip);
        if (!staged[i].visible) {
            const struct g5_icam_clip_rect empty={0};
            staged[i].clip=empty;
        }
    }
    for (uint32_t i=0;i<count;++i) out[i]=staged[i];
    return true;
}

/* Input routing uses exactly the clipped rectangles submitted to the
 * compositor, not a separately rounded world-space hit-test path. */
bool g5_icam_hit_projected_scene(
    const struct g5_integer_camera *camera,uint64_t expected_revision,
    const struct g5_icam_projected_item *items,uint32_t count,
    int32_t pointer_x_px,int32_t pointer_y_px,
    uint64_t *out_surface_id) {
    if (!valid(camera) || camera->revision!=expected_revision ||
        !items || !out_surface_id || count>G5_ICAM_SCENE_MAX_RECTS ||
        pointer_x_px<0 || pointer_y_px<0 ||
        (uint32_t)pointer_x_px>=camera->width_px ||
        (uint32_t)pointer_y_px>=camera->height_px)
        return false;
    uint64_t hit=0u;
    for (uint32_t i=count;i>0;--i) {
        const struct g5_icam_projected_item *item=&items[i-1u];
        if (!item->surface_id) return false;
        if (!item->visible) continue;
        const struct g5_icam_clip_rect *r=&item->clip;
        if (r->width_px<=0 || r->height_px<=0 ||
            r->x_px<0 || r->y_px<0 ||
            (int64_t)r->x_px+r->width_px>camera->width_px ||
            (int64_t)r->y_px+r->height_px>camera->height_px)
            return false;
        if (!hit && pointer_x_px>=r->x_px && pointer_y_px>=r->y_px &&
            (int64_t)pointer_x_px<(int64_t)r->x_px+r->width_px &&
            (int64_t)pointer_y_px<(int64_t)r->y_px+r->height_px)
            hit=item->surface_id;
    }
    *out_surface_id=hit;
    return true;
}

/* Prepare a whole compositor frame without publishing partial results.
 * All world positions remain untouched, including fully culled surfaces. */
bool g5_icam_prepare_submission(
    const struct g5_integer_camera *camera,uint64_t expected_camera_revision,
    uint64_t session_generation,uint64_t scene_revision,uint64_t request_id,
    const struct g5_icam_scene_item *items,uint32_t count,
    struct g5_icam_scene_submission *out) {
    if (!out || !camera || !session_generation || !scene_revision ||
        !request_id || !count || count>G5_ICAM_SCENE_MAX_RECTS ||
        !valid(camera) || camera->revision!=expected_camera_revision)
        return false;
    struct g5_icam_scene_submission candidate={0};
    if (!g5_icam_project_scene(camera,expected_camera_revision,
                                items,count,candidate.items))
        return false;
    candidate.session_generation=session_generation;
    candidate.scene_revision=scene_revision;
    candidate.request_id=request_id;
    candidate.camera=*camera;
    candidate.count=count;
    *out=candidate;
    return true;
}

/* A forwarded ACK is not a presented frame. Reject stale and replayed
 * compositor completions before the Shell publishes camera state. */
bool g5_icam_match_presented(
    const struct g5_icam_scene_submission *submitted,
    uint64_t session_generation,uint64_t scene_revision,
    uint64_t request_id,uint64_t camera_revision,uint64_t display_serial,
    uint64_t previous_display_serial) {
    return submitted && submitted->count>0 &&
        submitted->count<=G5_ICAM_SCENE_MAX_RECTS &&
        session_generation!=0 && scene_revision!=0 && request_id!=0 &&
        submitted->session_generation==session_generation &&
        submitted->scene_revision==scene_revision &&
        submitted->request_id==request_id &&
        valid(&submitted->camera) &&
        submitted->camera.revision==camera_revision &&
        display_serial!=0 && display_serial>previous_display_serial;
}

/* Do not resolve focus against an unpresented or superseded scene.
 * This is a Ring 3 helper: trusted IPC must establish the display serial
 * and authenticated compositor identity before calling it. */
bool g5_icam_hit_presented_submission(
    const struct g5_icam_scene_submission *submitted,
    const struct g5_integer_camera *live_camera,
    uint64_t live_scene_revision,uint64_t presented_display_serial,
    int32_t pointer_x_px,int32_t pointer_y_px,uint64_t *out_surface_id) {
    if (!submitted || !valid(live_camera) ||
        !submitted->session_generation || !submitted->request_id ||
        !live_scene_revision || !presented_display_serial ||
        submitted->scene_revision!=live_scene_revision ||
        submitted->camera.revision!=live_camera->revision ||
        submitted->camera.center_x_ticks!=live_camera->center_x_ticks ||
        submitted->camera.center_y_ticks!=live_camera->center_y_ticks ||
        submitted->camera.zoom_q16!=live_camera->zoom_q16 ||
        submitted->camera.width_px!=live_camera->width_px ||
        submitted->camera.height_px!=live_camera->height_px)
        return false;
    return g5_icam_hit_projected_scene(
        &submitted->camera,live_camera->revision,
        submitted->items,submitted->count,
        pointer_x_px,pointer_y_px,out_surface_id);
}

/* All checks precede both writes. In particular a speculative camera
 * cannot be published on forwarded, replayed or out-of-order ACKs. */
bool g5_icam_commit_presented_camera(
    struct g5_integer_camera *live_camera,
    uint64_t *last_display_serial,
    const struct g5_icam_scene_submission *submitted,
    uint64_t session_generation,uint64_t scene_revision,
    uint64_t request_id,uint64_t camera_revision,
    uint64_t display_serial) {
    if (!live_camera || !last_display_serial || !submitted ||
        !valid(live_camera) || live_camera->revision==UINT64_MAX ||
        !g5_icam_match_presented(submitted,session_generation,
                                  scene_revision,request_id,camera_revision,
                                  display_serial,*last_display_serial) ||
        submitted->camera.revision!=live_camera->revision+1u ||
        submitted->camera.width_px!=live_camera->width_px ||
        submitted->camera.height_px!=live_camera->height_px)
        return false;
    *live_camera=submitted->camera;
    *last_display_serial=display_serial;
    return true;
}

/* Explicit volatile byte loops prevent Clang's freestanding lowering
 * from inserting unavailable memcpy/memset runtime dependencies. */
static void g5_batch_zero(void *destination,uint32_t bytes) {
    volatile uint8_t *p=(volatile uint8_t *)destination;
    for(uint32_t i=0;i<bytes;++i)p[i]=0u;
}
static void g5_batch_copy(void *destination,const void *source,uint32_t bytes) {
    volatile uint8_t *d=(volatile uint8_t *)destination;
    const volatile uint8_t *s=(const volatile uint8_t *)source;
    for(uint32_t i=0;i<bytes;++i)d[i]=s[i];
}
static void g5_batch_put(uint8_t *p,uint64_t value,uint32_t bytes) {
    for (uint32_t i=0;i<bytes;++i) p[i]=(uint8_t)(value>>(8u*i));
}
static uint64_t g5_batch_get(const uint8_t *p,uint32_t bytes) {
    uint64_t value=0u;
    for (uint32_t i=0;i<bytes;++i) value|=(uint64_t)p[i]<<(8u*i);
    return value;
}
/* Explicit little-endian serialization; never copy native structs onto
 * the wire. The G5 envelope authenticates request/session separately. */
bool g5_icam_encode_batch(
    const struct g5_icam_scene_submission *submitted,
    uint8_t *destination,uint32_t capacity,uint32_t *written) {
    if (!submitted || !destination || !written ||
        !submitted->session_generation || !submitted->request_id ||
        !submitted->scene_revision || !valid(&submitted->camera) ||
        !submitted->count || submitted->count>G5_ICAM_WIRE_BATCH_MAX)
        return false;
    const uint32_t bytes=G5_ICAM_WIRE_BATCH_HEADER+
        submitted->count*G5_ICAM_WIRE_BATCH_ITEM;
    if (capacity<bytes) return false;
    uint8_t staged[G5_ICAM_WIRE_BATCH_CAPACITY];
    g5_batch_zero(staged,sizeof(staged));
    staged[0]='G';staged[1]='5';staged[2]='S';staged[3]='B';
    g5_batch_put(staged+4u,1u,2u);
    g5_batch_put(staged+6u,submitted->count,2u);
    g5_batch_put(staged+8u,submitted->camera.revision,8u);
    g5_batch_put(staged+16u,submitted->scene_revision,8u);
    g5_batch_put(staged+24u,submitted->camera.width_px,4u);
    g5_batch_put(staged+28u,submitted->camera.height_px,4u);
    g5_batch_put(staged+32u,submitted->camera.zoom_q16,4u);
    g5_batch_put(staged+40u,(uint64_t)submitted->camera.center_x_ticks,8u);
    g5_batch_put(staged+48u,(uint64_t)submitted->camera.center_y_ticks,8u);
    for (uint32_t i=0;i<submitted->count;++i) {
        const struct g5_icam_projected_item *item=&submitted->items[i];
        if (!item->surface_id) return false;
        for(uint32_t j=0;j<i;++j)
            if(submitted->items[j].surface_id==item->surface_id) return false;
        uint8_t *p=staged+G5_ICAM_WIRE_BATCH_HEADER+
            i*G5_ICAM_WIRE_BATCH_ITEM;
        g5_batch_put(p,item->surface_id,8u);
        const int32_t fields[10]={
            item->projected.x_px,item->projected.y_px,
            item->projected.width_px,item->projected.height_px,
            item->clip.x_px,item->clip.y_px,
            item->clip.width_px,item->clip.height_px,
            item->clip.source_x_px,item->clip.source_y_px};
        for(uint32_t j=0;j<10u;++j)
            g5_batch_put(p+8u+4u*j,(uint32_t)fields[j],4u);
    }
    g5_batch_copy(destination,staged,bytes);
    *written=bytes;
    return true;
}

/* Decoder is side-effect-free until all length, identity and geometry
 * checks pass. Session/request identity comes from the authenticated
 * G5 envelope, never from untrusted surface IDs in the payload. */
bool g5_icam_decode_batch(
    const uint8_t *source,uint32_t length,
    uint64_t authenticated_session_generation,uint64_t authenticated_request_id,
    struct g5_icam_scene_submission *out) {
    if (!source || !out || !authenticated_session_generation ||
        !authenticated_request_id || length<G5_ICAM_WIRE_BATCH_HEADER ||
        source[0]!='G' || source[1]!='5' || source[2]!='S' ||
        source[3]!='B' || g5_batch_get(source+4u,2u)!=1u ||
        g5_batch_get(source+36u,4u)!=0u)
        return false;
    const uint32_t count=(uint32_t)g5_batch_get(source+6u,2u);
    if (!count || count>G5_ICAM_WIRE_BATCH_MAX ||
        length!=G5_ICAM_WIRE_BATCH_HEADER+
                count*G5_ICAM_WIRE_BATCH_ITEM)
        return false;
    struct g5_icam_scene_submission staged;
    g5_batch_zero(&staged,sizeof(staged));
    staged.session_generation=authenticated_session_generation;
    staged.request_id=authenticated_request_id;
    staged.scene_revision=g5_batch_get(source+16u,8u);
    staged.camera.revision=g5_batch_get(source+8u,8u);
    staged.camera.width_px=(uint32_t)g5_batch_get(source+24u,4u);
    staged.camera.height_px=(uint32_t)g5_batch_get(source+28u,4u);
    staged.camera.zoom_q16=(uint32_t)g5_batch_get(source+32u,4u);
    staged.camera.center_x_ticks=(int64_t)g5_batch_get(source+40u,8u);
    staged.camera.center_y_ticks=(int64_t)g5_batch_get(source+48u,8u);
    if (!staged.scene_revision || !valid(&staged.camera))return false;
    staged.count=count;
    for(uint32_t i=0;i<count;++i) {
        const uint8_t *p=source+G5_ICAM_WIRE_BATCH_HEADER+
            i*G5_ICAM_WIRE_BATCH_ITEM;
        struct g5_icam_projected_item *item=&staged.items[i];
        item->surface_id=g5_batch_get(p,8u);
        if(!item->surface_id)return false;
        for(uint32_t j=0;j<i;++j)
            if(staged.items[j].surface_id==item->surface_id)return false;
        int32_t fields[10];
        for(uint32_t j=0;j<10u;++j)
            fields[j]=(int32_t)(uint32_t)g5_batch_get(p+8u+4u*j,4u);
        item->projected=(struct g5_icam_screen_rect){
            fields[0],fields[1],fields[2],fields[3]};
        item->clip=(struct g5_icam_clip_rect){
            fields[4],fields[5],fields[6],fields[7],fields[8],fields[9]};
        if (fields[2]<=0 || fields[3]<=0 || fields[6]<0 || fields[7]<0)
            return false;
        item->visible=fields[6]>0 && fields[7]>0;
        if (item->visible &&
            (fields[4]<0 || fields[5]<0 ||
             (int64_t)fields[4]+fields[6]>staged.camera.width_px ||
             (int64_t)fields[5]+fields[7]>staged.camera.height_px))
            return false;
        if (!item->visible &&
            (fields[4]!=0 || fields[5]!=0 || fields[6]!=0 ||
             fields[7]!=0 || fields[8]!=0 || fields[9]!=0))
            return false;
    }
    g5_batch_copy(out,&staged,sizeof(staged));
    return true;
}
