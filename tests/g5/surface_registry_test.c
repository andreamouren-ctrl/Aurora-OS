#include <assert.h>
#include <stdint.h>
#include <aurora/g5_surface_registry.h>

static struct aurora_graphics_surface surface;
bool graphics_surface_lookup(struct aurora_cap_table *caps,aurora_cap_handle h,
    uint64_t rights,struct aurora_graphics_surface **out) {
    if(!caps || h!=17 || !out || (rights & ~(AURORA_RIGHT_READ|AURORA_RIGHT_CONTROL)))
        return false;
    *out=&surface;return true;
}
bool graphics_surface_read_committed(struct aurora_cap_table *caps,
    aurora_cap_handle h,struct aurora_graphics_surface_snapshot *out) {
    if(!caps || h!=17 || !out)return false;
    *out=surface.committed;return true;
}
int main(void) {
    struct g5_surface_registry r={0};
    struct aurora_cap_table *caps=(void *)(uintptr_t)32;
    struct aurora_graphics_surface_snapshot snapshot={0};
    uint64_t serial=44,configure=0;
    uint32_t slot=UINT32_MAX;
    surface.object_id=7;surface.generation=1;
    surface.state=AURORA_GRAPHICS_SURFACE_READY;
    assert(g5_surface_registry_begin(&r,9));
    assert(g5_surface_registry_attach(&r,caps,17,&slot) && slot==0);
    assert(!g5_surface_registry_attach(&r,caps,17,&slot));
    assert(!g5_surface_registry_present(&r,0,&snapshot,&serial) && serial==0);
    assert(g5_surface_bridge_configure(&r.entries[0].bridge,&r.session,400,300,&configure));
    assert(g5_surface_bridge_ack(&r.entries[0].bridge,&r.session,configure));
    assert(g5_surface_registry_present(&r,0,&snapshot,&serial) && serial==1);
    assert(g5_surface_registry_present(&r,0,&snapshot,&serial) && serial==2);
    surface.destroy_requested=true;
    assert(!g5_surface_registry_present(&r,0,&snapshot,&serial));
    surface.destroy_requested=false;
    g5_surface_registry_end(&r);
    assert(!g5_surface_registry_present(&r,0,&snapshot,&serial));
    assert(!r.entries[0].occupied);
    assert(!g5_surface_registry_begin(&r,9));
    assert(g5_surface_registry_begin(&r,10));
    assert(g5_surface_registry_attach(&r,caps,17,&slot));
    assert(!g5_surface_registry_present(&r,slot,&snapshot,&serial));
    assert(g5_surface_registry_detach(&r,slot));
    assert(!g5_surface_registry_detach(&r,slot));
    g5_surface_registry_end(&r);
    return 0;
}
