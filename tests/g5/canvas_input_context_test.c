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
    uint32_t wheel_factor=0u;
    assert(g5_canvas_wheel_zoom_request(&c,1,&wheel_factor));
    assert(wheel_factor==G5_CANVAS_WHEEL_ZOOM_IN_Q16);
    assert(g5_canvas_wheel_zoom_request(&c,INT32_MAX,&wheel_factor));
    assert(wheel_factor==G5_CANVAS_WHEEL_ZOOM_IN_Q16);
    assert(g5_canvas_wheel_zoom_request(&c,INT32_MIN,&wheel_factor));
    assert(wheel_factor==G5_CANVAS_WHEEL_ZOOM_OUT_Q16);
    assert(!g5_canvas_wheel_zoom_request(&c,0,&wheel_factor));
    assert(!g5_canvas_wheel_zoom_request(&c,1,NULL));
    c.right_held=1;
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_SWITCH_PREVIOUS);
    assert(g5_canvas_resolve_wheel(&c,-1)==G5_CANVAS_INPUT_SWITCH_NEXT);
    assert(!g5_canvas_wheel_zoom_request(&c,1,&wheel_factor));
    c.right_wheel_used=1;
    assert(g5_canvas_resolve_right_release(&c)==G5_CANVAS_INPUT_SWITCH_COMMIT);
    c.app_control_active=1;
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_SWITCH_PREVIOUS);
    assert(g5_canvas_resolve_wheel(&c,-1)==G5_CANVAS_INPUT_SWITCH_NEXT);
    assert(!g5_canvas_wheel_zoom_request(&c,1,&wheel_factor));
    c.right_held=0;
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_APP);
    assert(g5_canvas_resolve_keys(&c,1)==G5_CANVAS_INPUT_APP);
    assert(g5_canvas_resolve_right_release(&c)==G5_CANVAS_INPUT_APP);
    c.app_control_active=0;c.modal_active=1;
    c.right_held=1;
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_APP);
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
    struct g5_integer_camera invalid=staged;
    invalid.width_px=801u;
    assert(!g5_canvas_accept_zoom_commit(&live,&invalid,rev,true));
    invalid=staged;invalid.zoom_q16=G5_ICAM_MAX_ZOOM+1u;
    assert(!g5_canvas_accept_zoom_commit(&live,&invalid,rev,true));
    invalid=staged;invalid.height_px=0u;
    assert(!g5_canvas_accept_zoom_commit(&live,&invalid,rev,true));
    assert(live.zoom_q16==G5_ICAM_ONE && live.revision==rev);
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
    /* Zoom-out is staged and gated exactly like zoom-in: no state
     * publication before a matching authoritative commit. */
    struct g5_integer_camera zoom_out={0},zoom_out_stage={0};
    assert(g5_icam_init(&zoom_out,1024,-2048,G5_ICAM_ONE,800,600));
    const uint64_t zoom_out_revision=zoom_out.revision;
    assert(g5_canvas_stage_wheel_zoom(&zoom_out,G5_CANVAS_INPUT_ZOOM_OUT,
                                       200,150,&zoom_out_stage));
    assert(zoom_out.zoom_q16==G5_ICAM_ONE);
    assert(zoom_out.center_x_ticks==1024 && zoom_out.center_y_ticks==-2048);
    assert(zoom_out_stage.zoom_q16==G5_CANVAS_WHEEL_ZOOM_OUT_Q16);
    assert(!g5_canvas_accept_zoom_commit(&zoom_out,&zoom_out_stage,
                                         zoom_out_revision,false));
    assert(zoom_out.zoom_q16==G5_ICAM_ONE);
    assert(g5_canvas_accept_zoom_commit(&zoom_out,&zoom_out_stage,
                                        zoom_out_revision,true));
    assert(zoom_out.zoom_q16==G5_CANVAS_WHEEL_ZOOM_OUT_Q16);
    assert(zoom_out.revision==zoom_out_revision+1u);
    /* A stale pan rejection can report a newer kernel revision.
     * It must not apply the speculative camera position or zoom. */
    struct g5_integer_camera rejected={0};
    assert(g5_icam_init(&rejected,123,-456,G5_ICAM_ONE,800,600));
    const uint64_t rejected_rev=rejected.revision;
    assert(!g5_icam_reconcile_rejected(&rejected,rejected_rev-1u));
    assert(!g5_icam_reconcile_rejected(&rejected,UINT64_MAX));
    assert(rejected.revision==rejected_rev);
    assert(g5_icam_reconcile_rejected(&rejected,rejected_rev+2u));
    assert(rejected.revision==rejected_rev+2u);
    assert(rejected.center_x_ticks==123 && rejected.center_y_ticks==-456);
    assert(rejected.zoom_q16==G5_ICAM_ONE);
    assert(!g5_icam_reconcile_rejected(&rejected,rejected_rev+1u));
    assert(g5_icam_reconcile_rejected(&rejected,rejected_rev+2u));
    assert(!g5_icam_reconcile_rejected(NULL,rejected_rev+2u));
    /* A world-space window stays fixed while the camera zooms. */
    struct g5_integer_camera projection={0};
    struct g5_icam_screen_rect original={0},enlarged={0};
    assert(g5_icam_init(&projection,0,0,G5_ICAM_ONE,800,600));
    assert(g5_icam_project_rect(&projection,0,0,102400u,51200u,&original));
    assert(original.x_px==400 && original.y_px==300);
    assert(original.width_px==100 && original.height_px==50);
    assert(g5_icam_zoom_at(&projection,G5_CANVAS_WHEEL_ZOOM_IN_Q16,400,300));
    assert(g5_icam_project_rect(&projection,0,0,102400u,51200u,&enlarged));
    assert(enlarged.x_px==400 && enlarged.y_px==300);
    assert(enlarged.width_px==125);
    assert(enlarged.height_px>=62 && enlarged.height_px<=63);
    assert(!g5_icam_project_rect(&projection,INT64_MAX,0,1024u,1024u,
                                 &enlarged));
    assert(!g5_icam_project_rect(&projection,0,0,0u,1024u,&enlarged));
    assert(!g5_icam_project_rect(&projection,0,0,1024u,0u,&enlarged));
    assert(!g5_icam_project_rect(&projection,0,0,1024u,1024u,NULL));
    puts("G5 contextual input resolver and staged zoom: PASS");
    return 0;
}
