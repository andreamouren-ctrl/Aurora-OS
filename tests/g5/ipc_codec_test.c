#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <aurora/g5_ipc_abi.h>

static struct g5_ipc_header valid_header(uint32_t size) {
    return (struct g5_ipc_header) {
        .major=1,.minor=0,.header_bytes=48,.kind=G5_IPC_REQUEST,
        .operation=0x01000001u,.flags=0,.payload_bytes=size,
        .request_id=7,.session_generation=11,.object_generation=15
    };
}
int main(void) {
    uint8_t bytes[257]={0}, data[208]={0};
    for (unsigned i=0;i<208;++i) data[i]=(uint8_t)i;
    struct g5_ipc_header h=valid_header(208), decoded={0};
    const uint8_t *payload=0;
    size_t n=0;
    assert(G5_IPC_WIRE_HEADER_BYTES==48 && G5_IPC_WIRE_INLINE_MAX==208);
    assert(g5_ipc_encode(&h,data,bytes,sizeof(bytes),&n)==G5_IPC_OK && n==256);
    assert(memcmp(bytes,"G5IP",4)==0 && bytes[4]==1 && bytes[8]==48);
    assert(g5_ipc_decode(bytes,n,&decoded,&payload)==G5_IPC_OK);
    assert(decoded.operation==h.operation && decoded.request_id==7);
    assert(decoded.object_generation==15 && decoded.payload_bytes==208);
    assert(memcmp(payload,data,208)==0);
    assert(g5_ipc_decode(bytes,255,&decoded,&payload)==G5_IPC_BAD_FORMAT);
    assert(g5_ipc_decode(bytes,257,&decoded,&payload)==G5_IPC_TOO_LARGE);
    bytes[0]=0;
    assert(g5_ipc_decode(bytes,n,&decoded,&payload)==G5_IPC_BAD_FORMAT);
    bytes[0]='G';
    bytes[4]=2;
    assert(g5_ipc_decode(bytes,n,&decoded,&payload)==G5_IPC_UNSUPPORTED_VERSION);
    bytes[4]=1;
    bytes[16]=1;
    assert(g5_ipc_decode(bytes,n,&decoded,&payload)==G5_IPC_UNSUPPORTED_FLAGS);
    bytes[16]=0;
    bytes[20]=209;
    assert(g5_ipc_decode(bytes,n,&decoded,&payload)==G5_IPC_TOO_LARGE);
    h=valid_header(209);
    assert(g5_ipc_encode(&h,data,bytes,sizeof(bytes),&n)==G5_IPC_TOO_LARGE);
    h=valid_header(0);
    assert(g5_ipc_encode(&h,NULL,bytes,47,&n)==G5_IPC_TOO_LARGE);
    assert(g5_ipc_encode(&h,NULL,bytes,48,&n)==G5_IPC_OK && n==48);
    assert(g5_ipc_decode(bytes,n,&decoded,&payload)==G5_IPC_OK);
    assert(g5_ipc_decode(bytes,47,&decoded,&payload)==G5_IPC_BAD_FORMAT);
    h=valid_header(0); h.session_generation=0;
    assert(g5_ipc_encode(&h,NULL,bytes,sizeof(bytes),&n)==G5_IPC_BAD_FORMAT);
    puts("G5 IPC v1 codec contract tests: PASS");
    return 0;
}
