#ifndef AURORA_IDENTITY_RUNTIME_PROTECTED_STATE_TRANSPORT_H
#define AURORA_IDENTITY_RUNTIME_PROTECTED_STATE_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/identity/protected_state_transport.h>

struct identity_runtime_protected_state_context {
    uint64_t handle;
};

bool identity_runtime_protected_state_transport_init(
    struct identity_runtime_protected_state_context *context,
    uint64_t handle,
    struct aurora_identity_protected_state_transport_ops *out_ops
);

#endif
