#include <aurora/g5_canvas_pan_frame.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
    struct g5_integer_pan c={INT64_C(102400),-INT64_C(102400),1};
    assert(g5_canvas_pan_screen_to_world(&c,8,-4,1));
    assert(c.x_ticks==INT64_C(94208) && c.y_ticks==-INT64_C(98304));
    assert(c.revision==2);
    /* Inverse screen gesture restores the exact world origin. */
    assert(g5_canvas_pan_screen_to_world(&c,-8,4,2));
    assert(c.x_ticks==INT64_C(102400) && c.y_ticks==-INT64_C(102400));
    assert(c.revision==3);
    assert(!g5_canvas_pan_screen_to_world(&c,0,0,3));
    assert(!g5_canvas_pan_screen_to_world(&c,33,0,3));
    assert(!g5_canvas_pan_screen_to_world(&c,1,0,2));
    assert(c.x_ticks==INT64_C(102400) && c.y_ticks==-INT64_C(102400));
    puts("[g5-wp05] screen/world frame adapter checks passed");
    return 0;
}
