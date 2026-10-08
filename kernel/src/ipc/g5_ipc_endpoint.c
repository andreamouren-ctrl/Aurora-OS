#include <aurora/g5_ipc_endpoint.h>
bool g5_ipc_endpoint_poll(struct g5_ipc_endpoint_binding *b,
 enum g5_ipc_status *status) {
 if(status) *status=G5_IPC_DENIED;
 if(!b || !status || !b->receiver || !b->receiver_caps ||
    !b->dispatch || !b->provisioned_exclusively || !b->authority_rights)
  return false;
 struct aurora_ipc_received native={0};
 if(!ipc_receive(b->receiver,b->receiver_caps,&native)) return false;
 if(native.length>AURORA_SYS_IPC_PAYLOAD_MAX ||
    native.capability_count>AURORA_SYS_IPC_CAPS_MAX) {
  *status=G5_IPC_BAD_FORMAT; return true;
 }
 struct aurora_sys_ipc_received wire={0};
 wire.length=native.length;
 wire.capability_count=native.capability_count;
 for(uint32_t i=0;i<native.length;++i) wire.data[i]=native.data[i];
 for(uint32_t i=0;i<native.capability_count;++i)
  wire.capabilities[i]=native.capabilities[i];
 *status=g5_ipc_dispatch_authorized(
   b->dispatch,&wire,b->receiver_caps,b->receiver_authority,
   b->authority_type,b->authority_rights);
 /* v1 schemas allow no transferred caps. Revoke unexpected receiver handles
  * to avoid leaking delegated capabilities when rejecting malformed traffic. */
 for(uint32_t i=0;i<native.capability_count;++i)
  (void)cap_revoke(b->receiver_caps,native.capabilities[i]);
 return true;
}
