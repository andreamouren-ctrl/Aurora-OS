#include <assert.h>
#include <aurora/g5_ipc_abi.h>
int main(void) {
 struct g5_ipc_header h={
  .major=1,.minor=0,.header_bytes=48,.kind=G5_IPC_REQUEST,
  .operation=G5_OP_WINDOW_CONFIGURE,.payload_bytes=16,
  .request_id=1,.session_generation=1,.object_generation=2
 };
 uint8_t bytes[24]={0};
 bytes[0]=1;bytes[8]=128;bytes[12]=64;
 assert(g5_ipc_validate_semantics(&h,bytes)==G5_IPC_OK);
 bytes[8]=0;
 assert(g5_ipc_validate_semantics(&h,bytes)==G5_IPC_BAD_FORMAT);
 bytes[8]=128;
 h.operation=G5_OP_WINDOW_CONFIGURE_ACK;
 bytes[8]=2;bytes[12]=0; /* ACK generation is a full uint64 LE */
 assert(g5_ipc_validate_semantics(&h,bytes)==G5_IPC_OK);
 bytes[8]=3;
 assert(g5_ipc_validate_semantics(&h,bytes)==G5_IPC_BAD_FORMAT);
 h.operation=G5_OP_WINDOW_PLACE;h.payload_bytes=24;
 bytes[8]=64;bytes[12]=64;bytes[16]=1;
 assert(g5_ipc_validate_semantics(&h,bytes)==G5_IPC_OK);
 bytes[0]=255;bytes[1]=255;bytes[2]=127;
 assert(g5_ipc_validate_semantics(&h,bytes)==G5_IPC_BAD_FORMAT);
 h.operation=G5_OP_WINDOW_CLOSE;h.payload_bytes=8;
 for(unsigned i=0;i<24;++i)bytes[i]=0;
 assert(g5_ipc_validate_semantics(&h,bytes)==G5_IPC_OK);
 bytes[0]=4;
 assert(g5_ipc_validate_semantics(&h,bytes)==G5_IPC_BAD_FORMAT);
 h.operation=G5_OP_SCENE_PREPARE;h.payload_bytes=16;
 bytes[0]=1;bytes[8]=1;
 assert(g5_ipc_validate_semantics(&h,bytes)==G5_IPC_OK);
 bytes[8]=0;
 assert(g5_ipc_validate_semantics(&h,bytes)==G5_IPC_BAD_FORMAT);
 h.kind=G5_IPC_EVENT;h.operation=G5_OP_SHELL_READY;h.payload_bytes=0;
 h.object_generation=0;
 assert(g5_ipc_validate_semantics(&h,NULL)==G5_IPC_OK);
 h.object_generation=1;
 assert(g5_ipc_validate_semantics(&h,NULL)==G5_IPC_BAD_FORMAT);
 assert(g5_ipc_opcode_required_rights(G5_OP_WINDOW_CLOSE)==AURORA_RIGHT_CONTROL);
 assert(g5_ipc_opcode_required_rights(G5_OP_SCENE_PUBLISH)==
   (AURORA_RIGHT_CONTROL|AURORA_RIGHT_WRITE));
 assert(g5_ipc_opcode_required_rights(G5_OP_SHELL_READY)==AURORA_RIGHT_READ);
 assert(g5_ipc_opcode_required_rights(0xffffffffu)==0);
 return 0;
}
