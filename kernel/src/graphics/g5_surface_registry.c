#include <aurora/g5_surface_registry.h>
#include <stddef.h>
#include <stdint.h>

bool g5_surface_registry_begin(struct g5_surface_registry *r,uint64_t generation) {
    if(!r || r->session.active)return false;
    if(!g5_session_context_begin(&r->session,generation))return false;
    r->next_presentation_serial=1;
    return true;
}
bool g5_surface_registry_attach(struct g5_surface_registry *r,
    struct aurora_cap_table *caps,aurora_cap_handle handle,uint32_t *slot) {
    if(slot)*slot=UINT32_MAX;
    if(!r || !slot || !r->session.active)return false;
    for(uint32_t i=0;i<G5_SURFACE_REGISTRY_CAPACITY;++i) {
        struct g5_surface_registry_entry *e=&r->entries[i];
        if(e->occupied) {
            if(e->bridge.owner_caps==caps && e->bridge.surface_handle==handle)
                return false;
            continue;
        }
        if(!g5_surface_bridge_bind(&e->bridge,&r->session,caps,handle))
            return false;
        e->occupied=true;
        e->presentation_serial=0;
        *slot=i;
        return true;
    }
    return false;
}
bool g5_surface_registry_present(struct g5_surface_registry *r,uint32_t slot,
    struct aurora_graphics_surface_snapshot *snapshot,uint64_t *serial) {
    if(serial)*serial=0;
    if(!r || !snapshot || !serial || !r->session.active ||
       slot>=G5_SURFACE_REGISTRY_CAPACITY ||
       !r->entries[slot].occupied || r->next_presentation_serial==UINT64_MAX)
        return false;
    struct g5_surface_registry_entry *e=&r->entries[slot];
    if(!g5_surface_bridge_read_committed(&e->bridge,&r->session,snapshot))
        return false;
    e->presentation_serial=r->next_presentation_serial++;
    *serial=e->presentation_serial;
    return true;
}
bool g5_surface_registry_detach(struct g5_surface_registry *r,uint32_t slot) {
    if(!r || slot>=G5_SURFACE_REGISTRY_CAPACITY ||
       !r->entries[slot].occupied)return false;
    g5_surface_bridge_revoke(&r->entries[slot].bridge);
    r->entries[slot].occupied=false;
    r->entries[slot].presentation_serial=0;
    return true;
}
void g5_surface_registry_end(struct g5_surface_registry *r) {
    if(!r)return;
    for(uint32_t i=0;i<G5_SURFACE_REGISTRY_CAPACITY;++i)
        (void)g5_surface_registry_detach(r,i);
    g5_session_context_revoke(&r->session);
    r->next_presentation_serial=0;
}
