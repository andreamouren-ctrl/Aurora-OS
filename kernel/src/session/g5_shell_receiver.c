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
bool g5_shell_receiver_connect(struct g5_shell_receiver *r,
 struct aurora_ipc_endpoint *endpoint,struct aurora_cap_table *caps,
 aurora_cap_handle endpoint_handle,aurora_cap_handle authority,
 enum aurora_cap_type authority_type,uint64_t rights,
 aurora_thread_id consumer) {
 if(!r||!endpoint||!caps||!rights||!consumer||
    endpoint_handle==AURORA_CAP_INVALID||authority==AURORA_CAP_INVALID||
    r->endpoint.receiver||!r->policy.session||
    !g5_shell_session_check(r->policy.session,r->dispatcher.active_session_generation)||
    !r->dispatcher.handler||!r->dispatcher.authorize)return false;
 r->endpoint=(struct g5_ipc_endpoint_binding){
  .receiver=endpoint,.receiver_caps=caps,.receiver_endpoint_handle=endpoint_handle,
  .receiver_authority=authority,.authority_type=authority_type,
  .authority_rights=rights,.trusted_consumer_thread=consumer,
  .dispatch=&r->dispatcher,.provisioned_exclusively=true
 };
 return true;
}
bool g5_shell_receiver_poll(struct g5_shell_receiver *r,enum g5_ipc_status *status) {
 if(status)*status=G5_IPC_DENIED;
 if(!r||!status||!r->endpoint.receiver||!r->policy.session||
    !g5_shell_session_check(r->policy.session,r->dispatcher.active_session_generation))
  return false;
 return g5_ipc_endpoint_poll(&r->endpoint,status);
}
void g5_shell_receiver_disconnect(struct g5_shell_receiver *r) {
 if(!r)return;
 r->endpoint.provisioned_exclusively=false;
 r->endpoint.receiver=NULL;
 r->endpoint.receiver_caps=NULL;
 r->endpoint.dispatch=NULL;
}
void g5_shell_receiver_revoke(struct g5_shell_receiver *r) {
 if(!r)return;
 g5_shell_receiver_disconnect(r);
 g5_ipc_dispatch_revoke(&r->dispatcher);
 g5_shell_policy_revoke(&r->policy);
}
