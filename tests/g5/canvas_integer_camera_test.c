#include <aurora/g5_canvas_integer_camera.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
int main(void) {
    struct g5_integer_camera c={0},saved;
    int32_t x=0,y=0;
    int64_t wx=0,wy=0;
    assert(!g5_icam_init(&c,0,0,0,800,600));
    assert(g5_icam_init(&c,0,0,G5_ICAM_ONE,800,600));
    assert(g5_icam_project(&c,0,0,&x,&y) && x==400 && y==300);
    assert(g5_icam_unproject(&c,c.revision,400,300,&wx,&wy) && wx==0 && wy==0);
    saved=c;
    assert(!g5_icam_pan(&c,65537,0));
    assert(c.revision==saved.revision && c.center_x_ticks==saved.center_x_ticks);
    assert(g5_icam_pan(&c,8,-4));
    assert(c.center_x_ticks==-8192 && c.center_y_ticks==4096 && c.revision==2);
    assert(!g5_icam_unproject(&c,1,400,300,&wx,&wy));
    assert(g5_icam_project(&c,0,0,&x,&y) && x==408 && y==296);
    assert(g5_icam_unproject(&c,2,x,y,&wx,&wy) && wx==0 && wy==0);
    c.revision=UINT64_MAX;
    saved=c;
    assert(!g5_icam_pan(&c,1,1) && c.revision==saved.revision);
    assert(g5_icam_init(&c,INT64_MAX,INT64_MIN,G5_ICAM_MIN_ZOOM,800,600));
    saved=c;
    assert(!g5_icam_pan(&c,-65536,65536));
    assert(c.center_x_ticks==saved.center_x_ticks &&
           c.center_y_ticks==saved.center_y_ticks && c.revision==saved.revision);
    assert(!g5_icam_project(&c,INT64_MIN,INT64_MAX,&x,&y));
    assert(g5_icam_init(&c,0,0,G5_ICAM_ONE,800,600));
    assert(g5_icam_zoom_at(&c,2*G5_ICAM_ONE,400,300));
    assert(c.zoom_q16==2*G5_ICAM_ONE && c.revision==2);
    assert(c.center_x_ticks==0 && c.center_y_ticks==0);
    assert(g5_icam_project(&c,1024,0,&x,&y) && x==402 && y==300);
    assert(g5_icam_init(&c,0,0,G5_ICAM_ONE,800,600));
    assert(g5_icam_zoom_at(&c,2*G5_ICAM_ONE,500,350));
    assert(c.center_x_ticks==51200 && c.center_y_ticks==25600);
    assert(g5_icam_unproject(&c,c.revision,500,350,&wx,&wy));
    assert(wx==102400 && wy==51200);
    saved=c;
    assert(!g5_icam_zoom_at(&c,0,500,350));
    assert(!g5_icam_zoom_at(&c,2*G5_ICAM_ONE,-1,350));
    assert(c.revision==saved.revision && c.center_x_ticks==saved.center_x_ticks);
    assert(g5_icam_init(&c,INT64_MAX,0,G5_ICAM_ONE,800,600));
    saved=c;
    assert(!g5_icam_zoom_at(&c,2*G5_ICAM_ONE,800,300));
    assert(c.revision==saved.revision && c.center_x_ticks==saved.center_x_ticks);
    assert(g5_icam_init(&c,0,0,G5_ICAM_ONE,800,600));
    saved=c;
    assert(!g5_icam_set_viewport(&c,0,600));
    assert(!g5_icam_set_viewport(&c,16385,600));
    assert(c.revision==saved.revision && c.width_px==saved.width_px);
    assert(g5_icam_set_viewport(&c,801,601));
    assert(c.revision==2 && c.width_px==801 && c.height_px==601);
    assert(!g5_icam_unproject(&c,1,400,300,&wx,&wy));
    c.revision=UINT64_MAX;
    assert(!g5_icam_set_viewport(&c,800,600));
    puts("g5 integer camera host arithmetic gate passed");
    return 0;
}
