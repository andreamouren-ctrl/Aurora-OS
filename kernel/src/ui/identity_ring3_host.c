#include <stddef.h>
#include <stdint.h>
#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/identity_ring3_abi.h>
#include <aurora/identity_ring3_host.h>
#include <aurora/ipc.h>
#include <aurora/log.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/usercopy.h>

extern const uint8_t *identity_ring3_host_image(void);
extern size_t identity_ring3_host_image_size(void);

struct identity_ring3_runtime {
    struct aurora_process *process;
    aurora_thread_id thread;
    struct aurora_ipc_channel channel;
    struct aurora_cap_table kernel_caps;
    struct aurora_ipc_endpoint *kernel_endpoint;
    uint64_t instance_epoch;
    uint64_t next_id;
    bool started;
    bool fault_logged;
};
static struct identity_ring3_runtime ring3;
static uint64_t last_instance_epoch;

static void scrub(void *p,size_t n) {
    volatile uint8_t *b=(volatile uint8_t *)p;
    while (n-- != 0u) *b++=0u;
}
static bool receive(uint32_t type,uint64_t id) {
    const uint64_t deadline=clock_now_ns()+AURORA_IDENTITY_RING3_TIMEOUT_NS;
    while (clock_now_ns()<deadline) {
        struct aurora_ipc_received r={0};
        if (ring3.kernel_endpoint &&
            ipc_receive(ring3.kernel_endpoint,&ring3.kernel_caps,&r)) {
            const struct aurora_identity_ring3_message expected={
                .abi_version=AURORA_IDENTITY_RING3_ABI_VERSION,
                .type=type,.request_id=id};
            bool ok=r.capability_count==0u && r.length==sizeof(expected);
            if (ok) {
                const uint8_t *a=(const uint8_t *)&expected;
                for (size_t i=0u;i<sizeof(expected);++i)
                    if (r.data[i]!=a[i]) {ok=false;break;}
            }
            scrub(&r,sizeof(r));
            return ok;
        }
        if (ring3.thread && scheduler_thread_finished(ring3.thread))
            return false;
        arch_idle();
    }
    return false;
}
static bool reap_finished(void) {
    if (ring3.thread && !scheduler_thread_finished(ring3.thread))
        return false;
    if (ring3.thread) {
        if (!scheduler_reap_thread(ring3.thread)) return false;
        ring3.thread=0u;
    }
    if (ring3.process) {
        if (process_live_thread_count(ring3.process)!=0u)
            return false;
        if (process_state(ring3.process)==AURORA_PROCESS_RUNNING)
            process_mark_exited(ring3.process,1);
        if (!process_reap(ring3.process,NULL) ||
            !process_release(ring3.process)) return false;
        ring3.process=NULL;
    }
    cap_table_destroy(&ring3.kernel_caps);
    scrub(&ring3,sizeof(ring3));
    return true;
}
bool identity_ring3_host_alive(void) {
    return ring3.started && ring3.process && ring3.thread &&
        process_state(ring3.process)==AURORA_PROCESS_RUNNING &&
        !scheduler_thread_finished(ring3.thread);
}
bool identity_ring3_host_start(void) {
    if (identity_ring3_host_alive()) return true;
    if (!reap_finished() || last_instance_epoch==UINT64_MAX)
        return false;
    ring3.instance_epoch=++last_instance_epoch;
    ring3.next_id=1u;
    ring3.process=process_create_image(
        "identity-pre-session",
        identity_ring3_host_image(),identity_ring3_host_image_size());
    if (!ring3.process) return false;
    ipc_channel_init(&ring3.channel);
    cap_table_init(&ring3.kernel_caps);
    ring3.kernel_endpoint=ipc_channel_endpoint(&ring3.channel,0u);
    struct aurora_ipc_endpoint *user=ipc_channel_endpoint(&ring3.channel,1u);
    if (!ring3.kernel_endpoint || !user) goto failure;
    aurora_cap_handle handle=cap_grant(
        &ring3.process->capabilities,user,AURORA_CAP_IPC_ENDPOINT,
        AURORA_RIGHT_READ|AURORA_RIGHT_WRITE);
    if (handle==AURORA_CAP_INVALID) goto failure;
    /* The whole granted authority is one non-transferable IPC endpoint.
     * No DISPLAY, FILE, PROFILE, INPUT or Identity authentication grants. */
    const struct aurora_identity_ring3_startup startup={
        .control_endpoint=handle,
        .instance_epoch=ring3.instance_epoch,
        .magic=AURORA_IDENTITY_RING3_READY_MAGIC
    };
    if (sizeof(startup)!=AURORA_IDENTITY_RING3_STARTUP_OFFSET ||
        !copy_to_user(ring3.process,
           ring3.process->user_stack_top-sizeof(startup),
           &startup,sizeof(startup))) goto failure;
    ring3.thread=scheduler_create_user_thread(
        "identity-pre-session",ring3.process);
    if (!ring3.thread) goto failure;
    if (!receive(AURORA_IDENTITY_RING3_READY,ring3.instance_epoch) ||
        process_bootstrap_signal(ring3.process)!=
            AURORA_IDENTITY_RING3_READY_MAGIC) goto failure;
    ring3.started=true;
    if (!identity_ring3_host_health_check()) goto failure;
    log_line("[identity-g2-ring3] isolated IPC-only pre-session process READY and healthy");
    return true;
failure:
    ring3.started=false;
    /* Fail closed: no authority is given to the failed worker, and the
     * kernel compositor/input path remains independent and operational.
     * Do not free a live process/channel that can still reference them. */
    (void)reap_finished();
    log_line("[identity-g2-ring3] presenter startup failed; trusted kernel UI retained");
    return false;
}
bool identity_ring3_host_health_check(void) {
    if (!identity_ring3_host_alive() ||
        ring3.next_id==UINT64_MAX) return false;
    struct aurora_identity_ring3_message m={
        .abi_version=AURORA_IDENTITY_RING3_ABI_VERSION,
        .type=AURORA_IDENTITY_RING3_HEALTH,
        .request_id=ring3.next_id++};
    return ring3.kernel_endpoint &&
        ipc_send(ring3.kernel_endpoint,&ring3.kernel_caps,&m,sizeof(m),NULL,0u) &&
        receive(AURORA_IDENTITY_RING3_HEALTH_ACK,m.request_id) &&
        identity_ring3_host_alive();
}
bool identity_ring3_host_stop(void) {
    if (!ring3.process) return true;
    if (!identity_ring3_host_alive()) return reap_finished();
    struct aurora_identity_ring3_message m={
        .abi_version=AURORA_IDENTITY_RING3_ABI_VERSION,
        .type=AURORA_IDENTITY_RING3_SHUTDOWN,
        .request_id=ring3.next_id++};
    bool ack=ring3.kernel_endpoint &&
        ipc_send(ring3.kernel_endpoint,&ring3.kernel_caps,&m,sizeof(m),NULL,0u) &&
        receive(AURORA_IDENTITY_RING3_SHUTDOWN_ACK,m.request_id);
    const uint64_t deadline=clock_now_ns()+AURORA_IDENTITY_RING3_TIMEOUT_NS;
    while (ring3.thread && !scheduler_thread_finished(ring3.thread) &&
           clock_now_ns()<deadline) arch_idle();
    ring3.started=false;
    return ack && reap_finished();
}
void identity_ring3_host_poll(void) {
    if (ring3.started && !identity_ring3_host_alive() &&
        !ring3.fault_logged) {
        ring3.fault_logged=true;
        ring3.started=false;
        log_line("[identity-g2-ring3] process exited/faulted; trusted kernel login stays authoritative");
    }
}
bool identity_ring3_host_self_test(void) {
    /* Real Ring3 READY/HEALTH/SHUTDOWN/REAP then fresh epoch restart.
     * This test issues no privilege transfer and never touches a key. */
    return identity_ring3_host_start() &&
           identity_ring3_host_health_check() &&
           identity_ring3_host_stop() &&
           identity_ring3_host_start() &&
           identity_ring3_host_health_check();
}
