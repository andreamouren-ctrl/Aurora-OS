#include <aurora/g5_camera_origin.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
    int64_t x=99,y=88;
    assert(g5_camera_origin_stage(0,0,8,-4,&x,&y));
    assert(x==-8192 && y==4096);
    assert(g5_camera_origin_stage(x,y,-8,4,&x,&y));
    assert(x==0 && y==0);
    x=99;y=88;
    assert(!g5_camera_origin_stage(INT64_MIN,0,1,0,&x,&y));
    assert(x==99 && y==88);
    assert(!g5_camera_origin_stage(INT64_MAX,0,-1,0,&x,&y));
    assert(x==99 && y==88);
    assert(!g5_camera_origin_stage(0,INT64_MIN,0,1,&x,&y));
    assert(x==99 && y==88);
    assert(!g5_camera_origin_stage(0,INT64_MAX,0,-1,&x,&y));
    assert(x==99 && y==88);
    assert(!g5_camera_origin_stage(0,0,33,0,&x,&y));
    assert(!g5_camera_origin_stage(0,0,0,0,&x,&y));
    assert(!g5_camera_origin_stage(0,0,1,0,&x,&x));
    assert(!g5_camera_origin_stage(0,0,1,0,NULL,&y));
    assert(!g5_camera_origin_stage(0,0,1,0,&x,NULL));
    assert(x==99 && y==88);
    puts("[g5-wp05] production origin staging passed");
    return 0;
}
