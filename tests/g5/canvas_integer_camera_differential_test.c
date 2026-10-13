#include <aurora/g5_canvas_camera.h>
#include <aurora/g5_canvas_integer_camera.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
/* Bounded differential oracle: identical device scale 1 and representable
 * Q16 zoom. No claim of complete floating point equivalence. */
static void compare(struct g5_integer_camera *i,struct g5_camera *f,
                    int64_t wx,int64_t wy) {
    int32_t ix,iy,fx,fy;
    struct g5_world_point p={wx,wy};
    assert(g5_icam_project(i,wx,wy,&ix,&iy));
    assert(g5_camera_world_to_screen(f,p,&fx,&fy));
    assert(abs(ix-fx)<=1 && abs(iy-fy)<=1);
}
int main(void) {
    const uint32_t zooms[]={G5_ICAM_MIN_ZOOM,G5_ICAM_ONE,
                            2*G5_ICAM_ONE,G5_ICAM_MAX_ZOOM};
    const int64_t anchors[]={0,INT64_C(123456789012345),-INT64_C(123456789012345)};
    unsigned checks=0;
    for (unsigned a=0;a<3;++a) for (unsigned z=0;z<4;++z) {
        struct g5_integer_camera i;
        struct g5_camera f;
        const double zoom=(double)zooms[z]/65536.0;
        assert(g5_icam_init(&i,anchors[a],anchors[a],zooms[z],800,600));
        assert(g5_camera_init(&f,(struct g5_world_point){anchors[a],anchors[a]},
                              800,600,zoom,1.0));
        compare(&i,&f,anchors[a],anchors[a]); ++checks;
        /* Exact whole-world-unit shifts avoid the representational
         * differences of a sub-tick floating reference offset. */
        for (int n=-4;n<=4;++n) {
            const int64_t delta=(int64_t)n*1024;
            compare(&i,&f,anchors[a]+delta,anchors[a]-delta); ++checks;
        }
        assert(g5_icam_pan(&i,8,-4));
        assert(g5_camera_pan(&f,8.0,-4.0));
        /* At low zoom, both cameras retain the same exact tick translation.
         * At high zoom, integer tick quantization is expected; allow a
         * one-pixel difference in the compositor projection. */
        compare(&i,&f,anchors[a],anchors[a]); ++checks;
    }
    printf("g5 camera differential oracle: %u bounded projections passed\n",checks);
    return 0;
}
