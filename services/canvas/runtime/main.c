#include <aurora/g5_canvas_session_camera.h>
#include <aurora/g5_canvas_bootstrap.h>
#include <aurora/capability_abi.h>
#include <aurora/syscall_abi.h>

/* Freestanding runtime: provide the compiler's required memory primitive
 * without depending on a hosted libc. */
void *memset(void *destination,int value,size_t count) {
    unsigned char *bytes=(unsigned char *)destination;
    for (size_t i=0;i<count;++i) bytes[i]=(unsigned char)value;
    return destination;
}

/* Startup is copied from the kernel-owned launch stack before any syscall.
 * Do not activate SSE camera operations until scheduler FPU isolation is proven. */
static uint64_t canvas_syscall3(uint64_t number,uint64_t a,uint64_t b,uint64_t c) {
    register uint64_t rax __asm__("rax")=number;
    register uint64_t rdi __asm__("rdi")=a;
    register uint64_t rsi __asm__("rsi")=b;
    register uint64_t rdx __asm__("rdx")=c;
    __asm__ volatile("syscall" : "+a"(rax) :
        "D"(rdi),"S"(rsi),"d"(rdx) : "rcx","r11","memory");
    return rax;
}

static uint64_t canvas_syscall5(uint64_t number,uint64_t a,uint64_t b,
                                uint64_t c,uint64_t d,uint64_t e) {
    register uint64_t rax __asm__("rax")=number;
    register uint64_t rdi __asm__("rdi")=a;
    register uint64_t rsi __asm__("rsi")=b;
    register uint64_t rdx __asm__("rdx")=c;
    register uint64_t r10 __asm__("r10")=d;
    register uint64_t r8 __asm__("r8")=e;
    __asm__ volatile("syscall" : "+a"(rax) :
        "D"(rdi),"S"(rsi),"d"(rdx),"r"(r10),"r"(r8) :
        "rcx","r11","memory");
    return rax;
}

static int canvas_send_ready(uint64_t endpoint,uint64_t generation) {
    const struct g5_canvas_bootstrap ready={
        .version=G5_CANVAS_BOOTSTRAP_VERSION,
        .type=G5_CANVAS_BOOTSTRAP_READY,
        .session_generation=generation,
        .request_id=G5_CANVAS_BOOTSTRAP_READY_REQUEST_ID
    };
    return canvas_syscall5(AURORA_SYS_IPC_SEND,endpoint,
        (uint64_t)(uintptr_t)&ready,sizeof(ready),0u,0u)==0u;
}

static int canvas_health_roundtrip(uint64_t endpoint,uint64_t generation) {
    struct aurora_sys_ipc_received received={0};
    if (canvas_syscall3(AURORA_SYS_IPC_WAIT,endpoint,0u,0u)!=0u)
        return 0;
    if (canvas_syscall3(AURORA_SYS_IPC_RECEIVE,endpoint,
        (uint64_t)(uintptr_t)&received,0u)!=0u ||
        received.capability_count!=0u ||
        received.length!=sizeof(struct g5_canvas_bootstrap))
        return 0;
    struct g5_canvas_bootstrap request={0};
    for (unsigned i=0u;i<sizeof(request);++i)
        ((uint8_t *)&request)[i]=received.data[i];
    if (request.type!=G5_CANVAS_BOOTSTRAP_HEALTH ||
        !g5_canvas_bootstrap_valid(&request,G5_CANVAS_BOOTSTRAP_HEALTH,
                                   generation,request.request_id) ||
        request.request_id==G5_CANVAS_BOOTSTRAP_READY_REQUEST_ID)
        return 0;
    request.type=G5_CANVAS_BOOTSTRAP_HEALTH_ACK;
    return canvas_syscall5(AURORA_SYS_IPC_SEND,endpoint,
        (uint64_t)(uintptr_t)&request,sizeof(request),0u,0u)==0u;
}

static int canvas_shutdown_roundtrip(uint64_t endpoint,uint64_t generation) {
    struct aurora_sys_ipc_received received={0};
    if (canvas_syscall3(AURORA_SYS_IPC_WAIT,endpoint,0u,0u)!=0u ||
        canvas_syscall3(AURORA_SYS_IPC_RECEIVE,endpoint,
            (uint64_t)(uintptr_t)&received,0u)!=0u ||
        received.capability_count!=0u ||
        received.length!=sizeof(struct g5_canvas_bootstrap)) return 0;
    struct g5_canvas_bootstrap request={0};
    for (unsigned i=0u;i<sizeof(request);++i)
        ((uint8_t *)&request)[i]=received.data[i];
    if (!g5_canvas_bootstrap_valid(&request,G5_CANVAS_BOOTSTRAP_SHUTDOWN,
                                   generation,request.request_id) ||
        request.request_id<=G5_CANVAS_BOOTSTRAP_READY_REQUEST_ID)
        return 0;
    request.type=G5_CANVAS_BOOTSTRAP_SHUTDOWN_ACK;
    return canvas_syscall5(AURORA_SYS_IPC_SEND,endpoint,
        (uint64_t)(uintptr_t)&request,sizeof(request),0u,0u)==0u;
}

int64_t g5_canvas_runtime_main(uint64_t initial_rsp) {
    if (initial_rsp<G5_CANVAS_STARTUP_STACK_OFFSET) return 11;
    const struct g5_canvas_startup *stack_startup=
        (const struct g5_canvas_startup *)(uintptr_t)(
            initial_rsp-G5_CANVAS_STARTUP_STACK_OFFSET);
    const struct g5_canvas_startup startup=*stack_startup;
    if (startup.abi_version!=G5_CANVAS_STARTUP_ABI_VERSION ||
        startup.reserved!=0u || startup.control_endpoint==0u ||
        startup.session_generation==0u) return 12;
    if (canvas_syscall3(AURORA_SYS_CAP_CHECK,startup.control_endpoint,
            AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_READ|AURORA_RIGHT_WRITE)!=1u ||
        canvas_syscall3(AURORA_SYS_CAP_CHECK,startup.control_endpoint,
            AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_TRANSFER)==1u)
        return 13;
    if (!canvas_send_ready(startup.control_endpoint,
                           startup.session_generation)) return 14;
    if (!canvas_health_roundtrip(startup.control_endpoint,
                                 startup.session_generation)) return 15;
    if (!canvas_shutdown_roundtrip(startup.control_endpoint,
                                   startup.session_generation)) return 16;
    struct g5_canvas_session_camera camera={0};
    g5_canvas_session_camera_revoke(&camera);
    return 0;
}
