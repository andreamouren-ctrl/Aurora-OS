#include <assert.h>
#include <stdint.h>
#include <aurora/g5_compositor_bridge.h>
static struct aurora_graphics_surface surface;
static unsigned adds,removes,presents,releases;
bool graphics_surface_lookup(struct aurora_cap_table *c,aurora_cap_handle h,
 uint64_t rights,struct aurora_graphics_surface **out) {
 if(!c||h!=17||!out||(rights&~(AURORA_RIGHT_READ|AURORA_RIGHT_CONTROL)))return false;
 *out=&surface;return true;
}
bool graphics_surface_read_committed(struct aurora_cap_table *c,aurora_cap_handle h,
 struct aurora_graphics_surface_snapshot *out) {
 if(!c||h!=17||!out)return false;
 *out=surface.committed;
 return true;
}
void graphics_surface_snapshot_release(struct aurora_graphics_surface_snapshot *s) {
 assert(s);++releases;
}
bool software_compositor_add_surface(struct aurora_software_compositor *c,
 struct aurora_graphics_surface *s,int32_t x,int32_t y,int32_t z,uint8_t opacity,
 uint64_t *out) {
 (void)x;(void)y;(void)z;(void)opacity;
 if(!c||!s||!out)return false;
 ++adds;*out=500;return true;
}
bool software_compositor_remove_surface(struct aurora_software_compositor *c,uint64_t id) {
 if(!c||id!=500)return false;
 ++removes;
 return true;
}
bool software_compositor_compose_present(struct aurora_software_compositor *c,uint64_t *out) {
 if(!c||!out)return false;
 ++presents;
 *out=presents;
 return true;
}
int main(void) {
 struct g5_frame_submission f={0};
 struct g5_frame_delivery d={0};
 struct g5_compositor_bridge b={0};
 struct aurora_software_compositor comp={0};
 struct aurora_cap_table *caps=(void *)(uintptr_t)32;
 uint32_t slot=0;uint64_t config=0,req_config=0,node=0,serial=0;
 surface.object_id=33;surface.generation=2;surface.state=AURORA_GRAPHICS_SURFACE_READY;
 comp.initialized=true;
 assert(g5_frame_submission_begin(&f,7));
 assert(g5_surface_registry_attach(&f.registry,caps,17,&slot));
 assert(g5_surface_bridge_configure(&f.registry.entries[slot].bridge,&f.registry.session,640,480,&config));
 assert(g5_surface_bridge_ack(&f.registry.entries[slot].bridge,&f.registry.session,config));
 assert(g5_frame_delivery_bind(&d,&f));
 assert(g5_compositor_bridge_bind(&b,&d,&comp));
 assert(g5_compositor_bridge_attach(&b,slot,0,0,0,&node)&&node==500);
 assert(!g5_compositor_bridge_attach(&b,slot,0,0,0,&node));
 assert(g5_frame_submission_request(&f,slot,1,&req_config));
 assert(g5_compositor_bridge_present(&b,slot,1,config,&serial)&&serial==1);
 assert(presents==1&&releases==1);
 assert(!g5_compositor_bridge_present(&b,slot,1,config,&serial));
 assert(g5_frame_submission_request(&f,slot,2,&req_config));
 surface.generation=3;
 assert(!g5_compositor_bridge_present(&b,slot,2,config,&serial));
 surface.generation=2;
 g5_session_context_revoke(&f.registry.session);
 assert(!g5_compositor_bridge_present(&b,slot,2,config,&serial));
 g5_compositor_bridge_revoke(&b);
 assert(removes==1&&!b.delivery);
 g5_frame_delivery_revoke(&d);
 g5_frame_submission_end(&f);
 return 0;
}
