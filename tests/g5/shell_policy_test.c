#include <assert.h>
#include <aurora/g5_shell_policy.h>
static unsigned applied;
static bool permit_apply=true;
static bool apply(void *ctx,const struct g5_ipc_header *h,const uint8_t *payload) {
 (void)ctx;(void)payload;
 if(!permit_apply||h->operation!=G5_OP_WINDOW_CONFIGURE)return false;
 ++applied;return true;
}
int main(void) {
 struct g5_shell_session s={0};
 struct g5_shell_policy p={0};
 const struct g5_ipc_header h={
  .kind=G5_IPC_REQUEST,.operation=G5_OP_WINDOW_CONFIGURE,
  .session_generation=10,.request_id=1
 };
 p.session=&s;p.apply=apply;p.permitted_operations=G5_SHELL_PERMIT_CONFIGURE;
 assert(!g5_shell_policy_authorize(&p,G5_OP_WINDOW_CONFIGURE,10));
 assert(g5_shell_session_ready(&s,10));
 assert(g5_shell_policy_authorize(&p,G5_OP_WINDOW_CONFIGURE,10));
 assert(!g5_shell_policy_authorize(&p,G5_OP_WINDOW_PLACE,10));
 assert(!g5_shell_policy_authorize(&p,G5_OP_WINDOW_CONFIGURE,9));
 assert(!g5_shell_policy_authorize(&p,G5_OP_SCENE_PUBLISH,10));
 permit_apply=false;
 assert(!g5_shell_policy_handle(&p,&h,0)&&applied==0);
 permit_apply=true;
 assert(g5_shell_policy_handle(&p,&h,0)&&applied==1);
 g5_shell_session_end(&s);
 assert(!g5_shell_policy_handle(&p,&h,0));
 assert(g5_shell_session_ready(&s,11));
 assert(!g5_shell_policy_handle(&p,&h,0));
 g5_shell_policy_revoke(&p);
 assert(!g5_shell_policy_authorize(&p,G5_OP_WINDOW_CONFIGURE,11));
 assert(!g5_shell_policy_handle(&p,&h,0));
 return 0;
}
