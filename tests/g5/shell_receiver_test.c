#include <assert.h>
#include <stdint.h>
#include <aurora/g5_shell_receiver.h>
static unsigned effects;
static unsigned native_polls;
bool g5_ipc_endpoint_poll(struct g5_ipc_endpoint_binding *binding,
 enum g5_ipc_status *status) {
 if(!binding||!binding->provisioned_exclusively||!status)return false;
 ++native_polls;*status=G5_IPC_OK;return true;
}

static bool apply(void *ctx,const struct g5_ipc_header *header,const uint8_t *payload) {
 (void)ctx;(void)payload;
 if(header->operation!=G5_OP_WINDOW_CLOSE)return false;
 ++effects;return true;
}
int main(void) {
 struct g5_shell_session session={0};
 struct g5_shell_receiver receiver={0};
 struct g5_ipc_header header={.kind=G5_IPC_REQUEST,.operation=G5_OP_WINDOW_CLOSE,
   .request_id=1,.session_generation=6};
 assert(!g5_shell_receiver_bind(&receiver,&session,G5_SHELL_PERMIT_CLOSE,apply,0));
 assert(g5_shell_session_ready(&session,6));
 assert(g5_shell_receiver_bind(&receiver,&session,G5_SHELL_PERMIT_CLOSE,apply,0));
 assert(!g5_shell_receiver_bind(&receiver,&session,G5_SHELL_PERMIT_CLOSE,apply,0));
 assert(receiver.dispatcher.active_session_generation==6);
 struct aurora_ipc_endpoint *endpoint=(void *)(uintptr_t)48;
 struct aurora_cap_table *caps=(void *)(uintptr_t)64;
 assert(!g5_shell_receiver_connect(&receiver,endpoint,caps,1,2,
   AURORA_CAP_SYSTEM,AURORA_RIGHT_CONTROL,0));
 assert(g5_shell_receiver_connect(&receiver,endpoint,caps,1,2,
   AURORA_CAP_SYSTEM,AURORA_RIGHT_CONTROL,42));
 assert(!g5_shell_receiver_connect(&receiver,endpoint,caps,1,2,
   AURORA_CAP_SYSTEM,AURORA_RIGHT_CONTROL,42));
 enum g5_ipc_status result=G5_IPC_DENIED;
 assert(g5_shell_receiver_poll(&receiver,&result)&&result==G5_IPC_OK&&native_polls==1);
 assert(receiver.dispatcher.authorize(receiver.dispatcher.context,G5_OP_WINDOW_CLOSE,6));
 assert(!receiver.dispatcher.authorize(receiver.dispatcher.context,G5_OP_WINDOW_PLACE,6));
 assert(receiver.dispatcher.handler(receiver.dispatcher.context,&header,0));
 assert(effects==1);
 g5_shell_session_end(&session);
 assert(!g5_shell_receiver_poll(&receiver,&result)&&native_polls==1);
 assert(!receiver.dispatcher.authorize(receiver.dispatcher.context,G5_OP_WINDOW_CLOSE,6));
 assert(!receiver.dispatcher.handler(receiver.dispatcher.context,&header,0));
 g5_shell_receiver_revoke(&receiver);
 assert(receiver.dispatcher.active_session_generation==0);
 assert(!receiver.endpoint.receiver);
 assert(!receiver.dispatcher.authorize(receiver.dispatcher.context,G5_OP_WINDOW_CLOSE,6));
 assert(g5_shell_session_ready(&session,7));
 assert(g5_shell_receiver_bind(&receiver,&session,G5_SHELL_PERMIT_CLOSE,apply,0));
 assert(!receiver.dispatcher.authorize(receiver.dispatcher.context,G5_OP_WINDOW_CLOSE,6));
 header.session_generation=7;
 assert(receiver.dispatcher.handler(receiver.dispatcher.context,&header,0));
 assert(effects==2);
 g5_shell_receiver_revoke(&receiver);
 g5_shell_session_end(&session);
 return 0;
}
