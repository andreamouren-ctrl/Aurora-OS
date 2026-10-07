#ifndef AURORA_USER_SESSION_HOST_ABI_H
#define AURORA_USER_SESSION_HOST_ABI_H

#include <stdint.h>

#define AURORA_USER_SESSION_HOST_ABI_VERSION 1u
#define AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET 64u
#define AURORA_USER_SESSION_HOST_USER_ID_SIZE 16u

#define AURORA_USER_SESSION_HOST_PROTOCOL_VERSION 1u

#define AURORA_USER_SESSION_HOST_READY 1u
#define AURORA_USER_SESSION_HOST_SHUTDOWN 2u
#define AURORA_USER_SESSION_HOST_SHUTDOWN_ACK 3u

#define AURORA_USER_SESSION_HOST_READY_MAGIC UINT64_C(0x4155525345535348)

struct aurora_user_session_host_startup {
    uint32_t abi_version;
    uint32_t flags;
    uint64_t control_endpoint;
    uint64_t profile_handle;
    uint64_t session_generation;
    uint8_t user_id[AURORA_USER_SESSION_HOST_USER_ID_SIZE];
    uint64_t reserved0;
    uint64_t reserved1;
};

struct aurora_user_session_host_message {
    uint32_t version;
    uint32_t type;
    uint64_t request_id;
};

#endif
