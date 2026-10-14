#include <aurora/g5_canvas_integer_pan.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    struct g5_integer_pan s={0,0,1}, before;
    assert(g5_integer_pan_apply(&s,8,-8,1));
    assert(s.x_ticks==-8192 && s.y_ticks==8192 && s.revision==2);
    before=s;
    assert(!g5_integer_pan_apply(&s,1,1,1));
    assert(!g5_integer_pan_apply(&s,33,0,2));
    assert(!g5_integer_pan_apply(&s,0,-33,2));
    assert(memcmp(&s,&before,sizeof s)==0);
    s.x_ticks=INT64_MIN; s.y_ticks=0; s.revision=3;
    before=s;
    assert(!g5_integer_pan_apply(&s,1,0,3));
    assert(memcmp(&s,&before,sizeof s)==0);
    s.x_ticks=INT64_MAX; s.y_ticks=0; s.revision=4;
    before=s;
    assert(!g5_integer_pan_apply(&s,-1,0,4));
    assert(memcmp(&s,&before,sizeof s)==0);
    s.x_ticks=0; s.y_ticks=INT64_MAX; s.revision=5;
    before=s;
    assert(!g5_integer_pan_apply(&s,0,-1,5));
    assert(memcmp(&s,&before,sizeof s)==0);
    s.x_ticks=0; s.y_ticks=INT64_MIN; s.revision=6;
    before=s;
    assert(!g5_integer_pan_apply(&s,0,1,6));
    assert(memcmp(&s,&before,sizeof s)==0);
    s.x_ticks=0; s.y_ticks=0; s.revision=UINT64_MAX;
    before=s;
    assert(!g5_integer_pan_apply(&s,0,0,UINT64_MAX));
    assert(memcmp(&s,&before,sizeof s)==0);
    puts("[g5-wp05-integer] bounded translation checks passed");
    return 0;
}
