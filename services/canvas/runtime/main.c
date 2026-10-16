#include <aurora/g5_canvas_session_camera.h>
#include <aurora/g5_canvas_bootstrap.h>
#include <aurora/capability_abi.h>
#include <aurora/syscall_abi.h>

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
    struct g5_canvas_session_camera camera={0};
    g5_canvas_session_camera_revoke(&camera);
    return 0;
}
