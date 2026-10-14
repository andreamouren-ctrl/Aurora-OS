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
    puts("[g5-wp05] Ring3 camera two-node projection transactions passed");
    return 0;
}
