#ifndef AURORA_G5_CANVAS_BOOTSTRAP_H
#define AURORA_G5_CANVAS_BOOTSTRAP_H
#include <stdint.h>
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
