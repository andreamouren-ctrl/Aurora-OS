#ifndef AURORA_G5_SHELL_POLICY_H
#define AURORA_G5_SHELL_POLICY_H
#include <aurora/g5_shell_session.h>
#include <aurora/g5_ipc_dispatch.h>
/* Receiver-local policy: only a trusted dispatcher may invoke these callbacks.
 * Capability validation happens in g5_ipc_endpoint_poll, not here. */
struct g5_shell_policy {
 struct g5_shell_session *session;
 uint32_t permitted_operations;
 uint64_t accepted_requests;
};
#define G5_SHELL_PERMIT_CONFIGURE (1u << 0)
#define G5_SHELL_PERMIT_ACK       (1u << 1)
#define G5_SHELL_PERMIT_PLACE     (1u << 2)
#define G5_SHELL_PERMIT_CLOSE     (1u << 3)
bool g5_shell_policy_authorize(void *context,uint32_t operation,uint64_t generation);
bool g5_shell_policy_handle(void *context,const struct g5_ipc_header *header,
 const uint8_t *payload);
void g5_shell_policy_revoke(struct g5_shell_policy *policy);
#endif
