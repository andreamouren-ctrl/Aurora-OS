#include <aurora/g5_ipc_endpoint.h>
bool g5_ipc_endpoint_poll(struct g5_ipc_endpoint_binding *b,
 enum g5_ipc_status *status) {
 if(status) *status=G5_IPC_DENIED;
 if(!b || !status || !b->receiver || !b->receiver_caps ||
    !b->dispatch || !b->provisioned_exclusively || !b->authority_rights ||
    b->receiver_endpoint_handle==AURORA_CAP_INVALID)
  return false;
 if(b->trusted_consumer_thread!=0 &&
    scheduler_current_thread_id()!=b->trusted_consumer_thread)
  return false;
 if(__atomic_exchange_n(&b->poll_in_progress,1u,__ATOMIC_ACQUIRE)!=0u)
  return false;
 bool received=false;
 struct aurora_capability_view view={0};
 if(!cap_lookup(b->receiver_caps,b->receiver_endpoint_handle,
                AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_READ,&view) ||
    view.object!=b->receiver) goto done;
 if(!g5_ipc_kernel_cap_check(b->receiver_caps,b->receiver_authority,
                             b->authority_type,b->authority_rights))
  goto done;
 struct aurora_ipc_received native={0};
 if(!ipc_receive(b->receiver,b->receiver_caps,&native)) goto done;
 received=true;
 if(native.length>AURORA_SYS_IPC_PAYLOAD_MAX ||
    native.capability_count>AURORA_SYS_IPC_CAPS_MAX) {
  *status=G5_IPC_BAD_FORMAT;goto done;
 }
 struct aurora_sys_ipc_received wire={0};
 wire.length=native.length;
 wire.capability_count=native.capability_count;
 for(uint32_t i=0;i<native.length;++i)wire.data[i]=native.data[i];
 for(uint32_t i=0;i<native.capability_count;++i)
  wire.capabilities[i]=native.capabilities[i];
 struct g5_ipc_header header;
 const uint8_t *payload=NULL;
 enum g5_ipc_status parsed=g5_ipc_decode_received(&wire,&header,&payload);
 if(parsed!=G5_IPC_OK){*status=parsed;goto cleanup;}
 uint64_t required=g5_ipc_opcode_required_rights(header.operation);
 if(!required || (b->authority_rights & required)!=required ||
    !g5_ipc_kernel_cap_check(b->receiver_caps,b->receiver_authority,
                             b->authority_type,required)) {
  *status=G5_IPC_DENIED;goto cleanup;
 }
 bool tracked=false;
 if(b->pending_requests &&
    (header.kind==G5_IPC_REQUEST || header.kind==G5_IPC_CANCEL)) {
  enum g5_pending_result pending=g5_pending_accept_control(
    b->pending_requests,&wire);
  if(pending!=G5_PENDING_OK) {
   if(header.kind==G5_IPC_CANCEL && pending==G5_PENDING_NOT_FOUND)
    *status=G5_IPC_OK; /* repeated cancel is idempotent */
   else
    *status=pending==G5_PENDING_FULL?G5_IPC_QUEUE_FULL:G5_IPC_DENIED;
   goto cleanup;
  }
  if(header.kind==G5_IPC_CANCEL) {
   *status=G5_IPC_OK;
   goto cleanup;
  }
  tracked=true;
 }
 *status=g5_ipc_dispatch_authorized(
   b->dispatch,&wire,b->receiver_caps,b->receiver_authority,
   b->authority_type,b->authority_rights);
 if(tracked) {
  (void)g5_pending_remove(b->pending_requests,
                           header.session_generation,header.request_id);
 }
cleanup:
 /* v1 schemas disallow transferred caps; free receiver-local handles. */
 for(uint32_t i=0;i<native.capability_count;++i)
  (void)cap_revoke(b->receiver_caps,native.capabilities[i]);
done:
 __atomic_store_n(&b->poll_in_progress,0u,__ATOMIC_RELEASE);
 return received;
}
