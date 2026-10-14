#include <aurora/g5_canvas_session_camera.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
    struct g5_canvas_session_camera s={0};
    const struct g5_world_point origin={0,0};
    assert(!g5_canvas_session_camera_init(&s,0,5,origin,800,600));
    assert(g5_canvas_session_camera_init(&s,10,5,origin,800,600));
    assert(s.camera.revision==1 && s.scene_revision==5);
    assert(!g5_canvas_session_camera_pan(&s,11,5,1,8,0));
    assert(!g5_canvas_session_camera_pan(&s,10,6,1,8,0));
    assert(!g5_canvas_session_camera_pan(&s,10,5,1,0,0));
    assert(s.camera.revision==1);
    assert(g5_canvas_session_camera_pan(&s,10,5,1,8,-4));
    assert(s.camera.revision==2 && s.scene_revision==5);
    assert(s.camera.anchor.x==-8192 && s.camera.anchor.y==4096);
    assert(!g5_canvas_session_camera_pan(&s,10,5,1,8,0));
    assert(g5_canvas_session_camera_pan(&s,10,5,2,-8,4));
    assert(s.camera.anchor.x==0 && s.camera.anchor.y==0);
    assert(!g5_canvas_session_camera_zoom(&s,10,6,3,2,400,300));
    assert(!g5_canvas_session_camera_zoom(&s,10,5,3,1,400,300));
    assert(g5_canvas_session_camera_zoom(&s,10,5,3,2,400,300));
    assert(s.camera.revision==4 && s.camera.zoom==2.0);
    assert(s.scene_revision==5);
    assert(!g5_canvas_session_camera_zoom(&s,10,5,3,2,400,300));
    assert(g5_canvas_session_camera_pan(&s,10,5,4,4,0));
    assert(s.camera.revision==5);
    /* Stage is invisible until the authorized compositor confirms present. */
    struct g5_canvas_session_camera staged={0};
    assert(g5_canvas_session_camera_stage_pan(&s,10,5,5,12,0,&staged));
    assert(s.camera.revision==5 && staged.camera.revision==6);
    assert(!g5_canvas_session_camera_commit(&s,&staged,10,6,5));
    assert(s.camera.revision==5);
    assert(!g5_canvas_session_camera_commit(&s,&staged,11,5,5));
    assert(s.camera.revision==5);
    assert(g5_canvas_session_camera_commit(&s,&staged,10,5,5));
    assert(s.camera.revision==6 && s.scene_revision==5);
    assert(!g5_canvas_session_camera_commit(&s,&staged,10,5,5));
    assert(!g5_canvas_session_camera_stage_pan(&s,10,5,6,1,0,&s));
    assert(s.camera.revision==6);
    assert(g5_canvas_session_camera_stage_zoom(&s,10,5,6,1.5,400,300,&staged));
    assert(staged.camera.revision==7 && s.camera.revision==6);
    assert(g5_canvas_session_camera_commit(&s,&staged,10,5,6));
    assert(s.camera.revision==7 && s.camera.zoom==3.0);
    puts("[g5-wp05] Ring3 camera session, staging and zoom passed");
    return 0;
}
