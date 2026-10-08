#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>
#include <aurora/g5_ipc_dispatch.h>
static atomic_int entered,finish;
static unsigned effects;
static struct g5_dispatch_context dispatcher;
static struct aurora_sys_ipc_received first,second;
static bool authorize(void *p,uint32_t op,uint64_t gen) {
 (void)p;return op==G5_OP_WINDOW_CLOSE && gen==9;
}
static bool execute(void *p,const struct g5_ipc_header *h,const uint8_t *bytes) {
 (void)p;(void)h;(void)bytes;
 ++effects;atomic_store(&entered,1);
 while(!atomic_load(&finish))sched_yield();
 return true;
}
static void fill(struct aurora_sys_ipc_received *msg,uint64_t id) {
 struct g5_ipc_header h={
  .major=1,.minor=0,.header_bytes=48,.kind=G5_IPC_REQUEST,
  .operation=G5_OP_WINDOW_CLOSE,.payload_bytes=8,
  .request_id=id,.session_generation=9,.object_generation=1
 };
 const uint8_t zeros[8]={0};size_t size=0;
 assert(g5_ipc_encode(&h,zeros,msg->data,sizeof(msg->data),&size)==G5_IPC_OK);
 msg->length=(uint32_t)size;
}
static void *worker(void *arg) {
 (void)arg;
 assert(g5_ipc_dispatch(&dispatcher,&first)==G5_IPC_OK);
 return NULL;
}
int main(void) {
 dispatcher.active_session_generation=9;
 dispatcher.authorize=authorize;dispatcher.handler=execute;
 fill(&first,1);fill(&second,2);
 pthread_t thread;
 assert(pthread_create(&thread,NULL,worker,NULL)==0);
 for(unsigned i=0;!atomic_load(&entered) && i<1000000;++i)sched_yield();
 assert(atomic_load(&entered));
 assert(g5_ipc_dispatch(&dispatcher,&second)==G5_IPC_DENIED);
 g5_ipc_dispatch_revoke(&dispatcher);
 assert(!g5_ipc_dispatch_bind_session(&dispatcher,10));
 atomic_store(&finish,1);
 assert(pthread_join(thread,NULL)==0);
 assert(effects==1 && dispatcher.last_revoked_generation==9);
 assert(g5_ipc_dispatch_bind_session(&dispatcher,10));
 assert(g5_ipc_dispatch(&dispatcher,&second)==G5_IPC_DENIED);
 return 0;
}
