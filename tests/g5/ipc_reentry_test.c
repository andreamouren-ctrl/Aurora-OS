#include <assert.h>
#include <aurora/g5_ipc_dispatch.h>

struct state {
    struct g5_dispatch_context *dispatcher;
    struct aurora_sys_ipc_received nested;
    unsigned runs;
};
static bool authorize(void *opaque, uint32_t op, uint64_t session) {
    (void)opaque;
    return op == G5_OP_WINDOW_CLOSE && session == 9;
}
static bool execute(void *opaque, const struct g5_ipc_header *h,
                    const uint8_t *payload) {
    struct state *s = opaque;
    (void)h; (void)payload;
    ++s->runs;
    assert(g5_ipc_dispatch(s->dispatcher, &s->nested) == G5_IPC_DENIED);
    return true;
}
int main(void) {
    struct state s = {0};
    struct g5_dispatch_context d = {
        .active_session_generation=9, .authorize=authorize,
        .handler=execute, .context=&s
    };
    s.dispatcher=&d;
    uint8_t zeros[8]={0};
    size_t n=0;
    struct g5_ipc_header h = {
        1,0,48,G5_IPC_REQUEST,G5_OP_WINDOW_CLOSE,0,8,2,9,1
    };
    assert(g5_ipc_encode(&h,zeros,s.nested.data,sizeof(s.nested.data),&n)==G5_IPC_OK);
    s.nested.length=(uint32_t)n;
    struct aurora_sys_ipc_received first=s.nested;
    h.request_id=1;
    assert(g5_ipc_encode(&h,zeros,first.data,sizeof(first.data),&n)==G5_IPC_OK);
    first.length=(uint32_t)n;
    assert(g5_ipc_dispatch(&d,&first)==G5_IPC_OK);
    assert(s.runs==1 && d.last_request_id==1);
    assert(g5_ipc_dispatch(&d,&s.nested)==G5_IPC_OK);
    assert(s.runs==2);
    return 0;
}
