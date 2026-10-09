#include <aurora/g5_shell_receiver.h>
#include <stddef.h>
#include <aurora/scheduler.h>
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
 aurora_thread_id consumer=scheduler_current_thread_id();
 if(!consumer||!g5_shell_session_ready(&session,71)||
    !g5_shell_receiver_bind(&receiver,&session,G5_SHELL_PERMIT_CLOSE,apply,NULL)||
    !g5_shell_receiver_connect(&receiver,endpoint,&caps,receive,authority,
      AURORA_CAP_SYSTEM,AURORA_RIGHT_CONTROL,consumer))return false;
 uint8_t bytes[80]={0},args[8]={0};size_t n=0;
 struct g5_ipc_header h={
  .major=1,.minor=0,.header_bytes=48,.kind=G5_IPC_REQUEST,
  .operation=G5_OP_WINDOW_CLOSE,.payload_bytes=8,.request_id=1,
  .session_generation=71,.object_generation=1
 };
 enum g5_ipc_status status=G5_IPC_DENIED;
 if(g5_ipc_encode(&h,args,bytes,sizeof(bytes),&n)!=G5_IPC_OK||
    !ipc_send(sender,NULL,bytes,(uint32_t)n,NULL,0)||
    !g5_shell_receiver_poll(&receiver,&status)||
    status!=G5_IPC_OK||effects!=1)return false;
 if(!ipc_send(sender,NULL,bytes,(uint32_t)n,NULL,0)||
    !g5_shell_receiver_poll(&receiver,&status)||
    status!=G5_IPC_DENIED||effects!=1)return false;
 /* A validly framed message for another session must have no effect. */
 h.request_id=2;
 h.session_generation=72;
 if(g5_ipc_encode(&h,args,bytes,sizeof(bytes),&n)!=G5_IPC_OK||
    !ipc_send(sender,NULL,bytes,(uint32_t)n,NULL,0)||
    !g5_shell_receiver_poll(&receiver,&status)||
    status!=G5_IPC_DENIED||effects!=1)return false;
 /* An unsupported receiver operation must also be rejected. */
 h.request_id=3;
 h.session_generation=71;
 h.operation=G5_OP_WINDOW_PLACE;
 h.payload_bytes=24;
 uint8_t place_args[24]={0};
 place_args[8]=1;place_args[12]=1;place_args[16]=1;
 if(g5_ipc_encode(&h,place_args,bytes,sizeof(bytes),&n)!=G5_IPC_OK||
    !ipc_send(sender,NULL,bytes,(uint32_t)n,NULL,0)||
    !g5_shell_receiver_poll(&receiver,&status)||
    status!=G5_IPC_DENIED||effects!=1)return false;
 /* Queued, otherwise valid IPC cannot execute without the receiver authority. */
 h.operation=G5_OP_WINDOW_CLOSE;
 h.payload_bytes=8;
 h.session_generation=71;
 h.request_id=4;
 if(g5_ipc_encode(&h,args,bytes,sizeof(bytes),&n)!=G5_IPC_OK||
    !ipc_send(sender,NULL,bytes,(uint32_t)n,NULL,0)||
    !cap_revoke(&caps,authority)||
    g5_shell_receiver_poll(&receiver,&status)||effects!=1)return false;
 /* Drain the refused request without dispatch, and grant a fresh capability. */
 struct aurora_ipc_received discarded={0};
 if(!ipc_receive(endpoint,&caps,&discarded))return false;
 authority=cap_grant(&caps,&channel,AURORA_CAP_SYSTEM,AURORA_RIGHT_CONTROL);
 if(authority==AURORA_CAP_INVALID)return false;
 receiver.endpoint.receiver_authority=authority;
 /* A revoked endpoint handle also denies a queued valid request. */
 h.request_id=5;
 if(g5_ipc_encode(&h,args,bytes,sizeof(bytes),&n)!=G5_IPC_OK||
    !ipc_send(sender,NULL,bytes,(uint32_t)n,NULL,0)||
    !cap_revoke(&caps,receive)||
    g5_shell_receiver_poll(&receiver,&status)||effects!=1)return false;
 if(!ipc_receive(endpoint,&caps,&discarded))return false;
 receive=cap_grant(&caps,endpoint,AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_READ);
 if(receive==AURORA_CAP_INVALID)return false;
 receiver.endpoint.receiver_endpoint_handle=receive;
 /* Session revocation must independently fail closed with live capabilities. */
 g5_shell_session_end(&session);
 h.request_id=6;
 if(g5_ipc_encode(&h,args,bytes,sizeof(bytes),&n)!=G5_IPC_OK||
    !ipc_send(sender,NULL,bytes,(uint32_t)n,NULL,0)||
    g5_shell_receiver_poll(&receiver,&status)||effects!=1)return false;
 g5_shell_receiver_revoke(&receiver);
 return cap_revoke(&caps,receive)&&cap_revoke(&caps,authority)&&
        !g5_shell_receiver_poll(&receiver,&status)&&effects==1;
}
