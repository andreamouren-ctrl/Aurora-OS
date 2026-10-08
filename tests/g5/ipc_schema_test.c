#include <assert.h>
#include <aurora/g5_ipc_abi.h>
int main(void) {
 struct g5_ipc_header h={1,0,48,G5_IPC_EVENT,G5_OP_SHELL_READY,0,0,1,1,0};
 assert(g5_ipc_validate_schema(&h,0)==G5_IPC_OK);
 assert(g5_ipc_validate_schema(&h,1)==G5_IPC_BAD_CAPABILITIES);
 h.kind=G5_IPC_REQUEST;
 assert(g5_ipc_validate_schema(&h,0)==G5_IPC_BAD_FORMAT);
 h.operation=G5_OP_WINDOW_CLOSE; h.payload_bytes=8;
 assert(g5_ipc_validate_schema(&h,0)==G5_IPC_OK);
 h.operation=0xffffffffu;
 assert(g5_ipc_validate_schema(&h,0)==G5_IPC_UNSUPPORTED_OPERATION);
 return 0;
}
