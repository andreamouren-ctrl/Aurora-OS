#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <aurora/g5_surface_bridge.h>

static struct aurora_graphics_surface mock_surface;
static bool allow_read=true, allow_control=true;
static unsigned read_count;
bool graphics_surface_lookup(struct aurora_cap_table *table,
    aurora_cap_handle handle,uint64_t rights,
    struct aurora_graphics_surface **out) {
    if (!table || handle!=12 || !out ||
        ((rights&AURORA_RIGHT_READ) && !allow_read) ||
        ((rights&AURORA_RIGHT_CONTROL) && !allow_control)) return false;
    *out=&mock_surface;
    return true;
}
bool graphics_surface_read_committed(struct aurora_cap_table *table,
    aurora_cap_handle handle,struct aurora_graphics_surface_snapshot *out) {
    if (!table || handle!=12 || !out) return false;
    ++read_count;
    *out=mock_surface.committed;
    return true;
}
int main(void) {
    struct g5_surface_bridge b={0};
    struct g5_session_context s={0};
    struct aurora_cap_table *caps=(struct aurora_cap_table *)(uintptr_t)16;
    struct aurora_graphics_surface_snapshot snapshot={0};
    uint64_t serial=0;
    mock_surface.object_id=43;
    mock_surface.generation=2;
    mock_surface.state=AURORA_GRAPHICS_SURFACE_READY;
    assert(g5_session_context_begin(&s,4));
    assert(g5_surface_bridge_bind(&b,&s,caps,12));
    assert(!g5_surface_bridge_read_committed(&b,&s,&snapshot));
    assert(g5_surface_bridge_configure(&b,&s,1280,720,&serial) && serial==1);
    assert(!g5_surface_bridge_ack(&b,&s,2));
    assert(g5_surface_bridge_ack(&b,&s,1));
    assert(g5_surface_bridge_read_committed(&b,&s,&snapshot) && read_count==1);
    allow_control=false;
    assert(!g5_surface_bridge_configure(&b,&s,1200,700,&serial));
    allow_control=true;
    allow_read=false;
    assert(!g5_surface_bridge_read_committed(&b,&s,&snapshot));
    allow_read=true;
    mock_surface.generation=3;
    assert(!g5_surface_bridge_read_committed(&b,&s,&snapshot));
    mock_surface.generation=2;
    mock_surface.destroy_requested=true;
    assert(!g5_surface_bridge_read_committed(&b,&s,&snapshot));
    mock_surface.destroy_requested=false;
    g5_session_context_revoke(&s);
    assert(!g5_surface_bridge_read_committed(&b,&s,&snapshot));
    assert(g5_session_context_begin(&s,5));
    assert(!g5_surface_bridge_ack(&b,&s,1));
    g5_surface_bridge_revoke(&b);
    assert(g5_surface_bridge_bind(&b,&s,caps,12));
    return 0;
}
