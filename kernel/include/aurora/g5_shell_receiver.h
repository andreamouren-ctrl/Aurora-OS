#ifndef AURORA_G5_SHELL_RECEIVER_H
#define AURORA_G5_SHELL_RECEIVER_H
#include <aurora/g5_shell_policy.h>
#include <aurora/g5_ipc_endpoint.h>
/* Receiver-local assembly. The service owner must provision a sender-exclusive
 * IPC endpoint and receiver-table capability authority separately. */
struct g5_shell_receiver {
 struct g5_dispatch_context dispatcher;
 struct g5_shell_policy policy;
 struct g5_ipc_endpoint_binding endpoint;
};
bool g5_shell_receiver_bind(struct g5_shell_receiver *receiver,
 struct g5_shell_session *session,uint32_t allowed,
 bool (*apply)(void *,const struct g5_ipc_header *,const uint8_t *),
 void *apply_context);
bool g5_shell_receiver_connect(struct g5_shell_receiver *receiver,
 struct aurora_ipc_endpoint *native_endpoint,
 struct aurora_cap_table *receiver_caps,
 aurora_cap_handle endpoint_handle,aurora_cap_handle authority,
 enum aurora_cap_type authority_type,uint64_t rights,
 aurora_thread_id consumer_thread);
bool g5_shell_receiver_poll(struct g5_shell_receiver *receiver,
 enum g5_ipc_status *status);
void g5_shell_receiver_disconnect(struct g5_shell_receiver *receiver);
void g5_shell_receiver_revoke(struct g5_shell_receiver *receiver);
#endif
