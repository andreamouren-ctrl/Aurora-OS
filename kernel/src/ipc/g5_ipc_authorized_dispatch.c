#include <aurora/g5_ipc_authorized_dispatch.h>

enum g5_ipc_status g5_ipc_dispatch_authorized(
    struct g5_dispatch_context *dispatcher,
    const struct aurora_sys_ipc_received *message,
    struct aurora_cap_table *receiver_caps,
    aurora_cap_handle authority_handle,
    enum aurora_cap_type expected_type,
    uint64_t required_rights
) {
    if (dispatcher == NULL || message == NULL || receiver_caps == NULL ||
        authority_handle == AURORA_CAP_INVALID || required_rights == 0)
        return G5_IPC_DENIED;
    if (!g5_ipc_kernel_cap_check(receiver_caps, authority_handle,
                                 expected_type, required_rights))
        return G5_IPC_DENIED;
    /* Session generation and operation authorization still checked by
     * g5_ipc_dispatch; the service must bind sender identity to endpoint. */
    return g5_ipc_dispatch(dispatcher, message);
}
