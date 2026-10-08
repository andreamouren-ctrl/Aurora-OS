#include <aurora/g5_ipc_pending.h>
#include <stddef.h>
void g5_pending_reset(struct g5_pending_queue *q,uint64_t g) {
 if (!q) return;
 for (uint32_t i=0;i<G5_IPC_PENDING_LIMIT;++i) {
  q->entries[i].occupied=false;
  q->entries[i].generation=0;
  q->entries[i].request_id=0;
 }
 q->count=0; q->active_generation=g;
}
enum g5_pending_result g5_pending_add(struct g5_pending_queue *q,
 uint64_t g,uint64_t id) {
 if (!q || !g || !id) return G5_PENDING_INVALID;
 if (g!=q->active_generation) return G5_PENDING_STALE;
 if (q->count > G5_IPC_PENDING_LIMIT) return G5_PENDING_INVALID;
 for (uint32_t i=0;i<G5_IPC_PENDING_LIMIT;++i)
  if(q->entries[i].occupied && q->entries[i].request_id==id)
   return G5_PENDING_DUPLICATE;
 if(q->count==G5_IPC_PENDING_LIMIT) return G5_PENDING_FULL;
 for (uint32_t i=0;i<G5_IPC_PENDING_LIMIT;++i)
  if(!q->entries[i].occupied) {
   q->entries[i]=(struct g5_pending_entry){id,g,true};
   ++q->count; return G5_PENDING_OK;
  }
 return G5_PENDING_INVALID;
}
enum g5_pending_result g5_pending_remove(struct g5_pending_queue *q,
 uint64_t g,uint64_t id) {
 if(!q || !g || !id) return G5_PENDING_INVALID;
 if(g!=q->active_generation) return G5_PENDING_STALE;
 for(uint32_t i=0;i<G5_IPC_PENDING_LIMIT;++i)
  if(q->entries[i].occupied && q->entries[i].request_id==id &&
     q->entries[i].generation==g) {
   q->entries[i].occupied=false;
   q->entries[i].request_id=0; q->entries[i].generation=0;
   --q->count; return G5_PENDING_OK;
  }
 return G5_PENDING_NOT_FOUND;
}

enum g5_pending_result g5_pending_accept_control(
 struct g5_pending_queue *q,
 const struct aurora_sys_ipc_received *msg
) {
 if(!q || !msg) return G5_PENDING_INVALID;
 struct g5_ipc_header h;
 const uint8_t *payload=NULL;
 if(g5_ipc_decode_received(msg,&h,&payload)!=G5_IPC_OK)
  return G5_PENDING_INVALID;
 if(h.session_generation!=q->active_generation) return G5_PENDING_STALE;
 if(h.kind==G5_IPC_CANCEL) {
  /* A cancel refers to request_id, not to a new executable operation. */
  if(!g5_ipc_opcode_known(h.operation) ||
     h.payload_bytes!=0 || msg->capability_count!=0)
   return G5_PENDING_INVALID;
  return g5_pending_remove(q,h.session_generation,h.request_id);
 }
 if(h.kind!=G5_IPC_REQUEST ||
    g5_ipc_validate_schema(&h,msg->capability_count)!=G5_IPC_OK)
  return G5_PENDING_INVALID;
 return g5_pending_add(q,h.session_generation,h.request_id);
}
