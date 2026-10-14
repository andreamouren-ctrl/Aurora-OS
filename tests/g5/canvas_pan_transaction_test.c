#include <aurora/g5_canvas_pan_transaction.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
static bool present_ok(void *ctx) { ++*(unsigned *)ctx; return true; }
static bool present_fail(void *ctx) { ++*(unsigned *)ctx; return false; }
int main(void) {
    const struct g5_canvas_pan_owner owner={11,22,33,44,55,true};
    const struct g5_canvas_pan_request request={11,22,33,44,1,55,8,-8};
    const struct g5_canvas_pan_transaction current={{0,0,1},55};
    struct g5_canvas_pan_transaction staged={{123,456,9},99},snapshot=staged;
    assert(g5_canvas_pan_prepare(&current,&owner,&request,&staged));
    assert(staged.camera.x_ticks==-8192 && staged.camera.y_ticks==8192);
    assert(staged.camera.revision==2 && staged.scene_revision==56);
    /* Staging must never change live camera or live scene. */
    assert(current.camera.x_ticks==0 && current.camera.y_ticks==0);
    assert(current.camera.revision==1 && current.scene_revision==55);
    staged=snapshot;
    struct g5_canvas_pan_transaction aliased=current;
    assert(!g5_canvas_pan_prepare(&aliased,&owner,&request,&aliased));
    assert(memcmp(&aliased,&current,sizeof aliased)==0);
    struct g5_canvas_pan_request stale=request;
    stale.expected_scene_revision=54;
    assert(!g5_canvas_pan_prepare(&current,&owner,&stale,&staged));
    assert(memcmp(&staged,&snapshot,sizeof staged)==0);
    struct g5_canvas_pan_transaction exhausted=current;
    exhausted.scene_revision=UINT64_MAX;
    assert(!g5_canvas_pan_prepare(&exhausted,&owner,&request,&staged));
    assert(memcmp(&staged,&snapshot,sizeof staged)==0);
    assert(!g5_canvas_pan_prepare(&current,&owner,&request,NULL));
    assert(!g5_canvas_pan_prepare(NULL,&owner,&request,&staged));
    assert(!g5_canvas_pan_prepare(&current,NULL,&request,&staged));
    assert(!g5_canvas_pan_prepare(&current,&owner,NULL,&staged));
    assert(memcmp(&staged,&snapshot,sizeof staged)==0);
    struct g5_canvas_pan_transaction live=current;
    unsigned calls=0;
    assert(!g5_canvas_pan_present_commit(&live,&owner,&request,
                                         present_fail,&calls));
    assert(calls==1 && memcmp(&live,&current,sizeof live)==0);
    assert(g5_canvas_pan_present_commit(&live,&owner,&request,
                                        present_ok,&calls));
    assert(calls==2 && live.camera.revision==2 && live.scene_revision==56);
    assert(live.camera.x_ticks==-8192 && live.camera.y_ticks==8192);
    assert(!g5_canvas_pan_present_commit(&live,&owner,&request,
                                         present_ok,&calls));
    assert(calls==2); /* stale revisions never reach presentation */
    assert(!g5_canvas_pan_present_commit(&live,&owner,&request,
                                         NULL,&calls));
    assert(calls==2);
    /* The authoritative owner must be refreshed after each committed scene. */
    struct g5_canvas_pan_owner next_owner=owner;
    struct g5_canvas_pan_request next_request=request;
    next_owner.scene_revision=56;
    next_request.expected_scene_revision=56;
    next_request.expected_camera_revision=2;
    next_request.dx_pixels=-8;
    next_request.dy_pixels=8;
    assert(g5_canvas_pan_present_commit(&live,&next_owner,&next_request,
                                        present_ok,&calls));
    assert(calls==3 && live.camera.revision==3 && live.scene_revision==57);
    assert(live.camera.x_ticks==0 && live.camera.y_ticks==0);
    /* Replaying an already committed message cannot present again. */
    assert(!g5_canvas_pan_present_commit(&live,&next_owner,&next_request,
                                         present_ok,&calls));
    assert(calls==3);
    puts("[g5-wp05] pan transaction staging and commit checks passed");
    return 0;
}
