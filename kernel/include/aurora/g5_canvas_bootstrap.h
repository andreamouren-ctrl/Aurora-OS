#ifndef AURORA_G5_CANVAS_BOOTSTRAP_H
#define AURORA_G5_CANVAS_BOOTSTRAP_H
#include <stdint.h>
#include <stddef.h>
/* Dedicated Canvas protocol: no Shell or graphics capabilities implied. */
#define G5_CANVAS_BOOTSTRAP_VERSION 1u
#define G5_CANVAS_BOOTSTRAP_READY 1u
#define G5_CANVAS_BOOTSTRAP_HEALTH 2u
#define G5_CANVAS_BOOTSTRAP_HEALTH_ACK 3u
#define G5_CANVAS_BOOTSTRAP_SHUTDOWN 4u
#define G5_CANVAS_BOOTSTRAP_SHUTDOWN_ACK 5u
struct g5_canvas_bootstrap {
    uint32_t version;
    uint32_t type;
    uint64_t session_generation;
    uint64_t request_id;
};
/* Wire ABI is fixed-width. IPC must still serialize fields explicitly
 * when crossing architectures; never trust padding from a sender. */
_Static_assert(sizeof(struct g5_canvas_bootstrap) == 24u,
               "Canvas bootstrap frame must remain 24 bytes");
_Static_assert(offsetof(struct g5_canvas_bootstrap, version) == 0u,
               "Canvas bootstrap version offset changed");
_Static_assert(offsetof(struct g5_canvas_bootstrap, type) == 4u,
               "Canvas bootstrap type offset changed");
_Static_assert(offsetof(struct g5_canvas_bootstrap, session_generation) == 8u,
               "Canvas bootstrap generation offset changed");
_Static_assert(offsetof(struct g5_canvas_bootstrap, request_id) == 16u,
               "Canvas bootstrap request offset changed");
/* A frame must never validate against an unknown or reserved opcode. */
static inline int g5_canvas_bootstrap_known_type(uint32_t type) {
    return type >= G5_CANVAS_BOOTSTRAP_READY &&
           type <= G5_CANVAS_BOOTSTRAP_SHUTDOWN_ACK;
}
/* Validation is transport-independent: receiving a well-formed message
 * does not confer any capability. The endpoint owner must still be checked
 * by the IPC dispatcher before these predicates are used. */
static inline int g5_canvas_bootstrap_valid(
    const struct g5_canvas_bootstrap *message,
    uint32_t expected_type,
    uint64_t expected_generation,
    uint64_t expected_request_id
) {
    return message != 0 &&
        message->version == G5_CANVAS_BOOTSTRAP_VERSION &&
        g5_canvas_bootstrap_known_type(expected_type) &&
        message->type == expected_type &&
        expected_generation != 0u &&
        message->session_generation == expected_generation &&
        expected_request_id != 0u &&
        message->request_id == expected_request_id;
}

/* The only unsolicited event is READY, with a fixed startup request ID.
 * HEALTH_ACK and SHUTDOWN_ACK must match an outstanding kernel request. */
#define G5_CANVAS_BOOTSTRAP_READY_REQUEST_ID UINT64_C(1)
static inline int g5_canvas_bootstrap_ready_valid(
    const struct g5_canvas_bootstrap *message,
    uint64_t expected_generation
) {
    return g5_canvas_bootstrap_valid(
        message, G5_CANVAS_BOOTSTRAP_READY, expected_generation,
        G5_CANVAS_BOOTSTRAP_READY_REQUEST_ID);
}
#endif
