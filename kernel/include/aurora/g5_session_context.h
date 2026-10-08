#ifndef AURORA_G5_SESSION_CONTEXT_H
#define AURORA_G5_SESSION_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

/* WP-03 initial Shell/Compositor session fence.
 * Never trust a generation supplied in IPC payloads as authentication:
 * the caller must already have passed the receiver-side capability check.
 * Mutators are session-supervisor-only. Single owner; serialize externally.
 */
struct g5_session_context {
    uint64_t generation;
    uint64_t last_revoked_generation;
    uint64_t revision;
    bool active;
};

bool g5_session_context_begin(struct g5_session_context *ctx, uint64_t generation);
void g5_session_context_revoke(struct g5_session_context *ctx);
bool g5_session_context_authorized(const struct g5_session_context *ctx,
                                   uint64_t generation);
bool g5_session_context_advance(struct g5_session_context *ctx,
                                uint64_t generation, uint64_t expected_revision,
                                uint64_t *out_revision);

#endif
