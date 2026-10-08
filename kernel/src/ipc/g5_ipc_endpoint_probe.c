#include <aurora/g5_ipc_endpoint.h>
#include <stddef.h>

static struct aurora_ipc_channel probe_channel;
static struct aurora_cap_table probe_caps;
static uint32_t probe_executions;
static struct g5_pending_queue probe_pending;

static bool probe_authorize(void *ctx,uint32_t op,uint64_t generation) {
 (void)ctx; return op==G5_OP_WINDOW_CLOSE && generation==17;
}
static bool probe_execute(void *ctx,const struct g5_ipc_header *h,
                          const uint8_t *payload) {
 (void)ctx;(void)payload;
 if(h->operation!=G5_OP_WINDOW_CLOSE) return false;
 ++probe_executions; return true;
}
bool g5_ipc_endpoint_self_test(void) {
 ipc_channel_init(&probe_channel);
 cap_table_init(&probe_caps);
 probe_executions=0;
 g5_pending_reset(&probe_pending,17);
 struct aurora_ipc_endpoint *sender=ipc_channel_endpoint(&probe_channel,0);
 struct aurora_ipc_endpoint *receiver=ipc_channel_endpoint(&probe_channel,1);
 if(!sender || !receiver) return false;
 aurora_cap_handle authority=cap_grant(
   &probe_caps,&probe_channel,AURORA_CAP_SYSTEM,AURORA_RIGHT_CONTROL);
 if(authority==AURORA_CAP_INVALID) return false;
 aurora_cap_handle recv_handle=cap_grant(&probe_caps,receiver,
    AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_READ);
 if(recv_handle==AURORA_CAP_INVALID) return false;
 struct g5_dispatch_context d={
   .active_session_generation=17,
   .authorize=probe_authorize,.handler=probe_execute
 };
 struct g5_ipc_endpoint_binding b={
   .receiver=receiver,.receiver_endpoint_handle=recv_handle,
   .receiver_caps=&probe_caps,.dispatch=&d,
   .pending_requests=&probe_pending,
   .receiver_authority=authority,.authority_type=AURORA_CAP_SYSTEM,
   .authority_rights=AURORA_RIGHT_CONTROL,.provisioned_exclusively=true
 };
 uint8_t bytes[64]={0},args[8]={0};size_t n=0;
 struct g5_ipc_header h={
   .major=1,.minor=0,.header_bytes=48,.kind=G5_IPC_REQUEST,
   .operation=G5_OP_WINDOW_CLOSE,.payload_bytes=8,.request_id=1,
   .session_generation=17,.object_generation=1
 };
 if(g5_ipc_encode(&h,args,bytes,sizeof(bytes),&n)!=G5_IPC_OK) return false;
 if(!ipc_send(sender,NULL,bytes,(uint32_t)n,NULL,0)) return false;
 enum g5_ipc_status result=G5_IPC_DENIED;
 if(!g5_ipc_endpoint_poll(&b,&result) || result!=G5_IPC_OK ||
    probe_executions!=1) return false;
 if(!ipc_send(sender,NULL,bytes,(uint32_t)n,NULL,0)) return false;
 if(!g5_ipc_endpoint_poll(&b,&result) || result!=G5_IPC_DENIED ||
    probe_executions!=1) return false;
 if(g5_pending_add(&probe_pending,17,55)!=G5_PENDING_OK)return false;
 h.kind=G5_IPC_CANCEL;h.payload_bytes=0;h.request_id=55;
 if(g5_ipc_encode(&h,NULL,bytes,sizeof(bytes),&n)!=G5_IPC_OK)return false;
 if(!ipc_send(sender,NULL,bytes,(uint32_t)n,NULL,0))return false;
 if(!g5_ipc_endpoint_poll(&b,&result) || result!=G5_IPC_OK ||
    probe_pending.count!=0)return false;
 h.kind=G5_IPC_REQUEST;h.payload_bytes=8;
 g5_ipc_dispatch_revoke(&d);
 h.request_id=2;
 if(g5_ipc_encode(&h,args,bytes,sizeof(bytes),&n)!=G5_IPC_OK) return false;
 if(!ipc_send(sender,NULL,bytes,(uint32_t)n,NULL,0)) return false;
 if(!g5_ipc_endpoint_poll(&b,&result) || result!=G5_IPC_DENIED ||
    probe_executions!=1) return false;
 return cap_revoke(&probe_caps,recv_handle) &&
        cap_revoke(&probe_caps,authority);
}
