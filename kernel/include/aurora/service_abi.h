#ifndef AURORA_SERVICE_ABI_H
#define AURORA_SERVICE_ABI_H

#include <stdint.h>

#define AURORA_SERVICE_STARTUP_ABI_VERSION 2u
#define AURORA_SERVICE_STARTUP_STACK_OFFSET 64u

struct aurora_service_startup_block {
    uint32_t abi_version;
    uint32_t flags;
    uint64_t ipc_endpoint;
    uint64_t protected_state;
    uint64_t entropy_seed;
};

#endif
