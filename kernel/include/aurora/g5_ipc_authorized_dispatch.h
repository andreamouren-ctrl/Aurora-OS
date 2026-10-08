#ifndef AURORA_G5_IPC_AUTHORIZED_DISPATCH_H
#define AURORA_G5_IPC_AUTHORIZED_DISPATCH_H

#include <aurora/g5_ipc_dispatch.h>
#include <aurora/g5_ipc_cap_adapter.h>

/* Kernel-side trusted-entry adapter. Authority is a handle owned by the
 * receiver's cap table, NOT an untrusted handle from the request payload.
 * This does not establish who sent the IPC request; the caller must separately
 * bind the endpoint to the authenticated session principal. */
enum g5_ipc_status g5_ipc_dispatch_authorized(
    struct g5_dispatch_context *dispatcher,
    const struct aurora_sys_ipc_received *message,
    struct aurora_cap_table *receiver_caps,
    aurora_cap_handle authority_handle,
    enum aurora_cap_type expected_type,
    uint64_t required_rights
);

#endif
