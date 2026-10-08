#ifndef AURORA_G5_IPC_DISPATCH_H
#define AURORA_G5_IPC_DISPATCH_H

#include <aurora/g5_ipc_abi.h>

/* A service-local dispatcher, not a cross-process Shell implementation.
 * Callbacks must check the authoritative caller session/identity and execute
 * only operations granted by that service's policy. No raw pointer wire data. */
typedef bool (*g5_dispatch_authorize_fn)(
    void *context, uint32_t operation, uint64_t session_generation
);
typedef bool (*g5_dispatch_handler_fn)(
    void *context, const struct g5_ipc_header *header,
    const uint8_t *payload
);
struct g5_dispatch_context {
    uint64_t active_session_generation;
    uint64_t last_request_id; /* ordered endpoint; reset on session rotation */
    g5_dispatch_authorize_fn authorize;
    g5_dispatch_handler_fn handler;
    void *context;
};
enum g5_ipc_status g5_ipc_dispatch(
    struct g5_dispatch_context *dispatch,
    const struct aurora_sys_ipc_received *message
);

#endif
