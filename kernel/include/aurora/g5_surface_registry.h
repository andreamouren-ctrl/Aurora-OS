#ifndef AURORA_G5_SURFACE_REGISTRY_H
#define AURORA_G5_SURFACE_REGISTRY_H
#include <aurora/g5_surface_bridge.h>
#define G5_SURFACE_REGISTRY_CAPACITY 16u
struct g5_surface_registry_entry {
    struct g5_surface_bridge bridge;
    uint64_t presentation_serial;
    bool occupied;
};
struct g5_surface_registry {
    struct g5_session_context session;
    struct g5_surface_registry_entry entries[G5_SURFACE_REGISTRY_CAPACITY];
    uint64_t next_presentation_serial;
};
bool g5_surface_registry_begin(struct g5_surface_registry *r,uint64_t generation);
bool g5_surface_registry_attach(struct g5_surface_registry *r,
    struct aurora_cap_table *caps,aurora_cap_handle handle,uint32_t *slot);
bool g5_surface_registry_present(struct g5_surface_registry *r,uint32_t slot,
    struct aurora_graphics_surface_snapshot *snapshot,uint64_t *serial);
bool g5_surface_registry_detach(struct g5_surface_registry *r,uint32_t slot);
void g5_surface_registry_end(struct g5_surface_registry *r);
#endif
