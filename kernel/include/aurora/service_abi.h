#ifndef AURORA_SERVICE_ABI_H
#define AURORA_SERVICE_ABI_H

#include <stdint.h>

#define AURORA_SERVICE_STARTUP_ABI_VERSION 3u
#define AURORA_SERVICE_STARTUP_STACK_OFFSET 64u
#define AURORA_SERVICE_STARTUP_MAX_EXTRA_CAPABILITIES 3u

struct aurora_service_startup_block {
    uint32_t abi_version;
    uint32_t flags;
    uint64_t ipc_endpoint;
    uint64_t protected_state;
    uint64_t entropy_seed;
    uint32_t extra_capability_count;
    uint32_t reserved;
    uint64_t extra_capabilities[
        AURORA_SERVICE_STARTUP_MAX_EXTRA_CAPABILITIES
    ];
};

_Static_assert(
    sizeof(struct aurora_service_startup_block) ==
        AURORA_SERVICE_STARTUP_STACK_OFFSET,
    "Service startup ABI must remain exactly one 64-byte startup slot"
);

#endif
