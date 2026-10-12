#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <aurora/capability_abi.h>
#include <aurora/syscall_abi.h>
#include <aurora/identity_ring3_abi.h>

/* Minimal independent Ring3 pre-session host. Only IPC lifecycle is
 * admitted in this first slice: no secrets, no graphics, no profile grants. */
static uint64_t sys1(uint64_t n, uint64_t a) {
    register uint64_t rax __asm__("rax")=n;
    register uint64_t rdi __asm__("rdi")=a;
    __asm__ volatile("syscall":"+a"(rax):"D"(rdi):"rcx","r11","memory");
    return rax;
}
static uint64_t sys2(uint64_t n,uint64_t a,uint64_t b) {
    register uint64_t rax __asm__("rax")=n;
    register uint64_t rdi __asm__("rdi")=a;
    register uint64_t rsi __asm__("rsi")=b;
    __asm__ volatile("syscall":"+a"(rax):"D"(rdi),"S"(rsi):"rcx","r11","memory");
    return rax;
}
static uint64_t sys3(uint64_t n,uint64_t a,uint64_t b,uint64_t c) {
    register uint64_t rax __asm__("rax")=n;
    register uint64_t rdi __asm__("rdi")=a;
    register uint64_t rsi __asm__("rsi")=b;
    register uint64_t rdx __asm__("rdx")=c;
    __asm__ volatile("syscall":"+a"(rax):"D"(rdi),"S"(rsi),"d"(rdx):"rcx","r11","memory");
    return rax;
}
static uint64_t sys5(uint64_t n,uint64_t a,uint64_t b,uint64_t c,
                     uint64_t d,uint64_t e) {
    register uint64_t rax __asm__("rax")=n;
    register uint64_t rdi __asm__("rdi")=a;
    register uint64_t rsi __asm__("rsi")=b;
    register uint64_t rdx __asm__("rdx")=c;
    register uint64_t r10 __asm__("r10")=d;
    register uint64_t r8 __asm__("r8")=e;
    __asm__ volatile("syscall":"+a"(rax):"D"(rdi),"S"(rsi),"d"(rdx),
                     "r"(r10),"r"(r8):"rcx","r11","memory");
    return rax;
}
static bool send(uint64_t endpoint,uint32_t type,uint64_t id) {
    const struct aurora_identity_ring3_message m={
        .abi_version=AURORA_IDENTITY_RING3_ABI_VERSION,.type=type,.request_id=id};
    return sys5(AURORA_SYS_IPC_SEND,endpoint,(uint64_t)(uintptr_t)&m,
                sizeof(m),0u,0u)==0u;
}
int64_t identity_ring3_main(uint64_t original_rsp) {
    if (original_rsp < AURORA_IDENTITY_RING3_STARTUP_OFFSET) return 11;
    const struct aurora_identity_ring3_startup startup=
        *(const struct aurora_identity_ring3_startup *)(uintptr_t)(
            original_rsp-AURORA_IDENTITY_RING3_STARTUP_OFFSET);
    if (startup.magic != AURORA_IDENTITY_RING3_READY_MAGIC ||
        startup.instance_epoch == 0u || startup.control_endpoint == 0u ||
        sys3(AURORA_SYS_CAP_CHECK,startup.control_endpoint,
             AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_READ|AURORA_RIGHT_WRITE)!=1u ||
        sys3(AURORA_SYS_CAP_CHECK,startup.control_endpoint,
             AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_TRANSFER)==1u)
        return 12;
    /* The kernel checks a separate bootstrap proof in addition to IPC READY. */
    if (sys1(AURORA_SYS_BOOTSTRAP_SIGNAL,AURORA_IDENTITY_RING3_READY_MAGIC)!=0u)
        return 13;
    if (!send(startup.control_endpoint,AURORA_IDENTITY_RING3_READY,
              startup.instance_epoch)) return 14;

    for (;;) {
        if (sys2(AURORA_SYS_IPC_WAIT,startup.control_endpoint,0u)!=0u)
            return 15;
        struct aurora_sys_ipc_received received={0};
        if (sys2(AURORA_SYS_IPC_RECEIVE,startup.control_endpoint,
                 (uint64_t)(uintptr_t)&received)!=0u ||
            received.capability_count!=0u ||
            received.length!=sizeof(struct aurora_identity_ring3_message))
            return 16;
        struct aurora_identity_ring3_message message={0};
        for (size_t i=0u;i<sizeof(message);++i)
            ((uint8_t *)&message)[i]=received.data[i];
        if (message.abi_version!=AURORA_IDENTITY_RING3_ABI_VERSION ||
            message.request_id==0u) return 17;
        if (message.type==AURORA_IDENTITY_RING3_HEALTH) {
            if (!send(startup.control_endpoint,AURORA_IDENTITY_RING3_HEALTH_ACK,
                      message.request_id)) return 18;
        } else if (message.type==AURORA_IDENTITY_RING3_SHUTDOWN) {
            if (!send(startup.control_endpoint,AURORA_IDENTITY_RING3_SHUTDOWN_ACK,
                      message.request_id)) return 19;
            return 0;
        } else {
            /* No credential payload, drawing request or delegated operation
             * is accepted by this lifecycle-only isolated host. */
            return 20;
        }
    }
}
