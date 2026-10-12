#include <stddef.h>
#include <stdint.h>
#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/identity_ring3_abi.h>
#include <aurora/display.h>
#include <aurora/graphics_buffer.h>
#include <aurora/graphics_surface.h>
#include <aurora/memory_object.h>
#include <aurora/pmm.h>
#include <aurora/software_compositor.h>
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
    struct aurora_software_compositor *badge_compositor;
    struct aurora_graphics_buffer *badge_buffer;
    struct aurora_graphics_surface *badge_surface;
    aurora_cap_handle badge_buffer_cap;
    aurora_cap_handle badge_surface_cap;
    uint64_t badge_node;
    uint64_t badge_commit;
    uint64_t badge_epoch;
    bool badge_sealed;
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
void identity_ring3_host_detach_badge(void) {
    /* The compositor's scene membership is revoked BEFORE capability and
     * graphic object teardown. No privileged surface survives its owner. */
    if (ring3.badge_compositor && ring3.badge_node)
        (void)software_compositor_remove_surface(
            ring3.badge_compositor,ring3.badge_node);
    ring3.badge_node=0u;
    ring3.badge_compositor=NULL;
    ring3.badge_epoch=0u;
    ring3.badge_commit=0u;
    ring3.badge_sealed=false;
    if (ring3.process && ring3.badge_surface_cap)
        (void)graphics_surface_detach_buffers(
            &ring3.process->capabilities,ring3.badge_surface_cap);
    if (ring3.process && ring3.badge_surface_cap)
        (void)cap_revoke(&ring3.process->capabilities,ring3.badge_surface_cap);
    if (ring3.process && ring3.badge_buffer_cap)
        (void)cap_revoke(&ring3.process->capabilities,ring3.badge_buffer_cap);
    ring3.badge_surface_cap=AURORA_CAP_INVALID;
    ring3.badge_buffer_cap=AURORA_CAP_INVALID;
    if (ring3.badge_surface)
        (void)graphics_surface_release_owner(
            ring3.badge_surface,ring3.badge_surface->generation);
    if (ring3.badge_buffer)
        (void)graphics_buffer_release_owner(
            ring3.badge_buffer,ring3.badge_buffer->generation);
    ring3.badge_surface=NULL;
    ring3.badge_buffer=NULL;
}
bool identity_ring3_host_badge_active(void) {
    return identity_ring3_host_alive() && ring3.badge_sealed &&
        ring3.badge_compositor && ring3.badge_node &&
        ring3.badge_commit && ring3.badge_epoch==ring3.instance_epoch &&
        software_compositor_node_in_active_scene(
            ring3.badge_compositor,ring3.badge_node);
}
static bool reap_finished(void) {
    identity_ring3_host_detach_badge();
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
    uint32_t startup_step=1u;
    ring3.instance_epoch=++last_instance_epoch;
    ring3.next_id=1u;
    ring3.process=process_create_image(
        "identity-pre-session",
        identity_ring3_host_image(),identity_ring3_host_image_size());
    if (!ring3.process) return false;
    startup_step=2u;
    ipc_channel_init(&ring3.channel);
    cap_table_init(&ring3.kernel_caps);
    ring3.kernel_endpoint=ipc_channel_endpoint(&ring3.channel,0u);
    struct aurora_ipc_endpoint *user=ipc_channel_endpoint(&ring3.channel,1u);
    if (!ring3.kernel_endpoint || !user) goto failure;
    aurora_cap_handle handle=cap_grant(
        &ring3.process->capabilities,user,AURORA_CAP_IPC_ENDPOINT,
        AURORA_RIGHT_READ|AURORA_RIGHT_WRITE);
    if (handle==AURORA_CAP_INVALID) goto failure;
    startup_step=3u;
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
    startup_step=4u;
    ring3.thread=scheduler_create_user_thread(
        "identity-pre-session",ring3.process);
    if (!ring3.thread) goto failure;
    startup_step=5u;
    if (!receive(AURORA_IDENTITY_RING3_READY,ring3.instance_epoch) ||
        process_bootstrap_signal(ring3.process)!=
            AURORA_IDENTITY_RING3_READY_MAGIC) goto failure;
    startup_step=6u;
    ring3.started=true;
    if (!identity_ring3_host_health_check()) goto failure;
    log_line("[identity-g2-ring3] isolated IPC-only pre-session process READY and healthy");
    return true;
failure:
    log_write("[identity-g2-ring3] startup failure step: ");
    log_u64(startup_step);
    if (ring3.process) {
        log_write(" process-state/exit/fault/proof: ");
        log_u64(process_state(ring3.process));
        log_write("/");
        log_u64((uint64_t)ring3.process->exit_code);
        log_write("/");
        log_u64(ring3.process->fault_vector);
        log_write("/");
        log_u64(process_bootstrap_signal(ring3.process));
    }
    log_line("");
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
static bool receive_badge_ack(uint64_t id,uint64_t *out_serial) {
    if (out_serial) *out_serial=0u;
    uint64_t deadline=clock_now_ns()+AURORA_IDENTITY_RING3_TIMEOUT_NS;
    while (clock_now_ns()<deadline) {
        struct aurora_ipc_received response={0};
        if (ring3.kernel_endpoint &&
            ipc_receive(ring3.kernel_endpoint,&ring3.kernel_caps,&response)) {
            struct aurora_identity_ring3_draw_ack ack={0};
            bool valid=response.capability_count==0u &&
                response.length==sizeof(ack);
            if (valid) {
                uint8_t *dst=(uint8_t *)&ack;
                for (size_t i=0;i<sizeof(ack);++i) dst[i]=response.data[i];
                valid=ack.header.abi_version==AURORA_IDENTITY_RING3_ABI_VERSION &&
                      ack.header.type==AURORA_IDENTITY_RING3_DRAW_ACK &&
                      ack.header.request_id==id &&
                      ack.commit_serial!=0u;
                if (valid && out_serial) *out_serial=ack.commit_serial;
            }
            scrub(&ack,sizeof(ack));
            scrub(&response,sizeof(response));
            return valid;
        }
        if (!ring3.thread || scheduler_thread_finished(ring3.thread))
            return false;
        arch_idle();
    }
    return false;
}
bool identity_ring3_host_attach_badge(
    struct aurora_software_compositor *compositor,
    struct aurora_cap_table *authority,
    aurora_cap_handle display_control,
    uint32_t output_width,uint32_t output_height
) {
    if (!identity_ring3_host_alive() || ring3.badge_buffer ||
        !compositor || !compositor->initialized ||
        !compositor->secure_scene_active || !authority ||
        output_width<480u || output_height<400u ||
        ring3.next_id==UINT64_MAX) return false;
    const struct aurora_display_mode *mode=display_mode_at(0u,0u);
    if (!mode || mode->format.bits_per_pixel!=32u ||
        mode->format.alpha_mask_size!=0u ||
        mode->format.red_mask_shift!=16u ||
        mode->format.green_mask_shift!=8u ||
        mode->format.blue_mask_shift!=0u) return false;
    ring3.badge_buffer=graphics_buffer_create(
        AURORA_IDENTITY_RING3_BADGE_WIDTH,
        AURORA_IDENTITY_RING3_BADGE_HEIGHT,&mode->format);
    ring3.badge_surface=graphics_surface_create();
    if (!ring3.badge_buffer || !ring3.badge_surface) goto failed;
    /* Ring3 can only map/commit its tiny private visual surface. Neither
     * DISPLAY CONTROL nor profile/credential input is delegated. */
    ring3.badge_buffer_cap=graphics_buffer_grant(
        &ring3.process->capabilities,ring3.badge_buffer,
        AURORA_RIGHT_READ|AURORA_RIGHT_WRITE|AURORA_RIGHT_MAP);
    ring3.badge_surface_cap=graphics_surface_grant(
        &ring3.process->capabilities,ring3.badge_surface,
        AURORA_RIGHT_READ|AURORA_RIGHT_WRITE);
    if (!ring3.badge_buffer_cap || !ring3.badge_surface_cap) goto failed;
    struct aurora_identity_ring3_draw_message draw={0};
    draw.header.abi_version=AURORA_IDENTITY_RING3_ABI_VERSION;
    draw.header.type=AURORA_IDENTITY_RING3_DRAW_BADGE;
    draw.header.request_id=ring3.next_id++;
    draw.graphics_buffer=ring3.badge_buffer_cap;
    draw.graphics_surface=ring3.badge_surface_cap;
    draw.width=AURORA_IDENTITY_RING3_BADGE_WIDTH;
    draw.height=AURORA_IDENTITY_RING3_BADGE_HEIGHT;
    uint64_t ack_serial=0u;
    if (!ipc_send(ring3.kernel_endpoint,&ring3.kernel_caps,
                  &draw,sizeof(draw),NULL,0u) ||
        !receive_badge_ack(draw.header.request_id,&ack_serial)) goto failed;
    /* Cross-check the real committed buffer + first written pixel, not
     * just an ACK sent by the Ring3 worker. */
    struct aurora_graphics_surface_snapshot snapshot={0};
    if (!graphics_surface_read_committed(
          &ring3.process->capabilities,ring3.badge_surface_cap,&snapshot))
        goto failed;
    bool valid=snapshot.buffer==ring3.badge_buffer &&
        snapshot.commit_serial==ack_serial &&
        snapshot.buffer->width==AURORA_IDENTITY_RING3_BADGE_WIDTH &&
        snapshot.buffer->height==AURORA_IDENTITY_RING3_BADGE_HEIGHT;
    graphics_surface_snapshot_release(&snapshot);
    uint64_t physical=0u;
    if (!valid || !memory_object_page_at(
            ring3.badge_buffer->memory,0u,&physical)) goto failed;
    const uint32_t *pixel=(const uint32_t *)pmm_phys_to_virt(physical);
    if (!pixel || pixel[0]!=AURORA_IDENTITY_RING3_BADGE_FIRST_PIXEL)
        goto failed;

    /* The untrusted process must relinquish all writable access BEFORE
     * privileged scene admission. Its signed runtime has already unmapped
     * the buffer before sending the draw ACK. Revoking both handles also
     * prevents further MAP, DAMAGE, ATTACH and COMMIT syscalls. */
    if (ring3.badge_buffer->memory->mapping_refs!=0u ||
        !cap_revoke(&ring3.process->capabilities,ring3.badge_surface_cap))
        goto failed;
    ring3.badge_surface_cap=AURORA_CAP_INVALID;
    if (!cap_revoke(&ring3.process->capabilities,ring3.badge_buffer_cap))
        goto failed;
    ring3.badge_buffer_cap=AURORA_CAP_INVALID;
    if (ring3.badge_buffer->memory->mapping_refs!=0u)
        goto failed;
    ring3.badge_sealed=true;
    log_line("[identity-g2-ring3] visual buffer sealed; Ring3 graphics caps revoked and mappings zero");

    /* First genuine Ring3-owned non-secret layout: a centered thin
     * divider in the lower decorative region, safely below Aurora Key,
     * prompt, status and authorization text. Never covers trusted fields. */
    const int32_t x=(int32_t)((output_width-
        AURORA_IDENTITY_RING3_BADGE_WIDTH)/2u);
    const int32_t y=(int32_t)(output_height*82u/100u);
    ring3.badge_compositor=compositor;
    if (!software_compositor_add_privileged_surface(
          compositor,authority,display_control,ring3.badge_surface,
          AURORA_COMPOSITOR_SURFACE_PRE_SESSION,
          x,y,200,255u,&ring3.badge_node) ||
        !software_compositor_set_input_transparent(
          compositor,authority,display_control,ring3.badge_node,true) ||
        !software_compositor_node_in_active_scene(
          compositor,ring3.badge_node)) goto failed;
    /* The kernel's main PRE_SESSION surface has not yet committed its
     * first frame here. Until then there may legitimately be NO hit target.
     * It must NEVER be this passive Ring3 node. After main commit, the
     * compositor checks that the underlying trusted node wins instead. */
    uint64_t hit=0u;
    if (software_compositor_hit_test(compositor,x+5,y+5,&hit) &&
        hit==ring3.badge_node) goto failed;
    ring3.badge_commit=ack_serial;
    ring3.badge_epoch=ring3.instance_epoch;
    log_line("[identity-g2-ring3] Ring3 passive layout pixels committed, click-through verified");
    return true;
failed:
    identity_ring3_host_detach_badge();
    log_line("[identity-g2-ring3] Ring3 badge unavailable; kernel login remains active");
    return false;
}

bool identity_ring3_host_stop(void) {
    identity_ring3_host_detach_badge();
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
        identity_ring3_host_detach_badge();
        log_line("[identity-g2-ring3] process exited/faulted; trusted kernel login stays authoritative");
    }
}
bool identity_ring3_host_self_test(void) {
    /* Actual Ring3 process lifecycle: start, health, cooperative shutdown,
     * reap, restart; followed by a malformed IPC request that must terminate
     * the worker without leaking or reusing the old endpoint/epoch. */
    if (!identity_ring3_host_start() ||
        !identity_ring3_host_health_check() ||
        !identity_ring3_host_stop() ||
        !identity_ring3_host_start() ||
        !identity_ring3_host_health_check()) return false;

    const uint64_t abandoned_epoch=ring3.instance_epoch;
    if (!ring3.kernel_endpoint || !ring3.thread) return false;
    const struct aurora_identity_ring3_message rejected={
        .abi_version=AURORA_IDENTITY_RING3_ABI_VERSION,
        .type=UINT32_MAX, /* Neither credentials nor graphics are accepted. */
        .request_id=ring3.next_id++
    };
    if (rejected.request_id==0u ||
        !ipc_send(ring3.kernel_endpoint,&ring3.kernel_caps,
                  &rejected,sizeof(rejected),NULL,0u)) return false;

    const uint64_t deadline=clock_now_ns()+AURORA_IDENTITY_RING3_TIMEOUT_NS;
    while (!scheduler_thread_finished(ring3.thread) &&
           clock_now_ns()<deadline) arch_idle();

    if (!scheduler_thread_finished(ring3.thread) ||
        identity_ring3_host_alive() ||
        process_state(ring3.process)!=AURORA_PROCESS_EXITED ||
        ring3.process->exit_code!=20) return false;

    /* An untrusted host's demise cannot upgrade presentation authority.
     * The kernel's independent secure scene and Identity Service still
     * exclusively determine login and credential verification. */
    identity_ring3_host_poll();
    if (ring3.started || !ring3.fault_logged ||
        !identity_ring3_host_start() ||
        ring3.instance_epoch<=abandoned_epoch ||
        !identity_ring3_host_health_check()) return false;

    log_line("[identity-g2-ring3] rejected IPC crashed worker; fresh epoch recovery and health passed");
    return true;
}
