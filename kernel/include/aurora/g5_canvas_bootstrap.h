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
#endif
