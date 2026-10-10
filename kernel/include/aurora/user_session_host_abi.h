#ifndef AURORA_USER_SESSION_HOST_ABI_H
#define AURORA_USER_SESSION_HOST_ABI_H

#include <stdint.h>
#include <aurora/input.h>

#define AURORA_USER_SESSION_HOST_ABI_VERSION 3u
#define AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET 112u
#define AURORA_USER_SESSION_HOST_USER_ID_SIZE 16u

#define AURORA_USER_SESSION_HOST_PROTOCOL_VERSION 1u
#define AURORA_USER_SESSION_HOST_FLAG_RENDER_CLIENT 1u

#define AURORA_USER_SESSION_HOST_READY 1u
#define AURORA_USER_SESSION_HOST_SHUTDOWN 2u
#define AURORA_USER_SESSION_HOST_SHUTDOWN_ACK 3u
#define AURORA_USER_SESSION_HOST_HEALTH_POLL 4u
#define AURORA_USER_SESSION_HOST_HEALTH_ACK 5u
/* Exclusive process control endpoint: request_id = committed frame serial. */
#define AURORA_USER_SESSION_HOST_FRAME_COMMITTED 6u
#define AURORA_USER_SESSION_HOST_INPUT_EVENT 7u
#define AURORA_USER_SESSION_HOST_RESIZE_PREPARE 8u
#define AURORA_USER_SESSION_HOST_RESIZE_ACK 9u
/* Kernel admits resize commit only after trusted ACK is recorded. */
#define AURORA_USER_SESSION_HOST_RESIZE_COMMIT_GRANT 10u

#define AURORA_USER_SESSION_HOST_READY_MAGIC UINT64_C(0x4155525345535348)

struct aurora_user_session_host_startup {
    uint32_t abi_version;
    uint32_t flags; /* 0: Shell, RENDER_CLIENT: isolated secondary Ring3 renderer */
    uint64_t control_endpoint;
    uint64_t profile_handle;
    uint64_t session_generation;
    uint8_t user_id[AURORA_USER_SESSION_HOST_USER_ID_SIZE];
    uint64_t g5_endpoint; /* optional, exclusive non-transferable G5 sender */
    uint64_t reserved1;
    uint64_t graphics_buffer; /* write/map, non-transferable */
    uint64_t graphics_surface; /* write/read, non-transferable */
    uint64_t graphics_object_id;
    uint64_t graphics_object_generation;
    uint64_t graphics_width;
    uint64_t graphics_height;
};

struct aurora_user_session_host_message {
    uint32_t version;
    uint32_t type;
    uint64_t request_id;
};

/* Kernel-to-one-client delivery through an exclusive process endpoint.
 * Entire payload is initialized before send; no capability is transferred. */
struct aurora_user_session_host_input_message {
    struct aurora_user_session_host_message header;
    struct aurora_input_event event;
};

/* Receiver-owned graphics buffer handle (in this process's cap table).
 * The client ACKs configure_serial before committing the new pixels. */
struct aurora_user_session_host_resize_message {
    struct aurora_user_session_host_message header;
    uint64_t graphics_buffer;
    uint64_t configure_serial;
    uint32_t width;
    uint32_t height;
};

#endif
