#include <aurora/g5_shell_policy.h>
#include <stddef.h>
#include <stdint.h>
static uint32_t bit_for(uint32_t op) {
 switch(op) {
 case G5_OP_WINDOW_CONFIGURE:return G5_SHELL_PERMIT_CONFIGURE;
 case G5_OP_WINDOW_CONFIGURE_ACK:return G5_SHELL_PERMIT_ACK;
 case G5_OP_WINDOW_PLACE:return G5_SHELL_PERMIT_PLACE;
 case G5_OP_WINDOW_CLOSE:return G5_SHELL_PERMIT_CLOSE;
 default:return 0;
 }
}
bool g5_shell_policy_authorize(void *context,uint32_t op,uint64_t gen) {
 const struct g5_shell_policy *p=(const struct g5_shell_policy *)context;
 const uint32_t permission=bit_for(op);
 return p && p->session && p->apply && permission!=0 &&
  (p->permitted_operations & permission)==permission &&
  g5_shell_session_check(p->session,gen);
}
bool g5_shell_policy_handle(void *context,const struct g5_ipc_header *h,
 const uint8_t *payload) {
 struct g5_shell_policy *p=(struct g5_shell_policy *)context;
 if(!p||!h||!g5_shell_policy_authorize(p,h->operation,h->session_generation)||
    h->kind!=G5_IPC_REQUEST||h->request_id==0||p->accepted_requests==UINT64_MAX)
    return false;
 if(!p->apply(p->apply_context,h,payload))return false;
 ++p->accepted_requests;
 return true;
}
void g5_shell_policy_revoke(struct g5_shell_policy *p) {
 if(!p)return;
 p->permitted_operations=0;
 p->apply=NULL;
 p->apply_context=NULL;
}
