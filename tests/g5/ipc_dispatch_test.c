#include <assert.h>
#include <string.h>
#include <aurora/g5_ipc_dispatch.h>

struct counters { unsigned accepted; unsigned handled; bool allow; bool handle; };
static bool authorize(void *ctx,uint32_t op,uint64_t generation) {
 struct counters *s=ctx; ++s->accepted;
 return s->allow && op==G5_OP_WINDOW_CLOSE && generation==17;
}
static bool handle(void *ctx,const struct g5_ipc_header *h,const uint8_t *p) {
 struct counters *s=ctx;
 if(!s->handle || h->payload_bytes!=8 || p==NULL)return false;
 ++s->handled; return true;
}
static struct aurora_sys_ipc_received packet(uint64_t id,uint64_t session) {
 struct aurora_sys_ipc_received r={0};
 const uint8_t bytes[8]={0}; size_t n=0;
 struct g5_ipc_header h={1,0,48,G5_IPC_REQUEST,G5_OP_WINDOW_CLOSE,
                          0,8,id,session,1};
 assert(g5_ipc_encode(&h,bytes,r.data,sizeof(r.data),&n)==G5_IPC_OK);
 r.length=(uint32_t)n;
 return r;
}
int main(void) {
 struct counters count={0,0,true,true};
 struct g5_dispatch_context d={17,0,authorize,handle,&count};
 struct aurora_sys_ipc_received m=packet(1,17);
 assert(g5_ipc_dispatch(&d,&m)==G5_IPC_OK);
 assert(d.last_request_id==1 && count.handled==1);
 assert(g5_ipc_dispatch(&d,&m)==G5_IPC_DENIED);
 m=packet(2,18);
 assert(g5_ipc_dispatch(&d,&m)==G5_IPC_DENIED);
 count.allow=false; m=packet(2,17);
 assert(g5_ipc_dispatch(&d,&m)==G5_IPC_DENIED);
 assert(d.last_request_id==1);
 count.allow=true;count.handle=false;
 assert(g5_ipc_dispatch(&d,&m)==G5_IPC_DENIED);
 count.handle=true;
 assert(g5_ipc_dispatch(&d,&m)==G5_IPC_OK);
 assert(d.last_request_id==2);
 m.capability_count=1; m.capabilities[0]=12;
 assert(g5_ipc_dispatch(&d,&m)==G5_IPC_BAD_CAPABILITIES);
 m=packet(3,17);m.data[0]=0;
 assert(g5_ipc_dispatch(&d,&m)==G5_IPC_BAD_FORMAT);
 d.authorize=NULL;m=packet(3,17);
 assert(g5_ipc_dispatch(&d,&m)==G5_IPC_DENIED);
 return 0;
}
