#include <aurora/g5_ipc_durable.h>
#include <stddef.h>
#define G5_DURABLE_RECORD_BYTES 32u
static void put64(uint8_t *p,uint64_t n) {
 for(unsigned i=0;i<8;++i)p[i]=(uint8_t)(n>>(8u*i));
}
static uint64_t get64(const uint8_t *p) {
 uint64_t n=0;
 for(unsigned i=0;i<8;++i)n|=(uint64_t)p[i]<<(8u*i);
 return n;
}
static uint64_t checksum(const uint8_t *p) {
 uint64_t v=UINT64_C(14695981039346656037);
 for(unsigned i=0;i<24;++i) {
  v^=p[i];v*=UINT64_C(1099511628211);
 }
 return v;
}
static void encode(uint8_t bytes[G5_DURABLE_RECORD_BYTES],uint64_t g,uint64_t id) {
 for(unsigned i=0;i<G5_DURABLE_RECORD_BYTES;++i)bytes[i]=0;
 bytes[0]='G';bytes[1]='5';bytes[2]='D';bytes[3]='L';
 bytes[4]=1;
 put64(bytes+8,g);put64(bytes+16,id);
 put64(bytes+24,checksum(bytes));
}
static bool decode(const uint8_t *bytes,size_t n,uint64_t *g,uint64_t *id) {
 if(n!=G5_DURABLE_RECORD_BYTES || bytes[0]!='G'||bytes[1]!='5'||
    bytes[2]!='D'||bytes[3]!='L'||bytes[4]!=1||
    bytes[5]||bytes[6]||bytes[7]||
    get64(bytes+24)!=checksum(bytes))return false;
 *g=get64(bytes+8);*id=get64(bytes+16);
 return *g!=0;
}
bool g5_durable_restore(struct g5_ipc_durable_ledger *d,uint64_t generation) {
 if(!d||!d->capabilities||!d->state||!d->record_name||!generation)
  return false;
 d->loaded=false; d->generation=0; d->last_request_id=0;
 uint8_t bytes[G5_DURABLE_RECORD_BYTES]={0};
 size_t len=0;
 enum aurora_protected_state_read_result r=protected_state_read_record(
    d->capabilities,d->handle,d->state,d->record_name,bytes,sizeof(bytes),&len);
 if(r==AURORA_PROTECTED_STATE_READ_ERROR)return false;
 if(r==AURORA_PROTECTED_STATE_READ_OK) {
  uint64_t previous_generation=0,previous_id=0;
  if(!decode(bytes,len,&previous_generation,&previous_id) ||
     previous_generation>generation)return false;
  if(previous_generation==generation)d->last_request_id=previous_id;
 }
 d->generation=generation; d->loaded=true;
 return true;
}
bool g5_durable_reserve(struct g5_ipc_durable_ledger *d,
 uint64_t generation,uint64_t request_id) {
 if(!d||!d->loaded||!generation||generation!=d->generation||
    !request_id||request_id<=d->last_request_id)return false;
 uint8_t bytes[G5_DURABLE_RECORD_BYTES];
 encode(bytes,generation,request_id);
 if(protected_state_replace_record_durable(
   d->capabilities,d->handle,d->state,d->record_name,bytes,sizeof(bytes))
   !=AURORA_PROTECTED_STATE_REPLACE_OK) return false;
 d->last_request_id=request_id;
 return true;
}

bool g5_durable_reserve_callback(void *context,uint64_t generation,
 uint64_t request_id) {
 return g5_durable_reserve((struct g5_ipc_durable_ledger *)context,
                           generation,request_id);
}
