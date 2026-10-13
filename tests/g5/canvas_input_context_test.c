#include <aurora/g5_canvas_input_context.h>
#include <assert.h>
#include <stdio.h>

int main(void) {
    struct g5_canvas_input_context c={0};
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_APP);
    assert(g5_canvas_zoom_factor(G5_CANVAS_INPUT_APP)==0u);
    assert(g5_canvas_zoom_factor(G5_CANVAS_INPUT_ZOOM_IN)==81920u);
    assert(g5_canvas_zoom_factor(G5_CANVAS_INPUT_ZOOM_OUT)==52429u);
    c.canvas_navigation=1;
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_ZOOM_IN);
    assert(g5_canvas_resolve_wheel(&c,-1)==G5_CANVAS_INPUT_ZOOM_OUT);
    assert(g5_canvas_resolve_wheel(&c,0)==G5_CANVAS_INPUT_APP);
    c.right_held=1;
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_SWITCH_PREVIOUS);
    assert(g5_canvas_resolve_wheel(&c,-1)==G5_CANVAS_INPUT_SWITCH_NEXT);
    c.right_wheel_used=1;
    assert(g5_canvas_resolve_right_release(&c)==G5_CANVAS_INPUT_SWITCH_COMMIT);
    c.app_control_active=1;
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_APP);
    assert(g5_canvas_resolve_keys(&c,1)==G5_CANVAS_INPUT_APP);
    assert(g5_canvas_resolve_right_release(&c)==G5_CANVAS_INPUT_APP);
    c.app_control_active=0;c.modal_active=1;
    assert(g5_canvas_resolve_wheel(&c,-1)==G5_CANVAS_INPUT_APP);
    c.modal_active=0;c.right_held=0;
    c.pointer_over_window=1;
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_APP);
    c.left_drag_on_background=1;
    assert(g5_canvas_resolve_drag(&c)==G5_CANVAS_INPUT_APP);
    c.pointer_over_window=0;
    assert(g5_canvas_resolve_drag(&c)==G5_CANVAS_INPUT_PAN_DRAG);
    assert(g5_canvas_resolve_keys(&c,1)==G5_CANVAS_INPUT_PAN_KEYS);
    assert(g5_canvas_resolve_keys(&c,0)==G5_CANVAS_INPUT_APP);
    struct g5_integer_camera live={0},staged={0};
    assert(g5_icam_init(&live,0,0,G5_ICAM_ONE,800,600));
    const uint64_t rev=live.revision;
    assert(g5_canvas_stage_wheel_zoom(&live,G5_CANVAS_INPUT_ZOOM_IN,
                                      400,300,&staged));
    assert(live.zoom_q16==G5_ICAM_ONE && live.revision==rev);
    assert(staged.zoom_q16==81920u && staged.revision==rev+1u);
    assert(!g5_canvas_accept_zoom_commit(&live,&staged,rev,false));
    assert(live.zoom_q16==G5_ICAM_ONE);
    assert(!g5_canvas_accept_zoom_commit(&live,&staged,rev+1u,true));
    assert(g5_canvas_accept_zoom_commit(&live,&staged,rev,true));
    assert(live.zoom_q16==81920u);
    assert(!g5_canvas_accept_zoom_commit(&live,&staged,rev,true));
    assert(!g5_canvas_stage_wheel_zoom(&live,G5_CANVAS_INPUT_APP,
                                       400,300,&staged));
    /* Off-centre wheel zoom must preserve the world point beneath the
     * cursor (within one world tick of integer rounding). */
    struct g5_integer_camera offcenter={0},candidate={0};
    int64_t before_x=0,before_y=0,after_x=0,after_y=0;
    assert(g5_icam_init(&offcenter,123456,-789012,G5_ICAM_ONE,801,601));
    assert(g5_icam_unproject(&offcenter,offcenter.revision,713,109,
                             &before_x,&before_y));
    assert(g5_canvas_stage_wheel_zoom(&offcenter,G5_CANVAS_INPUT_ZOOM_IN,
                                      713,109,&candidate));
    assert(g5_icam_unproject(&candidate,candidate.revision,713,109,
                             &after_x,&after_y));
    assert(before_x>=after_x-1 && before_x<=after_x+1);
    assert(before_y>=after_y-1 && before_y<=after_y+1);
    assert(offcenter.center_x_ticks==123456);
    assert(offcenter.center_y_ticks==-789012);
    assert(!g5_canvas_stage_wheel_zoom(&offcenter,G5_CANVAS_INPUT_ZOOM_IN,
                                       -1,109,&candidate));
    assert(!g5_canvas_stage_wheel_zoom(&offcenter,G5_CANVAS_INPUT_ZOOM_IN,
                                       713,602,&candidate));
    /* A rejected out-of-range zoom cannot mutate either camera. */
    struct g5_integer_camera at_limit={0};
    assert(g5_icam_init(&at_limit,0,0,G5_ICAM_MAX_ZOOM,800,600));
    assert(!g5_canvas_stage_wheel_zoom(&at_limit,G5_CANVAS_INPUT_ZOOM_IN,
                                       400,300,&candidate));
    assert(at_limit.zoom_q16==G5_ICAM_MAX_ZOOM);
    puts("G5 contextual input resolver and staged zoom: PASS");
    return 0;
}
