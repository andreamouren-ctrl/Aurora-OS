#include <aurora/g5_ipc_dispatch.h>

enum g5_ipc_status g5_ipc_dispatch(
 struct g5_dispatch_context *d,
 const struct aurora_sys_ipc_received *message
) {
 if(d==NULL || message==NULL)return G5_IPC_BAD_ARGUMENT;
 /* Single active executor across CPUs and nested callbacks. */
 if(__atomic_exchange_n(&d->dispatch_in_progress,true,__ATOMIC_ACQUIRE))
  return G5_IPC_DENIED;
 enum g5_ipc_status result=G5_IPC_DENIED;
 uint64_t session=__atomic_load_n(&d->active_session_generation,__ATOMIC_ACQUIRE);
 if(!session || d->authorize==NULL || d->handler==NULL)goto out;
 struct g5_ipc_header h;
 const uint8_t *payload=NULL;
 result=g5_ipc_decode_received(message,&h,&payload);
 if(result!=G5_IPC_OK)goto out;
 result=g5_ipc_validate_schema(&h,message->capability_count);
 if(result!=G5_IPC_OK)goto out;
 result=g5_ipc_validate_semantics(&h,payload);
 if(result!=G5_IPC_OK)goto out;
 if(h.session_generation!=session ||
    h.request_id<=__atomic_load_n(&d->last_request_id,__ATOMIC_ACQUIRE)) {
  result=G5_IPC_DENIED;goto out;
 }
 if(!d->authorize(d->context,h.operation,h.session_generation) ||
    __atomic_load_n(&d->active_session_generation,__ATOMIC_ACQUIRE)!=session) {
  result=G5_IPC_DENIED;goto out;
 }
 /* Durable reservation is required for irreversible commands when enabled.
  * Failed journal writes MUST cause no user-visible side effect. */
 if((d->require_durable_reservation && d->reserve==NULL) ||
    (d->reserve && !d->reserve(d->reserve_context,session,h.request_id)) ||
    __atomic_load_n(&d->active_session_generation,__ATOMIC_ACQUIRE)!=session) {
  result=G5_IPC_DENIED;goto out;
 }
 /* In-memory retry guard remains even if handler reports failure after an
  * effect. For crash safety use the durable reserve callback above. */
 __atomic_store_n(&d->last_request_id,h.request_id,__ATOMIC_RELEASE);
 result=d->handler(d->context,&h,payload)?G5_IPC_OK:G5_IPC_DENIED;
out:
 __atomic_store_n(&d->dispatch_in_progress,false,__ATOMIC_RELEASE);
 return result;
}

void g5_ipc_dispatch_revoke(struct g5_dispatch_context *d) {
 if(!d)return;
 uint64_t old=__atomic_exchange_n(&d->active_session_generation,0,
                                   __ATOMIC_ACQ_REL);
 uint64_t seen=__atomic_load_n(&d->last_revoked_generation,__ATOMIC_ACQUIRE);
 while(old>seen &&
       !__atomic_compare_exchange_n(&d->last_revoked_generation,&seen,old,
                                    false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
 }
 __atomic_store_n(&d->last_request_id,0,__ATOMIC_RELEASE);
}

bool g5_ipc_dispatch_bind_session(struct g5_dispatch_context *d,
 uint64_t generation
) {
 if(!d || generation==0 ||
    __atomic_load_n(&d->dispatch_in_progress,__ATOMIC_ACQUIRE) ||
    generation<=__atomic_load_n(&d->last_revoked_generation,__ATOMIC_ACQUIRE))
  return false;
 uint64_t expected=0;
 __atomic_store_n(&d->last_request_id,0,__ATOMIC_RELEASE);
 return __atomic_compare_exchange_n(&d->active_session_generation,&expected,
             generation,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
