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
#endif
