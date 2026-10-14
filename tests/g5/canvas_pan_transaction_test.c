#include <aurora/g5_canvas_pan_transaction.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
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
    puts("[g5-wp05] pan transaction staging checks passed");
    return 0;
}
