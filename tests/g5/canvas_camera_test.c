#include <aurora/g5_canvas_camera.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Wide subtraction is intentional in this host numeric oracle. */
__extension__ typedef __int128 g5_test_wide_t;

static struct g5_world_point pt(int64_t x,int64_t y) {
    return (struct g5_world_point){x,y};
}
static void test_checked_world(void) {
    int64_t output=91;
    assert(g5_world_add_checked(INT64_MAX,-1,&output) &&
           output==INT64_MAX-1);
    assert(!g5_world_add_checked(INT64_MAX,1,&output) && output==INT64_MAX-1);
    assert(!g5_world_add_checked(INT64_MIN,-1,&output));
    assert(!g5_world_add_checked(1,1,NULL));
    struct g5_world_point p=pt(1,2),out=pt(40,50);
    assert(g5_world_translate_checked(p,-2,3,&out));
    assert(out.x==-1 && out.y==5);
    assert(!g5_world_translate_checked(pt(INT64_MIN,0),-1,0,&out));
    assert(out.x==-1 && out.y==5);
    struct g5_world_aabb good={pt(-1,0),pt(1,2)};
    struct g5_world_aabb bad={pt(2,0),pt(1,2)};
    assert(g5_world_aabb_valid(&good) && !g5_world_aabb_valid(&bad));
}
static void test_camera_validation(void) {
    struct g5_camera c={0};
    assert(!g5_camera_valid(&c));
    assert(!g5_camera_init(&c,pt(0,0),0,720,1,1));
    assert(!g5_camera_init(&c,pt(0,0),1920,720,NAN,1));
    assert(!g5_camera_init(&c,pt(0,0),1920,720,1,INFINITY));
    assert(!g5_camera_init(&c,pt(0,0),1920,720,1e-10,1));
    assert(g5_camera_init(&c,pt(0,0),1920,1080,1,1));
    struct g5_camera before=c;
    assert(!g5_camera_pan(&c,NAN,1));
    assert(!g5_camera_zoom_at(&c,INFINITY,100,100));
    assert(!g5_camera_zoom_at(&c,1000000,100,100));
    assert(!g5_camera_zoom_at(&c,1,-1,100));
    assert(!g5_camera_set_viewport(&c,0,200));
    assert(memcmp(&c,&before,sizeof(c))==0);
    int32_t x=123,y=456;
    assert(g5_camera_world_to_screen(&c,pt(0,0),&x,&y));
    assert(x==960 && y==540);
    struct g5_world_point w=pt(99,99);
    assert(!g5_camera_screen_to_world(&c,0,x,y,&w));
    assert(w.x==99 && w.y==99);
    assert(g5_camera_screen_to_world(&c,c.revision,x,y,&w));
    assert(w.x==0 && w.y==0);
    uint64_t old=c.revision;
    assert(g5_camera_set_viewport(&c,1024,768) && c.revision==old+1);
    assert(!g5_camera_screen_to_world(&c,old,512,384,&w));
    before=c;
    c.revision=UINT64_MAX;
    assert(!g5_camera_pan(&c,1,1));
    assert(!g5_camera_set_viewport(&c,800,600));
    c=before;
}
static void test_distant_coordinates(void) {
    struct g5_camera c;
    assert(g5_camera_init(&c,pt(INT64_MAX-100,INT64_MIN+100),
                          1920,1080,1,1));
    int32_t x=0,y=0;
    assert(g5_camera_world_to_screen(&c,pt(INT64_MAX-100,INT64_MIN+100),
                                      &x,&y));
    assert(x==960 && y==540);
    assert(g5_camera_world_to_screen(&c,pt(INT64_MAX,INT64_MIN),&x,&y));
    assert(x==960 && y==540); /* subpixel deltas */
    struct g5_world_point p;
    assert(g5_camera_screen_to_world(&c,c.revision,960,540,&p));
    assert(p.x==INT64_MAX-100 && p.y==INT64_MIN+100);
    assert(!g5_camera_pan(&c,-200,200)); /* Would exceed both limits. */
    assert(c.anchor.x==INT64_MAX-100 && c.anchor.y==INT64_MIN+100);
    assert(!g5_camera_world_to_screen(&c,pt(INT64_MIN,INT64_MAX),&x,&y));
}
static void test_pointer_zoom(void) {
    struct g5_camera c;
    assert(g5_camera_init(&c,pt(9000000000000000000LL,
                                  -9000000000000000000LL),
                          1920,1080,1,2));
    const double x=1713.0,y=287.0;
    /* Measure camera-local sub-tick cursor invariant independent of
       world absolute double precision. */
    double initial_x=(x-960.0)/(c.zoom*c.device_scale)+c.offset_x_units;
    double initial_y=(y-540.0)/(c.zoom*c.device_scale)+c.offset_y_units;
    assert(g5_camera_zoom_at(&c,2.0,x,y));
    double later_x=(double)((g5_test_wide_t)c.anchor.x-
                        (g5_test_wide_t)9000000000000000000LL)/1024.0+
                      (x-960.0)/(c.zoom*c.device_scale)+c.offset_x_units;
    double later_y=(double)((g5_test_wide_t)c.anchor.y+
                        (g5_test_wide_t)9000000000000000000LL)/1024.0+
                      (y-540.0)/(c.zoom*c.device_scale)+c.offset_y_units;
    assert(fabs(later_x-initial_x)<1e-8);
    assert(fabs(later_y-initial_y)<1e-8);
    assert(g5_camera_zoom_at(&c,0.5,x,y));
    assert(fabs(c.zoom-1.0)<1e-12);
    assert(g5_camera_valid(&c));
}
static void test_many_movements(void) {
    struct g5_camera c;
    assert(g5_camera_init(&c,pt(-123456789,987654321),1600,900,1,1));
    for (int i=0;i<100000;++i) {
        assert(g5_camera_pan(&c,0.13,-0.11));
        assert(g5_camera_zoom_at(&c,1.00001,350,450));
        assert(g5_camera_zoom_at(&c,1.0/1.00001,350,450));
        assert(g5_camera_valid(&c));
    }
    assert(c.revision==300001u);
    assert(fabs(c.zoom-1.0)<1e-9);
}
static void test_visibility_and_stale(void) {
    struct g5_camera c;
    assert(g5_camera_init(&c,pt(0,0),1000,800,1,1));
    struct g5_world_aabb b[5]={
        {pt(-1024,-1024),pt(1024,1024)},
        {pt(100000000,100000000),pt(100001000,100001000)},
        {pt(-512000,-409600),pt(-511999,-409599)}, /* boundary */
        {pt(INT64_MIN,INT64_MIN),pt(INT64_MIN+100,INT64_MIN+100)},
        {pt(511999,409599),pt(512000,409600)}
    };
    const uint64_t ids[5]={10,11,12,13,14};
    uint64_t out[5]={0};
    size_t count=55;
    assert(g5_camera_reference_visible_set(&c,c.revision,
                                           ids,b,5,out,5,&count));
    assert(count==3 && out[0]==10 && out[1]==12 && out[2]==14);
    uint64_t prev=c.revision;
    assert(g5_camera_pan(&c,100,0));
    count=88;
    assert(!g5_camera_reference_visible_set(&c,prev,
                                            ids,b,5,out,5,&count));
    assert(count==88);
    struct g5_world_aabb invalid={pt(1,2),pt(0,2)};
    bool visible=true;
    assert(!g5_camera_aabb_visible(&c,&invalid,c.revision,&visible));
    assert(visible);
    assert(!g5_camera_reference_visible_set(&c,c.revision,
                                            ids,b,5,out,0,&count));
    assert(count==88);
}
static void test_roundtrip_scales(void) {
    const double zooms[5]={G5_CAMERA_MIN_ZOOM,0.25,1.0,4.0,
                           G5_CAMERA_MAX_ZOOM};
    const double scales[3]={G5_CAMERA_MIN_DEVICE_SCALE,1.0,
                            G5_CAMERA_MAX_DEVICE_SCALE};
    for (size_t z=0;z<5;++z) for (size_t d=0;d<3;++d) {
        struct g5_camera c;
        const struct g5_world_point anchor=pt(-9876543210LL,8765432109LL);
        assert(g5_camera_init(&c,anchor,1200,900,zooms[z],scales[d]));
        for (int k=-20;k<=20;++k) {
            struct g5_world_point p=pt(anchor.x+(int64_t)k*1024,
                anchor.y-(int64_t)k*1024);
            int32_t x,y;
            assert(g5_camera_world_to_screen(&c,p,&x,&y));
            struct g5_world_point inverse;
            assert(g5_camera_screen_to_world(&c,c.revision,x,y,&inverse));
            const double error_limit=
                0.5/(c.zoom*c.device_scale)*1024.0+2.0;
            assert(fabs((double)(inverse.x-p.x))<=error_limit);
            assert(fabs((double)(inverse.y-p.y))<=error_limit);
            int32_t reproj_x=0,reproj_y=0;
            assert(g5_camera_world_to_screen(&c,inverse,
                                             &reproj_x,&reproj_y));
            assert(abs(reproj_x-x)<=1 && abs(reproj_y-y)<=1);
        }
    }
}
int main(void) {
    test_checked_world();
    test_camera_validation();
    test_distant_coordinates();
    test_pointer_zoom();
    test_many_movements();
    test_roundtrip_scales();
    test_visibility_and_stale();
    puts("[g5-wp05] reference camera numeric and visibility tests passed");
    return 0;
}
