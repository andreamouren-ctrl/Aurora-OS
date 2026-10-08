#include <assert.h>
#include <stdint.h>
#include <aurora/g5_frame_delivery.h>
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
 struct g5_frame_delivery d={0};
 struct aurora_cap_table *caps=(void *)(uintptr_t)32;
 struct aurora_graphics_surface_snapshot snapshot={0};
 uint64_t config=0,req_config=0,serial=0;
 uint32_t slot=UINT32_MAX;
 surface.object_id=43;surface.generation=2;surface.state=AURORA_GRAPHICS_SURFACE_READY;
 assert(g5_frame_submission_begin(&f,10));
 assert(g5_surface_registry_attach(&f.registry,caps,17,&slot));
 assert(g5_surface_bridge_configure(&f.registry.entries[slot].bridge,&f.registry.session,600,400,&config));
 assert(g5_surface_bridge_ack(&f.registry.entries[slot].bridge,&f.registry.session,config));
 assert(g5_frame_delivery_bind(&d,&f));
 assert(!g5_frame_delivery_bind(&d,&f));
 assert(g5_frame_submission_request(&f,slot,1,&req_config));
 assert(g5_frame_delivery_publish(&d,slot,1,config,&snapshot,&serial)&&serial==1);
 assert(!g5_frame_delivery_publish(&d,slot,1,config,&snapshot,&serial));
 assert(!g5_frame_delivery_ack(&d,9,1));
 assert(g5_frame_delivery_ack(&d,10,1));
 assert(!g5_frame_delivery_ack(&d,10,1));
 for(unsigned i=0;i<G5_FRAME_DELIVERY_CAPACITY;i++) {
  uint64_t request_id=2+i;
  assert(g5_frame_submission_request(&f,slot,request_id,&req_config));
  assert(g5_frame_delivery_publish(&d,slot,request_id,config,&snapshot,&serial));
 }
 assert(g5_frame_submission_request(&f,slot,20,&req_config));
 assert(!g5_frame_delivery_publish(&d,slot,20,config,&snapshot,&serial));
 assert(g5_frame_delivery_ack(&d,10,2));
 assert(g5_frame_delivery_publish(&d,slot,20,config,&snapshot,&serial));
 g5_frame_delivery_revoke(&d);
 assert(!g5_frame_delivery_ack(&d,10,3));
 g5_frame_submission_end(&f);
 assert(!g5_frame_delivery_bind(&d,&f));
 assert(g5_frame_submission_begin(&f,11));
 assert(g5_frame_delivery_bind(&d,&f));
 assert(!g5_frame_delivery_ack(&d,10,4));
 g5_frame_delivery_revoke(&d);g5_frame_submission_end(&f);
 return 0;
}
