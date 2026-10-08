#ifndef AURORA_G5_SHELL_RECEIVER_H
#define AURORA_G5_SHELL_RECEIVER_H
#include <aurora/g5_shell_policy.h>
/* Receiver-local assembly. The service owner must provision a sender-exclusive
 * IPC endpoint and receiver-table capability authority separately. */
struct g5_shell_receiver {
 struct g5_dispatch_context dispatcher;
 struct g5_shell_policy policy;
};
bool g5_shell_receiver_bind(struct g5_shell_receiver *receiver,
 struct g5_shell_session *session,uint32_t allowed,
 bool (*apply)(void *,const struct g5_ipc_header *,const uint8_t *),
 void *apply_context);
void g5_shell_receiver_revoke(struct g5_shell_receiver *receiver);
#endif
