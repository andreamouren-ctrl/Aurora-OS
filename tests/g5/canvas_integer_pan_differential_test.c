#include <aurora/g5_canvas_integer_pan.h>
#include <aurora/g5_canvas_camera.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
/* Differential gate for the ONLY currently supported common domain:
 * integer pixel translation, zoom=1, device scale=1. No Ring3 claim. */
static void check(int64_t x,int64_t y,int32_t dx,int32_t dy) {
    struct g5_integer_pan integer={x,y,1};
    struct g5_camera reference;
    assert(g5_camera_init(&reference,(struct g5_world_point){x,y},
                          1024,768,1.0,1.0));
    /* The production IPC contract rejects zero motion; reference math does not. */
    if (dx==0 && dy==0) return;
    bool a=g5_integer_pan_apply(&integer,dx,dy,1);
    bool b=g5_camera_pan(&reference,(double)dx,(double)dy);
    assert(a==b);
    if (a) {
        assert(integer.x_ticks==reference.anchor.x);
        assert(integer.y_ticks==reference.anchor.y);
        assert(integer.revision==reference.revision);
        assert(reference.offset_x_units==0.0);
        assert(reference.offset_y_units==0.0);
    } else {
        assert(integer.x_ticks==x && integer.y_ticks==y);
        assert(integer.revision==1);
    }
}
int main(void) {
    const int64_t anchors[]={0,1,-1,INT64_C(1000000000000000000),
                             -INT64_C(1000000000000000000),
                             INT64_MAX-32768,INT64_MIN+32768};
    for (unsigned i=0;i<sizeof anchors/sizeof anchors[0];++i)
      for (unsigned j=0;j<sizeof anchors/sizeof anchors[0];++j)
        for (int dx=-32;dx<=32;++dx)
          for (int dy=-32;dy<=32;++dy)
            check(anchors[i],anchors[j],dx,dy);
    puts("[g5-wp05-integer] reference differential checks passed");
    return 0;
}
