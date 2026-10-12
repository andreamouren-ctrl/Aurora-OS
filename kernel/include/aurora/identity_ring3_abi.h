#ifndef AURORA_IDENTITY_RING3_ABI_H
#define AURORA_IDENTITY_RING3_ABI_H
#include <stdint.h>

#define AURORA_IDENTITY_RING3_ABI_VERSION 1u
#define AURORA_IDENTITY_RING3_STARTUP_OFFSET 24u
#define AURORA_IDENTITY_RING3_READY_MAGIC UINT64_C(0x4944454e54335231)
#define AURORA_IDENTITY_RING3_TIMEOUT_NS UINT64_C(1500000000)
#define AURORA_IDENTITY_RING3_READY 1u
#define AURORA_IDENTITY_RING3_HEALTH 2u
#define AURORA_IDENTITY_RING3_HEALTH_ACK 3u
#define AURORA_IDENTITY_RING3_SHUTDOWN 4u
#define AURORA_IDENTITY_RING3_SHUTDOWN_ACK 5u
/* Visual-only, noninteractive and deliberately small Ring3 accent. */
#define AURORA_IDENTITY_RING3_DRAW_BADGE 6u
#define AURORA_IDENTITY_RING3_DRAW_ACK 7u
#define AURORA_IDENTITY_RING3_BADGE_WIDTH 128u
#define AURORA_IDENTITY_RING3_BADGE_HEIGHT 16u
#define AURORA_IDENTITY_RING3_BADGE_FIRST_PIXEL UINT32_C(0xff4dcce8)

struct aurora_identity_ring3_startup {
    uint64_t control_endpoint;
    uint64_t instance_epoch;
    uint64_t magic;
};
struct aurora_identity_ring3_message {
    uint32_t abi_version;
    uint32_t type;
    uint64_t request_id;
};
/* These handles are scoped to the host's own process cap table. No
 * transfer or display authority is provided with the request. */
struct aurora_identity_ring3_draw_message {
    struct aurora_identity_ring3_message header;
    uint64_t graphics_buffer;
    uint64_t graphics_surface;
    uint32_t width;
    uint32_t height;
};
struct aurora_identity_ring3_draw_ack {
    struct aurora_identity_ring3_message header;
    uint64_t commit_serial;
};
#endif
