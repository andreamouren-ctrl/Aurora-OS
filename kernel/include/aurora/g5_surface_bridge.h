#ifndef AURORA_G5_SURFACE_BRIDGE_H
#define AURORA_G5_SURFACE_BRIDGE_H
#include <stdbool.h>
#include <stdint.h>
#include <aurora/g5_surface_configure.h>
#include <aurora/graphics_surface.h>

/* WP-03 integration batch:
 * 1. Receiver-owned capability lookup and object-generation binding.
 * 2. Configure/ACK fence before a compositor-facing committed snapshot.
 * 3. Session/surface teardown invalidates stale authority and snapshots.
 * Caller serializes this per-session bridge. No automatic IPC authentication.
 */
struct g5_surface_bridge {
    struct g5_surface_configure configure;
    struct aurora_cap_table *owner_caps;
    aurora_cap_handle surface_handle;
    uint64_t object_id;
    uint32_t object_generation;
};
bool g5_surface_bridge_bind(struct g5_surface_bridge *bridge,
    const struct g5_session_context *session, struct aurora_cap_table *owner_caps,
    aurora_cap_handle surface_handle);
bool g5_surface_bridge_configure(struct g5_surface_bridge *bridge,
    const struct g5_session_context *session, uint32_t width, uint32_t height,
    uint64_t *out_serial);
bool g5_surface_bridge_ack(struct g5_surface_bridge *bridge,
    const struct g5_session_context *session, uint64_t serial);
bool g5_surface_bridge_read_committed(struct g5_surface_bridge *bridge,
    const struct g5_session_context *session,
    struct aurora_graphics_surface_snapshot *out_snapshot);
void g5_surface_bridge_revoke(struct g5_surface_bridge *bridge);
#endif
