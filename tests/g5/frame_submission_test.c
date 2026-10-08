#include <assert.h>
#include <stdint.h>
#include <aurora/g5_frame_submission.h>

static struct aurora_graphics_surface surface;
bool graphics_surface_lookup(struct aurora_cap_table *caps,aurora_cap_handle h,
 uint64_t rights,struct aurora_graphics_surface **out) {
 if(!caps||h!=17||!out||(rights&~(AURORA_RIGHT_READ|AURORA_RIGHT_CONTROL)))return false;
 *out=&surface;return true;
}
bool graphics_surface_read_committed(struct aurora_cap_table *caps,aurora_cap_handle h,
 struct aurora_graphics_surface_snapshot *out) {
 if(!caps||h!=17||!out)return false;
 *out=surface.committed;return true;
}
int main(void) {
 struct g5_frame_submission f={0};
 struct aurora_cap_table *caps=(void *)(uintptr_t)32;
 struct aurora_graphics_surface_snapshot snapshot={0};
 uint32_t slot=UINT32_MAX;
 uint64_t config=0,serial=0,request_config=0;
 surface.object_id=7;surface.generation=2;
 surface.state=AURORA_GRAPHICS_SURFACE_READY;
 assert(g5_frame_submission_begin(&f,11));
 assert(g5_surface_registry_attach(&f.registry,caps,17,&slot));
 assert(!g5_frame_submission_request(&f,slot,1,&request_config));
 assert(g5_surface_bridge_configure(&f.registry.entries[slot].bridge,
        &f.registry.session,1280,720,&config));
 assert(g5_surface_bridge_ack(&f.registry.entries[slot].bridge,&f.registry.session,config));
 assert(g5_frame_submission_request(&f,slot,1,&request_config)&&request_config==config);
 assert(!g5_frame_submission_request(&f,slot,1,&request_config));
 assert(!g5_frame_submission_complete(&f,slot,1,config+1,&snapshot,&serial));
 assert(g5_frame_submission_complete(&f,slot,1,config,&snapshot,&serial)&&serial==1);
 assert(!g5_frame_submission_complete(&f,slot,1,config,&snapshot,&serial));
 assert(g5_frame_submission_request(&f,slot,2,&request_config));
 assert(g5_frame_submission_cancel(&f,2));
 assert(!g5_frame_submission_complete(&f,slot,2,config,&snapshot,&serial));
 assert(g5_frame_submission_request(&f,slot,3,&request_config));
 surface.generation=3;
 assert(!g5_frame_submission_complete(&f,slot,3,config,&snapshot,&serial));
 surface.generation=2;
 g5_frame_submission_end(&f);
 assert(!g5_frame_submission_complete(&f,slot,3,config,&snapshot,&serial));
 assert(!g5_frame_submission_request(&f,slot,4,&request_config));
 assert(g5_frame_submission_begin(&f,12));
 assert(g5_surface_registry_attach(&f.registry,caps,17,&slot));
 assert(!g5_frame_submission_complete(&f,slot,3,config,&snapshot,&serial));
 g5_frame_submission_end(&f);
 return 0;
}
