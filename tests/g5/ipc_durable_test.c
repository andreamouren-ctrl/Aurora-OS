#include <assert.h>
#include <string.h>
#include <aurora/g5_ipc_durable.h>

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
 return 0;
}
