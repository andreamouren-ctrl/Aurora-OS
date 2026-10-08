#include <assert.h>
#include <string.h>
#include <aurora/g5_ipc_durable.h>
#include <aurora/g5_ipc_dispatch.h>

static uint8_t record[64];
static size_t record_len;
static bool fail_write;
enum aurora_protected_state_read_result protected_state_read_record(
 struct aurora_cap_table *table,aurora_cap_handle handle,
 struct aurora_protected_state_namespace *state,const char *name,
 void *output,size_t capacity,size_t *length) {
 (void)table;(void)handle;(void)state;(void)name;
 if(!record_len)return AURORA_PROTECTED_STATE_READ_NOT_FOUND;
 if(capacity<record_len)return AURORA_PROTECTED_STATE_READ_ERROR;
 memcpy(output,record,record_len);*length=record_len;
 return AURORA_PROTECTED_STATE_READ_OK;
}
enum aurora_protected_state_replace_result protected_state_replace_record_durable(
 struct aurora_cap_table *table,aurora_cap_handle handle,
 struct aurora_protected_state_namespace *state,const char *name,
 const void *input,size_t length) {
 (void)table;(void)handle;(void)state;(void)name;
 if(fail_write || length>sizeof(record))return AURORA_PROTECTED_STATE_REPLACE_ERROR;
 memcpy(record,input,length);record_len=length;
 return AURORA_PROTECTED_STATE_REPLACE_OK;
}
static unsigned effects;
static bool authorize(void *ctx,uint32_t op,uint64_t g) {
 (void)ctx;return op==G5_OP_WINDOW_CLOSE && g==9;
}
static bool execute(void *ctx,const struct g5_ipc_header *h,const uint8_t *p) {
 (void)ctx;(void)h;(void)p;++effects;return true;
}
int main(void) {
 struct aurora_cap_table caps;
 struct aurora_protected_state_namespace ns={0};
 struct g5_ipc_durable_ledger d={
    .capabilities=&caps,.handle=1,.state=&ns,.record_name="g5-ledger"
 };
 assert(g5_durable_restore(&d,7));
 assert(g5_durable_reserve(&d,7,1));
 assert(!g5_durable_reserve(&d,7,1));
 struct g5_ipc_durable_ledger recovered={
    .capabilities=&caps,.handle=1,.state=&ns,.record_name="g5-ledger"
 };
 assert(g5_durable_restore(&recovered,7));
 assert(recovered.last_request_id==1);
 assert(!g5_durable_reserve(&recovered,7,1));
 fail_write=true;
 assert(!g5_durable_reserve(&recovered,7,2));
 assert(recovered.last_request_id==1);
 fail_write=false;
 assert(g5_durable_reserve(&recovered,7,2));
 assert(g5_durable_restore(&d,8));
 assert(d.last_request_id==0);
 assert(g5_durable_reserve(&d,8,1));
 assert(!g5_durable_restore(&recovered,7));
 record[9]^=1;
 assert(!g5_durable_restore(&d,8));
 record_len=0;
 struct g5_ipc_durable_ledger l={
   .capabilities=&caps,.handle=1,.state=&ns,.record_name="g5-ledger"
 };
 assert(g5_durable_restore(&l,9));
 struct g5_dispatch_context disp={
  .active_session_generation=9,.authorize=authorize,.handler=execute,
  .require_durable_reservation=true,
  .reserve=g5_durable_reserve_callback,.reserve_context=&l
 };
 struct aurora_sys_ipc_received msg={0};
 struct g5_ipc_header h={
  .major=1,.minor=0,.header_bytes=48,.kind=G5_IPC_REQUEST,
  .operation=G5_OP_WINDOW_CLOSE,.payload_bytes=8,
  .request_id=1,.session_generation=9,.object_generation=1
 };
 uint8_t zeros[8]={0};size_t n=0;
 assert(g5_ipc_encode(&h,zeros,msg.data,sizeof(msg.data),&n)==G5_IPC_OK);
 msg.length=(uint32_t)n;
 assert(g5_ipc_dispatch(&disp,&msg)==G5_IPC_OK && effects==1);
 struct g5_ipc_durable_ledger restarted={
   .capabilities=&caps,.handle=1,.state=&ns,.record_name="g5-ledger"
 };
 assert(g5_durable_restore(&restarted,9));
 disp.last_request_id=0;disp.reserve_context=&restarted;
 assert(g5_ipc_dispatch(&disp,&msg)==G5_IPC_DENIED && effects==1);
 h.request_id=2;
 assert(g5_ipc_encode(&h,zeros,msg.data,sizeof(msg.data),&n)==G5_IPC_OK);
 msg.length=(uint32_t)n;
 fail_write=true;
 assert(g5_ipc_dispatch(&disp,&msg)==G5_IPC_DENIED && effects==1);
 fail_write=false;
 assert(g5_ipc_dispatch(&disp,&msg)==G5_IPC_OK && effects==2);
 return 0;
}
