#include <aurora/g5_canvas_projection.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
static struct g5_world_point pt(int64_t x,int64_t y) {
    return (struct g5_world_point){x,y};
}
static struct g5_canvas_panel panel(uint64_t id,int64_t lx,int64_t ly,
                                   int64_t rx,int64_t ry) {
    return (struct g5_canvas_panel){
        .object_id=id,.object_generation=1u,
        .bounds={pt(lx,ly),pt(rx,ry)}
    };
}
int main(void) {
    struct g5_camera camera;
    assert(g5_camera_init(&camera,pt(0,0),1024,768,1.0,1.0));
    const uint64_t camrev=camera.revision,scene=7,session=42;
    struct g5_canvas_panel p=panel(1,-1024,-1024,1024,1024);
    struct g5_canvas_projected_panel r={0};
    bool visible=false;
    assert(g5_canvas_project_panel(&camera,camrev,scene,scene,session,
                                   &p,&r,&visible));
    assert(visible && r.object_id==1u && r.object_generation==1u);
    assert(r.width_px>0u && r.height_px>0u);
    assert(r.x_px<=512 && r.x_px+(int32_t)r.width_px>512);

    struct g5_canvas_projected_panel old=r;
    assert(!g5_canvas_project_panel(&camera,camrev,scene,8,session,
                                    &p,&r,&visible));
    assert(!g5_canvas_project_panel(&camera,camrev,scene,scene,0,
                                    &p,&r,&visible));
    assert(memcmp(&old,&r,sizeof(r))==0);
    assert(g5_camera_pan(&camera,16,0));
    assert(!g5_canvas_project_panel(&camera,camrev,scene,scene,session,
                                    &p,&r,&visible));
    assert(memcmp(&old,&r,sizeof(r))==0);

    /* A world-spanning AABB covers the full viewport when centered. */
    assert(g5_camera_init(&camera,pt(0,0),1024,768,1.0,1.0));
    p=panel(9,INT64_MIN,INT64_MIN,INT64_MAX,INT64_MAX);
    assert(g5_canvas_project_panel(&camera,camera.revision,scene,scene,
                                   session,&p,&r,&visible));
    assert(visible && r.x_px==0 && r.y_px==0 &&
           r.width_px==1024 && r.height_px==768);
    /* Near extreme anchors, still clip before converting to int32. */
    assert(g5_camera_init(&camera,pt(INT64_MAX-100,INT64_MIN+100),
                          1024,768,1.0,1.0));
    p=panel(9,INT64_MAX-200,INT64_MIN,INT64_MAX,INT64_MIN+200);
    assert(g5_canvas_project_panel(&camera,camera.revision,scene,scene,
                                   session,&p,&r,&visible));
    assert(visible && r.x_px<=512 && r.y_px<=384);
    p=panel(10,INT64_MIN,INT64_MIN,INT64_MIN+1,INT64_MIN+1);
    assert(g5_canvas_project_panel(&camera,camera.revision,scene,scene,
                                   session,&p,&r,&visible));
    assert(!visible);

    assert(g5_camera_init(&camera,pt(0,0),1024,768,1.0,1.0));
    struct g5_canvas_panel panels[3]={
        panel(1,-1024,-1024,1024,1024),
        panel(2,INT64_MAX-200,INT64_MAX-200,INT64_MAX,INT64_MAX),
        panel(3,-524288,-393216,-523264,-392192)
    };
    struct g5_canvas_projected_panel results[3];
    memset(results,0x7a,sizeof(results));
    struct g5_canvas_projected_panel sentinel[3];
    memcpy(sentinel,results,sizeof(sentinel));
    size_t used=99;
    assert(g5_canvas_project_batch(&camera,camera.revision,scene,scene,
                                   session,panels,3,results,3,&used));
    assert(used==2 && results[0].object_id==1u &&
           results[1].object_id==3u);
    struct g5_canvas_projected_panel saved[3];
    memcpy(saved,results,sizeof(saved));
    used=99;
    panels[2].object_id=1;
    assert(!g5_canvas_project_batch(&camera,camera.revision,scene,scene,
                                    session,panels,3,results,3,&used));
    assert(used==99 && memcmp(results,saved,sizeof(saved))==0);
    panels[2].object_id=3;
    panels[2].bounds.min.x=INT64_MAX;
    assert(!g5_canvas_project_batch(&camera,camera.revision,scene,scene,
                                    session,panels,3,results,3,&used));
    assert(used==99 && memcmp(results,saved,sizeof(saved))==0);
    panels[2]=panel(3,-524288,-393216,-523264,-392192);
    assert(!g5_canvas_project_batch(&camera,camera.revision,scene,scene,
                                    session,panels,3,results,2,&used));
    assert(used==99 && memcmp(results,saved,sizeof(saved))==0);
    assert(!g5_canvas_project_batch(&camera,camera.revision,scene,scene,
                                    session,panels,65,results,3,&used));
    assert(used==99 && memcmp(results,saved,sizeof(saved))==0);
    puts("[g5-wp05] viewport projection and atomic batch gate passed");
    return 0;
}
