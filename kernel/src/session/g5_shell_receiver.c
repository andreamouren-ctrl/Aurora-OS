#include <aurora/g5_shell_receiver.h>
#include <stddef.h>
bool g5_shell_receiver_bind(struct g5_shell_receiver *r,
 struct g5_shell_session *s,uint32_t allowed,
 bool (*apply)(void *,const struct g5_ipc_header *,const uint8_t *),
 void *ctx) {
 if(!r||!s||!apply||!allowed||!s->context.active||
    r->dispatcher.active_session_generation||r->dispatcher.dispatch_in_progress)
  return false;
 r->policy.session=s;
 r->policy.permitted_operations=allowed;
 r->policy.apply=apply;
 r->policy.apply_context=ctx;
 r->dispatcher.context=&r->policy;
 r->dispatcher.authorize=g5_shell_policy_authorize;
 r->dispatcher.handler=g5_shell_policy_handle;
 if(!g5_ipc_dispatch_bind_session(&r->dispatcher,s->context.generation)) {
  g5_shell_policy_revoke(&r->policy);
  return false;
 }
 return true;
}
void g5_shell_receiver_revoke(struct g5_shell_receiver *r) {
 if(!r)return;
 g5_ipc_dispatch_revoke(&r->dispatcher);
 g5_shell_policy_revoke(&r->policy);
}
