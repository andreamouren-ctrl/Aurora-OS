#ifndef AURORA_G5_SURFACE_CONFIGURE_H
#define AURORA_G5_SURFACE_CONFIGURE_H

#include <stdbool.h>
#include <stdint.h>
#include <aurora/g5_session_context.h>

/* WP-03: Shell-authorized surface Configure/ACK transaction.
 * Does not create graphics surfaces, grant capabilities or render frames.
 * Callers must authenticate ownership before invoking these operations.
 * Access must be serialized by the owning Shell service.
 */
#define G5_SURFACE_MAX_DIMENSION 8192u

struct g5_surface_configure {
    uint64_t session_generation;
    uint64_t object_generation;
    uint64_t next_serial;
    uint64_t pending_serial;
    uint64_t accepted_serial;
    uint64_t session_revision;
    uint32_t pending_width;
    uint32_t pending_height;
    uint32_t accepted_width;
    uint32_t accepted_height;
    bool active;
};

bool g5_surface_configure_bind(struct g5_surface_configure *surface,
    const struct g5_session_context *session, uint64_t object_generation);
bool g5_surface_configure_issue(struct g5_surface_configure *surface,
    const struct g5_session_context *session,
    uint32_t width, uint32_t height, uint64_t *out_serial);
bool g5_surface_configure_ack(struct g5_surface_configure *surface,
    const struct g5_session_context *session,
    uint64_t serial, uint64_t object_generation);
bool g5_surface_configure_ready(const struct g5_surface_configure *surface,
    const struct g5_session_context *session);
void g5_surface_configure_revoke(struct g5_surface_configure *surface);

#endif
