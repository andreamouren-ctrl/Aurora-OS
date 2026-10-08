#include <aurora/g5_ipc_endpoint.h>
#include <aurora/arch.h>
#include <aurora/clock.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/user_ipc_wait_probe.h>
#include <aurora/usercopy.h>

static struct aurora_ipc_channel g5_ring3_channel;
static struct aurora_cap_table g5_ring3_kernel_caps;
static uint32_t g5_ring3_effects;
static bool authorize(void *ctx,uint32_t op,uint64_t session) {
 (void)ctx;
 return op==G5_OP_WINDOW_CLOSE && session==61;
}
static bool dispatch(void *ctx,const struct g5_ipc_header *h,const uint8_t *p) {
 (void)ctx;(void)p;
 if(h->operation!=G5_OP_WINDOW_CLOSE)return false;
 ++g5_ring3_effects;
 return true;
}
bool g5_ipc_ring3_self_test(void) {
 ipc_channel_init(&g5_ring3_channel);
 cap_table_init(&g5_ring3_kernel_caps);
 g5_ring3_effects=0;
 struct aurora_ipc_endpoint *kernel=ipc_channel_endpoint(&g5_ring3_channel,0);
 struct aurora_ipc_endpoint *user=ipc_channel_endpoint(&g5_ring3_channel,1);
 if(!kernel || !user)return false;
 struct aurora_process *process=process_create_image(
   "g5-ipc-ring3-probe",
   user_ipc_wait_probe_image(),user_ipc_wait_probe_image_size());
 if(!process)return false;
 aurora_cap_handle user_handle=cap_grant(
   &process->capabilities,user,AURORA_CAP_IPC_ENDPOINT,
   AURORA_RIGHT_READ|AURORA_RIGHT_WRITE);
 if(user_handle==AURORA_CAP_INVALID)return false;
 uint64_t user_stack_handle=(uint64_t)user_handle;
 if(!copy_to_user(process,process->user_stack_top-8ull,
                  &user_stack_handle,sizeof(user_stack_handle)))return false;
 aurora_thread_id thread=scheduler_create_user_thread("g5-ipc-ring3",process);
 if(thread==0)return false;
 uint64_t deadline=clock_now_ns()+UINT64_C(500000000);
 while((process_bootstrap_signal(process)!=AURORA_USER_IPC_WAITING_MAGIC ||
        !scheduler_thread_blocked(thread)) &&
       !scheduler_thread_finished(thread) && clock_now_ns()<deadline)arch_idle();
 if(process_bootstrap_signal(process)!=AURORA_USER_IPC_WAITING_MAGIC ||
    !scheduler_thread_blocked(thread) || scheduler_thread_finished(thread))return false;
 uint8_t wire[64]={0},args[8]={0};size_t n=0;
 struct g5_ipc_header h={
   .major=1,.minor=0,.header_bytes=48,.kind=G5_IPC_REQUEST,
   .operation=G5_OP_WINDOW_CLOSE,.payload_bytes=8,
   .request_id=1,.session_generation=61,.object_generation=1
 };
 if(g5_ipc_encode(&h,args,wire,sizeof(wire),&n)!=G5_IPC_OK)return false;
 if(!ipc_send(kernel,NULL,wire,(uint32_t)n,NULL,0))return false;
 deadline=clock_now_ns()+UINT64_C(500000000);
 while((process_bootstrap_signal(process)!=AURORA_USER_IPC_WAIT_DONE_MAGIC ||
        !scheduler_thread_finished(thread)) &&
       !scheduler_thread_finished(thread) && clock_now_ns()<deadline)arch_idle();
 if(process_bootstrap_signal(process)!=AURORA_USER_IPC_WAIT_DONE_MAGIC ||
    !scheduler_thread_finished(thread) ||
    process_state(process)!=AURORA_PROCESS_EXITED ||
    process->exit_code!=0)return false;
 aurora_cap_handle endpoint_handle=cap_grant(
   &g5_ring3_kernel_caps,kernel,AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_READ);
 aurora_cap_handle authority_handle=cap_grant(
   &g5_ring3_kernel_caps,&g5_ring3_channel,AURORA_CAP_SYSTEM,AURORA_RIGHT_CONTROL);
 if(endpoint_handle==AURORA_CAP_INVALID ||
    authority_handle==AURORA_CAP_INVALID)return false;
 struct g5_dispatch_context d={
   .active_session_generation=61,.authorize=authorize,.handler=dispatch
 };
 struct g5_ipc_endpoint_binding b={
   .receiver=kernel,.receiver_endpoint_handle=endpoint_handle,
   .receiver_caps=&g5_ring3_kernel_caps,.dispatch=&d,
   .receiver_authority=authority_handle,
   .authority_type=AURORA_CAP_SYSTEM,.authority_rights=AURORA_RIGHT_CONTROL,
   .provisioned_exclusively=true
 };
 enum g5_ipc_status result=G5_IPC_DENIED;
 if(!g5_ipc_endpoint_poll(&b,&result) || result!=G5_IPC_OK ||
    g5_ring3_effects!=1)return false;
 return cap_revoke(&g5_ring3_kernel_caps,endpoint_handle) &&
        cap_revoke(&g5_ring3_kernel_caps,authority_handle);
}
