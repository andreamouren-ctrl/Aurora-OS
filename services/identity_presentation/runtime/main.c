#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <aurora/capability_abi.h>
#include <aurora/syscall_abi.h>
#include <aurora/identity_ring3_abi.h>

/* Explicit freestanding memory primitive. Clang may lower aggregate
 * zero-initialization into a memset even with -fno-builtin and -nostdlib. */
void *memset(void *destination,int value,size_t length) {
    volatile unsigned char *out=(volatile unsigned char *)destination;
    for (size_t i=0u;i<length;++i) out[i]=(unsigned char)value;
    return destination;
}

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
static bool draw_badge(
    uint64_t endpoint,
    const struct aurora_identity_ring3_draw_message *draw
) {
    if (!draw || draw->header.request_id==0u ||
        draw->width!=AURORA_IDENTITY_RING3_BADGE_WIDTH ||
        draw->height!=AURORA_IDENTITY_RING3_BADGE_HEIGHT ||
        draw->graphics_buffer==0u || draw->graphics_surface==0u ||
        sys3(AURORA_SYS_CAP_CHECK,draw->graphics_buffer,
            AURORA_CAP_GRAPHICS_BUFFER,
            AURORA_RIGHT_READ|AURORA_RIGHT_WRITE|AURORA_RIGHT_MAP)!=1u ||
        sys3(AURORA_SYS_CAP_CHECK,draw->graphics_buffer,
            AURORA_CAP_GRAPHICS_BUFFER,AURORA_RIGHT_TRANSFER)==1u ||
        sys3(AURORA_SYS_CAP_CHECK,draw->graphics_surface,
            AURORA_CAP_SURFACE,AURORA_RIGHT_READ|AURORA_RIGHT_WRITE)!=1u ||
        sys3(AURORA_SYS_CAP_CHECK,draw->graphics_surface,
            AURORA_CAP_SURFACE,AURORA_RIGHT_TRANSFER)==1u)
        return false;
    const uint64_t ptr=sys2(
        AURORA_SYS_GRAPHICS_BUFFER_MAP,draw->graphics_buffer,1u);
    if (ptr==0u || ptr==AURORA_SYS_RESULT_ERROR) return false;
    volatile uint32_t *pixels=(volatile uint32_t *)(uintptr_t)ptr;
    for (uint32_t y=0u;y<draw->height;++y)
        for (uint32_t x=0u;x<draw->width;++x) {
            /* Small Aurora cyan/gold footer detail. No credential pixels,
             * key lengths, user identifiers or even state strings appear. */
            uint32_t pixel=UINT32_C(0xff071a33);
            if (y==0u || y==draw->height-1u)
                pixel=AURORA_IDENTITY_RING3_BADGE_FIRST_PIXEL;
            else if (x>3u && x<12u && y>3u && y<12u)
                pixel=UINT32_C(0xffdfbb66);
            else if (x>=20u && x<124u && y>=6u && y<=9u)
                pixel=UINT32_C(0xff197c9e);
            pixels[(size_t)y*draw->width+x]=pixel;
        }
    if (sys1(AURORA_SYS_GRAPHICS_BUFFER_UNMAP,ptr)!=0u ||
        sys2(AURORA_SYS_GRAPHICS_SURFACE_ATTACH,
             draw->graphics_surface,draw->graphics_buffer)!=0u ||
        sys5(AURORA_SYS_GRAPHICS_SURFACE_DAMAGE,
             draw->graphics_surface,0u,0u,draw->width,draw->height)!=0u)
        return false;
    const uint64_t commit=sys1(
        AURORA_SYS_GRAPHICS_SURFACE_COMMIT,draw->graphics_surface);
    if (commit==0u || commit==AURORA_SYS_RESULT_ERROR) return false;
    const struct aurora_identity_ring3_draw_ack ack={
        .header={
            .abi_version=AURORA_IDENTITY_RING3_ABI_VERSION,
            .type=AURORA_IDENTITY_RING3_DRAW_ACK,
            .request_id=draw->header.request_id
        },.commit_serial=commit};
    return sys5(AURORA_SYS_IPC_SEND,endpoint,
                (uint64_t)(uintptr_t)&ack,sizeof(ack),0u,0u)==0u;
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
            (received.length!=sizeof(struct aurora_identity_ring3_message) &&
             received.length!=sizeof(struct aurora_identity_ring3_draw_message)))
            return 16;
        struct aurora_identity_ring3_message message={0};
        for (size_t i=0u;i<sizeof(message);++i)
            ((uint8_t *)&message)[i]=received.data[i];
        if (message.abi_version!=AURORA_IDENTITY_RING3_ABI_VERSION ||
            message.request_id==0u) return 17;
        if (message.type==AURORA_IDENTITY_RING3_DRAW_BADGE) {
            if (received.length!=sizeof(struct aurora_identity_ring3_draw_message))
                return 21;
            struct aurora_identity_ring3_draw_message draw={0};
            for (size_t i=0u;i<sizeof(draw);++i)
                ((uint8_t *)&draw)[i]=received.data[i];
            if (!draw_badge(startup.control_endpoint,&draw))
                return 22;
        } else if (message.type==AURORA_IDENTITY_RING3_HEALTH) {
            if (received.length!=sizeof(message)) return 23;
            if (!send(startup.control_endpoint,AURORA_IDENTITY_RING3_HEALTH_ACK,
                      message.request_id)) return 18;
        } else if (message.type==AURORA_IDENTITY_RING3_SHUTDOWN) {
            if (received.length!=sizeof(message)) return 24;
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
