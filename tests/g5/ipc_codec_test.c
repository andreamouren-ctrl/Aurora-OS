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
static bool check_surface_control(void *ctx, aurora_cap_handle handle,
    enum aurora_cap_type type, uint64_t rights) {
    (void)ctx;
    return handle == 88 && type == AURORA_CAP_SURFACE &&
           rights == AURORA_RIGHT_CONTROL;
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
    h=valid_header(0);
    assert(g5_ipc_encode(&h,NULL,bytes,sizeof(bytes),&n)==G5_IPC_OK);
    struct aurora_sys_ipc_received received={0};
    memcpy(received.data,bytes,n);
    received.length=(uint32_t)n;
    received.capability_count=1;
    received.capabilities[0]=77;
    assert(g5_ipc_decode_received(&received,&decoded,&payload)==G5_IPC_OK);
    received.capabilities[0]=0;
    assert(g5_ipc_decode_received(&received,&decoded,&payload)==G5_IPC_BAD_CAPABILITIES);
    received.capability_count=2;
    received.capabilities[0]=88;
    received.capabilities[1]=88;
    assert(g5_ipc_decode_received(&received,&decoded,&payload)==G5_IPC_BAD_CAPABILITIES);
    received.capabilities[1]=89;
    assert(g5_ipc_decode_received(&received,&decoded,&payload)==G5_IPC_OK);
    received.capability_count=5;
    assert(g5_ipc_decode_received(&received,&decoded,&payload)==G5_IPC_BAD_CAPABILITIES);
    received.capability_count=0;
    received.length=257;
    assert(g5_ipc_decode_received(&received,&decoded,&payload)==G5_IPC_TOO_LARGE);
    received.capability_count=1;
    received.length=(uint32_t)n;
    received.capabilities[0]=88;
    struct g5_ipc_cap_requirement needed={
        AURORA_CAP_SURFACE,AURORA_RIGHT_CONTROL
    };
    assert(g5_ipc_opcode_known(G5_OP_WINDOW_CONFIGURE));
    assert(!g5_ipc_opcode_known(0xff000001u));
    assert(g5_ipc_validate_caps(&received,&needed,1,check_surface_control,NULL)==G5_IPC_OK);
    needed.rights=AURORA_RIGHT_WRITE;
    assert(g5_ipc_validate_caps(&received,&needed,1,check_surface_control,NULL)==G5_IPC_DENIED);
    needed.rights=AURORA_RIGHT_CONTROL;
    assert(g5_ipc_validate_caps(&received,&needed,1,NULL,NULL)==G5_IPC_DENIED);
    received.capability_count=0;
    assert(g5_ipc_validate_caps(&received,NULL,0,NULL,NULL)==G5_IPC_OK);
    puts("G5 IPC v1 codec contract tests: PASS");
    return 0;
}
