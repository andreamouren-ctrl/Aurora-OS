#include <aurora/g5_shell_receiver.h>
#include <stddef.h>
static struct aurora_ipc_channel channel;
static struct aurora_cap_table caps;
static unsigned effects;
static bool apply(void *context,const struct g5_ipc_header *h,const uint8_t *payload) {
 (void)context;(void)payload;
 if(h->operation!=G5_OP_WINDOW_CLOSE)return false;
 ++effects;return true;
}
bool g5_shell_receiver_native_self_test(void) {
 ipc_channel_init(&channel);
 cap_table_init(&caps);
 effects=0;
 struct aurora_ipc_endpoint *sender=ipc_channel_endpoint(&channel,0);
 struct aurora_ipc_endpoint *endpoint=ipc_channel_endpoint(&channel,1);
 if(!sender||!endpoint)return false;
 aurora_cap_handle receive=cap_grant(&caps,endpoint,
   AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_READ);
 aurora_cap_handle authority=cap_grant(&caps,&channel,
   AURORA_CAP_SYSTEM,AURORA_RIGHT_CONTROL);
 if(receive==AURORA_CAP_INVALID||authority==AURORA_CAP_INVALID)return false;
 struct g5_shell_session session={0};
 struct g5_shell_receiver receiver={0};
 if(!g5_shell_session_ready(&session,71)||
    !g5_shell_receiver_bind(&receiver,&session,G5_SHELL_PERMIT_CLOSE,apply,NULL)||
    !g5_shell_receiver_connect(&receiver,endpoint,&caps,receive,authority,
      AURORA_CAP_SYSTEM,AURORA_RIGHT_CONTROL,0)) {
  /* The consumer must be explicitly trusted: 0 is not permitted. */
  if(receiver.endpoint.receiver)return false;
 }
 /* Kernel bootstrap is the only single-consumer context with thread id 0.
  * The runtime connect contract intentionally rejects 0, so this probe
  * runs only once a scheduler consumer identity can be provided. */
 g5_shell_receiver_revoke(&receiver);
 g5_shell_session_end(&session);
 return cap_revoke(&caps,receive)&&cap_revoke(&caps,authority);
}
