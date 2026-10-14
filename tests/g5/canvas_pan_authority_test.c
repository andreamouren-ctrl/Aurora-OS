#include <aurora/g5_canvas_pan_authority.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    struct g5_integer_pan c={0,0,1}, old;
    struct g5_canvas_pan_owner o={11,22,33,44,true};
    struct g5_canvas_pan_request r={11,22,33,44,1,8,-8};
    assert(g5_canvas_pan_authorized(&c,&o,&r));
    assert(c.x_ticks==-8192 && c.y_ticks==8192 && c.revision==2);
    old=c;
    assert(!g5_canvas_pan_authorized(&c,&o,&r)); /* stale camera */
    r.expected_camera_revision=2;
    r.session_generation=12;
    assert(!g5_canvas_pan_authorized(&c,&o,&r));
    r.session_generation=11; r.process_generation=23;
    assert(!g5_canvas_pan_authorized(&c,&o,&r));
    r.process_generation=22; r.surface_generation=34;
    assert(!g5_canvas_pan_authorized(&c,&o,&r));
    r.surface_generation=33; r.owner_token=45;
    assert(!g5_canvas_pan_authorized(&c,&o,&r));
    r.owner_token=44; o.active=false;
    assert(!g5_canvas_pan_authorized(&c,&o,&r));
    o.active=true; r.dx_pixels=33;
    assert(!g5_canvas_pan_authorized(&c,&o,&r));
    assert(memcmp(&old,&c,sizeof c)==0);
    o.owner_token=0; r.dx_pixels=8;
    assert(!g5_canvas_pan_authorized(&c,&o,&r));
    assert(memcmp(&old,&c,sizeof c)==0);
    puts("[g5-wp05] owner-generation pan prototype checks passed");
    return 0;
}
