#ifndef AURORA_G5_IPC_ENDPOINT_H
#define AURORA_G5_IPC_ENDPOINT_H
#include <aurora/g5_ipc_authorized_dispatch.h>
#include <aurora/ipc.h>
/* Kernel-owned, single-consumer endpoint: the opposite side must be delegated
 * ONLY to the authorized session principal, with no transferable rights.
 * IPC itself does not supply sender PID; provisioning is the trust boundary.
 */
struct g5_ipc_endpoint_binding {
 struct aurora_ipc_endpoint *receiver;
 aurora_cap_handle receiver_endpoint_handle;
 aurora_thread_id trusted_consumer_thread; /* 0 = bootstrap-only */
 struct aurora_cap_table *receiver_caps;
 struct g5_dispatch_context *dispatch;
 aurora_cap_handle receiver_authority;
 enum aurora_cap_type authority_type;
 uint64_t authority_rights;
 bool provisioned_exclusively;
};
/* Consume one message, returning false when no message is available or the
 * endpoint is not provisioned. Rejected messages are consumed fail-closed.
 * Callers must serialize access to this binding and its dispatcher. */
bool g5_ipc_endpoint_poll(struct g5_ipc_endpoint_binding *binding,
 enum g5_ipc_status *status);
/* Kernel boot-validation probe using actual Aurora IPC queues. */
bool g5_ipc_endpoint_self_test(void);
/* Ring 3 IPC transfer exercise with existing isolated user probe image. */
bool g5_ipc_ring3_self_test(void);
#endif
