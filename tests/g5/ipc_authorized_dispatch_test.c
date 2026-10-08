#include <assert.h>
#include <aurora/g5_ipc_authorized_dispatch.h>

static unsigned handled;
bool cap_lookup(struct aurora_cap_table *table,aurora_cap_handle handle,
                enum aurora_cap_type type,uint64_t rights,
                struct aurora_capability_view *out) {
    if (table==NULL || out==NULL) return false;
    return handle==42 && type==AURORA_CAP_SYSTEM &&
           rights==AURORA_RIGHT_CONTROL;
}
static bool allow(void *ctx,uint32_t op,uint64_t session) {
    (void)ctx;return op==G5_OP_WINDOW_CLOSE && session==5;
}
static bool execute(void *ctx,const struct g5_ipc_header *h,const uint8_t *p) {
    (void)ctx;(void)h;(void)p;++handled;return true;
}
int main(void) {
    struct aurora_cap_table table;
    struct g5_dispatch_context dispatch={.active_session_generation=5,.authorize=allow,.handler=execute};
    struct aurora_sys_ipc_received msg={0};
    struct g5_ipc_header header={1,0,48,G5_IPC_REQUEST,
                                  G5_OP_WINDOW_CLOSE,0,8,1,5,1};
    uint8_t payload[8]={0};size_t n=0;
    assert(g5_ipc_encode(&header,payload,msg.data,sizeof(msg.data),&n)==G5_IPC_OK);
    msg.length=(uint32_t)n;
    assert(g5_ipc_dispatch_authorized(&dispatch,&msg,&table,43,
        AURORA_CAP_SYSTEM,AURORA_RIGHT_CONTROL)==G5_IPC_DENIED);
    assert(handled==0 && dispatch.last_request_id==0);
    assert(g5_ipc_dispatch_authorized(&dispatch,&msg,&table,42,
        AURORA_CAP_SYSTEM,AURORA_RIGHT_WRITE)==G5_IPC_DENIED);
    assert(g5_ipc_dispatch_authorized(&dispatch,&msg,&table,42,
        AURORA_CAP_SYSTEM,AURORA_RIGHT_CONTROL)==G5_IPC_OK);
    assert(handled==1);
    assert(g5_ipc_dispatch_authorized(&dispatch,&msg,&table,42,
        AURORA_CAP_SYSTEM,AURORA_RIGHT_CONTROL)==G5_IPC_DENIED);
    return 0;
}
