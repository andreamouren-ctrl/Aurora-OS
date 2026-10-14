#include <aurora/g5_canvas_session_camera.h>
#include <assert.h>
#include <stdio.h>
static bool present_yes(void *p,const struct g5_canvas_session_camera *next) {
    assert(next && g5_camera_valid(&next->camera));
    assert(next->scene_revision==5 && next->session_generation==10);
    ++*(unsigned *)p; return true;
}
static bool present_no(void *p,const struct g5_canvas_session_camera *next) {
    assert(next && g5_camera_valid(&next->camera));
    ++*(unsigned *)p; return false;
}
struct two_node_presenter {
    unsigned calls;
    bool accept;
    int32_t x[2], y[2];
};
static bool present_two_nodes(void *context,
    const struct g5_canvas_session_camera *proposed) {
    struct two_node_presenter *p=context;
    int32_t x[2],y[2];
    ++p->calls;
    if (!g5_camera_world_to_screen(&proposed->camera,
        (struct g5_world_point){0,0},&x[0],&y[0]) ||
        !g5_camera_world_to_screen(&proposed->camera,
        (struct g5_world_point){102400,51200},&x[1],&y[1]))
        return false;
    if (!p->accept) return false;
    for (unsigned i=0;i<2;++i) { p->x[i]=x[i];p->y[i]=y[i]; }
    return true;
}
struct serialized_presenter {
    bool locked;
    bool accept;
    unsigned locks,unlocks,presents;
};
static bool acquire_serial(void *context) {
    struct serialized_presenter *p=context;
    if (p->locked) return false;
    p->locked=true;
    ++p->locks;
    return true;
}
static void release_serial(void *context) {
    struct serialized_presenter *p=context;
    assert(p->locked);
    p->locked=false;
    ++p->unlocks;
}
static bool publish_serial(void *context,
    const struct g5_canvas_session_camera *next) {
    struct serialized_presenter *p=context;
    assert(p->locked && next && g5_camera_valid(&next->camera));
    ++p->presents;
    return p->accept;
}
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
    unsigned presentations=0;
    assert(!g5_canvas_session_camera_present_pan(
        &s,10,5,7,3,0,present_no,&presentations));
    assert(presentations==1 && s.camera.revision==7);
    assert(!g5_canvas_session_camera_present_pan(
        &s,10,6,7,3,0,present_yes,&presentations));
    assert(presentations==1 && s.camera.revision==7);
    assert(g5_canvas_session_camera_present_pan(
        &s,10,5,7,3,0,present_yes,&presentations));
    assert(presentations==2 && s.camera.revision==8);
    assert(!g5_canvas_session_camera_present_zoom(
        &s,10,5,8,1.5,400,300,present_no,&presentations));
    assert(presentations==3 && s.camera.revision==8);
    assert(g5_canvas_session_camera_present_zoom(
        &s,10,5,8,1.5,400,300,present_yes,&presentations));
    assert(presentations==4 && s.camera.revision==9);
    assert(!g5_canvas_session_camera_present_pan(
        &s,10,5,8,3,0,present_yes,&presentations));
    assert(presentations==4);
    /* Two independent world points project consistently after pan/zoom. */
    int32_t x0=0,y0=0,x1=0,y1=0;
    assert(g5_camera_world_to_screen(&s.camera,
        (struct g5_world_point){0,0},&x0,&y0));
    assert(g5_camera_world_to_screen(&s.camera,
        (struct g5_world_point){102400,51200},&x1,&y1));
    assert(x1-x0==450 && y1-y0==225);
    assert(s.camera.revision==9 && s.scene_revision==5);
    struct g5_canvas_session_camera nodes={0};
    struct two_node_presenter pair={.accept=true};
    assert(g5_canvas_session_camera_init(&nodes,50,7,
        (struct g5_world_point){0,0},800,600));
    assert(present_two_nodes(&pair,&nodes));
    assert(pair.x[0]==400 && pair.y[0]==300);
    assert(pair.x[1]==500 && pair.y[1]==350);
    assert(g5_canvas_session_camera_present_pan(&nodes,50,7,1,
        16,-8,present_two_nodes,&pair));
    assert(nodes.camera.revision==2 && nodes.scene_revision==7);
    assert(pair.x[0]==416 && pair.y[0]==292);
    assert(pair.x[1]==516 && pair.y[1]==342);
    pair.accept=false;
    assert(!g5_canvas_session_camera_present_pan(&nodes,50,7,2,
        8,8,present_two_nodes,&pair));
    assert(nodes.camera.revision==2);
    assert(pair.x[0]==416 && pair.y[0]==292);
    pair.accept=true;
    assert(g5_canvas_session_camera_present_zoom(&nodes,50,7,2,
        2,400,300,present_two_nodes,&pair));
    assert(nodes.camera.revision==3 && nodes.camera.zoom==2);
    assert(pair.x[0]==432 && pair.y[0]==284);
    assert(pair.x[1]==632 && pair.y[1]==384);
    /* Rejected operations must never invoke the presenter. */
    const unsigned calls_before=pair.calls;
    assert(!g5_canvas_session_camera_present_pan(&nodes,50,7,2,
        4,0,present_two_nodes,&pair)); /* stale camera revision */
    assert(!g5_canvas_session_camera_present_zoom(&nodes,50,7,3,
        1,400,300,present_two_nodes,&pair)); /* no-op zoom */
    assert(!g5_canvas_session_camera_present_zoom(&nodes,50,7,3,
        100,400,300,present_two_nodes,&pair)); /* zoom limit */
    assert(!g5_canvas_session_camera_present_pan(&nodes,50,7,3,
        65537,0,present_two_nodes,&pair)); /* pan limit */
    assert(pair.calls==calls_before && nodes.camera.revision==3);
    /* Revision saturation must fail closed without presenting. */
    nodes.camera.revision=UINT64_MAX;
    assert(!g5_canvas_session_camera_present_pan(&nodes,50,7,
        UINT64_MAX,1,0,present_two_nodes,&pair));
    assert(pair.calls==calls_before && nodes.camera.revision==UINT64_MAX);
    /* Session logout revokes camera authority and rejects replay. */
    g5_canvas_session_camera_revoke(&nodes);
    assert(nodes.session_generation==0 && nodes.scene_revision==0);
    assert(!g5_camera_valid(&nodes.camera));
    assert(!g5_canvas_session_camera_present_pan(&nodes,50,7,3,
        1,0,present_two_nodes,&pair));
    assert(pair.calls==calls_before);
    g5_canvas_session_camera_revoke(&nodes); /* idempotent */
    g5_canvas_session_camera_revoke(NULL);
    assert(g5_canvas_session_camera_init(&nodes,51,1,
        (struct g5_world_point){0,0},800,600));
    assert(!g5_canvas_session_camera_present_pan(&nodes,50,7,3,
        1,0,present_two_nodes,&pair)); /* previous session replay */
    assert(g5_canvas_session_camera_present_pan(&nodes,51,1,1,
        1,0,present_two_nodes,&pair));
    /* Bounded scene projection is fail-atomic and revision fenced. */
    const struct g5_world_point anchors[2]={{0,0},{102400,51200}};
    int32_t px[2]={-1,-1},py[2]={-1,-1};
    assert(g5_canvas_session_camera_project_nodes(&nodes,51,1,2,
        anchors,2,px,py,2));
    assert(px[0]==401 && py[0]==300);
    assert(px[1]==501 && py[1]==350);
    px[0]=-1;py[0]=-1;
    assert(!g5_canvas_session_camera_project_nodes(&nodes,51,1,1,
        anchors,2,px,py,2));
    assert(px[0]==-1 && py[0]==-1);
    assert(!g5_canvas_session_camera_project_nodes(&nodes,51,1,2,
        anchors,2,px,py,1));
    assert(px[0]==-1 && py[0]==-1);
    assert(!g5_canvas_session_camera_project_nodes(&nodes,51,1,2,
        anchors,65,px,py,65));
    assert(px[0]==-1 && py[0]==-1);
    /* Aliased output ranges must not corrupt each other on publication. */
    int32_t overlapping[3]={-7,-8,-9};
    assert(!g5_canvas_session_camera_project_nodes(&nodes,51,1,2,
        anchors,2,overlapping,overlapping+1,2));
    assert(overlapping[0]==-7 && overlapping[1]==-8 &&
           overlapping[2]==-9);
    /* Pointer coordinates are camera-local and cannot use stale revisions. */
    struct g5_world_point hit={-1,-1};
    assert(g5_canvas_session_camera_pointer_to_world(&nodes,51,1,2,
        401,300,&hit));
    assert(hit.x==0 && hit.y==0);
    hit=(struct g5_world_point){-1,-1};
    assert(!g5_canvas_session_camera_pointer_to_world(&nodes,51,1,1,
        401,300,&hit));
    assert(!g5_canvas_session_camera_pointer_to_world(&nodes,50,1,2,
        401,300,&hit));
    assert(!g5_canvas_session_camera_pointer_to_world(&nodes,51,2,2,
        401,300,&hit));
    assert(!g5_canvas_session_camera_pointer_to_world(&nodes,51,1,2,
        800,300,&hit));
    assert(!g5_canvas_session_camera_pointer_to_world(&nodes,51,1,2,
        -1,300,&hit));
    assert(hit.x==-1 && hit.y==-1);
    /* Scene topology changes invalidate stale scene requests, not camera. */
    assert(g5_canvas_session_camera_advance_scene(&nodes,51,1,2));
    assert(nodes.scene_revision==2 && nodes.camera.revision==2);
    assert(!g5_canvas_session_camera_pointer_to_world(&nodes,51,1,2,
        401,300,&hit));
    assert(g5_canvas_session_camera_pointer_to_world(&nodes,51,2,2,
        401,300,&hit));
    assert(!g5_canvas_session_camera_advance_scene(&nodes,51,1,2));
    assert(!g5_canvas_session_camera_advance_scene(&nodes,51,2,1));
    assert(nodes.scene_revision==2 && nodes.camera.revision==2);
    nodes.scene_revision=UINT64_MAX;
    assert(!g5_canvas_session_camera_advance_scene(&nodes,51,
        UINT64_MAX,2));
    assert(nodes.scene_revision==UINT64_MAX);
    /* Resize must be presented before publishing the new camera revision. */
    struct g5_canvas_session_camera resized={0},candidate={0};
    assert(g5_canvas_session_camera_init(&resized,60,4,
        (struct g5_world_point){0,0},800,600));
    assert(g5_canvas_session_camera_stage_viewport(&resized,60,4,1,
        1024,768,&candidate));
    assert(resized.camera.width_px==800 && resized.camera.revision==1);
    assert(candidate.camera.width_px==1024 &&
           candidate.camera.height_px==768 && candidate.camera.revision==2);
    pair.accept=false;
    const unsigned resize_calls=pair.calls;
    assert(!g5_canvas_session_camera_present_viewport(&resized,60,4,1,
        1024,768,present_two_nodes,&pair));
    assert(pair.calls==resize_calls+1 && resized.camera.revision==1);
    pair.accept=true;
    assert(g5_canvas_session_camera_present_viewport(&resized,60,4,1,
        1024,768,present_two_nodes,&pair));
    assert(resized.camera.width_px==1024 &&
           resized.camera.height_px==768 && resized.camera.revision==2);
    const unsigned accepted_resize_calls=pair.calls;
    assert(!g5_canvas_session_camera_present_viewport(&resized,60,4,1,
        1200,900,present_two_nodes,&pair));
    assert(!g5_canvas_session_camera_present_viewport(&resized,60,4,2,
        1024,768,present_two_nodes,&pair));
    assert(!g5_canvas_session_camera_present_viewport(&resized,60,4,2,
        0,768,present_two_nodes,&pair));
    assert(pair.calls==accepted_resize_calls);
    /* Scene transaction staging must not publish a topology revision. */
    struct g5_canvas_session_camera scene_next={0};
    assert(g5_canvas_session_camera_stage_scene(&resized,60,4,2,
        &scene_next));
    assert(resized.scene_revision==4 && scene_next.scene_revision==5);
    assert(scene_next.camera.revision==resized.camera.revision);
    assert(!g5_canvas_session_camera_commit_scene(&resized,&scene_next,
        60,4,1)); /* stale camera */
    assert(resized.scene_revision==4);
    scene_next.camera.anchor.x=1;
    assert(!g5_canvas_session_camera_commit_scene(&resized,&scene_next,
        60,4,2)); /* camera mutation forbidden in topology commit */
    scene_next.camera.anchor.x=0;
    assert(g5_canvas_session_camera_commit_scene(&resized,&scene_next,
        60,4,2));
    assert(resized.scene_revision==5 && resized.camera.revision==2);
    assert(!g5_canvas_session_camera_commit_scene(&resized,&scene_next,
        60,4,2)); /* replay */
    /* Publication and camera commit must share the exclusive lock. */
    struct serialized_presenter serial={0};
    struct g5_canvas_session_camera locked={0};
    assert(g5_canvas_session_camera_init(&locked,70,3,
        (struct g5_world_point){0,0},800,600));
    serial.accept=true;
    assert(g5_canvas_session_camera_locked_pan(&locked,70,3,1,
        2,0,acquire_serial,release_serial,publish_serial,&serial));
    assert(locked.camera.revision==2);
    assert(serial.locks==1 && serial.unlocks==1 &&
           serial.presents==1 && !serial.locked);
    serial.accept=false;
    assert(!g5_canvas_session_camera_locked_pan(&locked,70,3,2,
        2,0,acquire_serial,release_serial,publish_serial,&serial));
    assert(locked.camera.revision==2);
    assert(serial.locks==2 && serial.unlocks==2 &&
           serial.presents==2 && !serial.locked);
    serial.locked=true;
    assert(!g5_canvas_session_camera_locked_pan(&locked,70,3,2,
        2,0,acquire_serial,release_serial,publish_serial,&serial));
    assert(serial.presents==2 && serial.unlocks==2);
    /* Zoom and resize share the same serialization discipline. */
    serial.accept=true;
    assert(g5_canvas_session_camera_locked_zoom(&locked,70,3,2,
        2.0,400,300,acquire_serial,release_serial,publish_serial,&serial));
    assert(locked.camera.revision==3 && locked.camera.zoom==2.0);
    assert(g5_canvas_session_camera_locked_viewport(&locked,70,3,3,
        1024,768,acquire_serial,release_serial,publish_serial,&serial));
    assert(locked.camera.revision==4 && locked.camera.width_px==1024);
    serial.accept=false;
    assert(!g5_canvas_session_camera_locked_zoom(&locked,70,3,4,
        2.0,512,384,acquire_serial,release_serial,publish_serial,&serial));
    assert(locked.camera.revision==4 && locked.camera.zoom==2.0);
    assert(!g5_canvas_session_camera_locked_viewport(&locked,70,3,4,
        1200,900,acquire_serial,release_serial,publish_serial,&serial));
    assert(locked.camera.width_px==1024);
    assert(serial.locks==serial.unlocks); /* failed lock did not acquire */
    serial.locked=false;
    assert(serial.locks==serial.unlocks);
    puts("[g5-wp05] Ring3 serialized pan zoom viewport passed");
    return 0;
}
