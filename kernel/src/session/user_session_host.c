#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/log.h>
#include <aurora/ipc.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/profile_session.h>
#include <aurora/session_manager_client.h>
#include <aurora/session_profile_lease.h>
#include <aurora/user_session_host.h>
#include <aurora/user_session_host_abi.h>
#include <aurora/user_session_host_image.h>
#include <aurora/g5_ipc_endpoint.h>
#include <aurora/g5_shell_session.h>
#include <aurora/g5_shell_scene.h>
#include <aurora/usercopy.h>

#define USER_SESSION_HOST_TIMEOUT_NS UINT64_C(2000000000)

struct user_session_host_runtime {
    struct aurora_process *process;
    aurora_thread_id thread;
    /* Second Ring3 process, exclusive control endpoint and graphics owner. */
    struct aurora_process *second_process;
    aurora_thread_id second_thread;
    struct aurora_ipc_channel second_channel;
    struct aurora_ipc_endpoint *second_kernel_endpoint;
    aurora_cap_handle second_control_handle;
    struct aurora_ipc_channel channel;
    struct aurora_cap_table kernel_caps;
    struct aurora_ipc_endpoint *kernel_endpoint;
    aurora_cap_handle control_handle;
    aurora_cap_handle profile_handle;
    uint64_t next_request_id;
    bool active;
    struct aurora_ipc_channel g5_channel;
    struct g5_ipc_endpoint_binding g5_binding;
    struct g5_pending_queue g5_pending;
    struct g5_shell_scene scene;
    uint64_t drag_window_id;
    int32_t drag_offset_x;
    int32_t drag_offset_y;
    aurora_cap_handle g5_receiver_handle;
    aurora_cap_handle g5_authority_handle;
    aurora_cap_handle g5_sender_handle;
    bool g5_ready;
};

static struct user_session_host_runtime host;
static struct g5_shell_session shell_session;
static struct g5_dispatch_context *session_g5_dispatcher;

bool user_session_host_register_g5_dispatcher(struct g5_dispatch_context *d) {
    if (d == NULL || host.active || host.process != NULL ||
        host.thread != 0u || session_g5_dispatcher != NULL ||
        d->active_session_generation != 0) return false;
    session_g5_dispatcher = d;
    return true;
}

void user_session_host_unregister_g5_dispatcher(struct g5_dispatch_context *d) {
    if (d == NULL || d != session_g5_dispatcher) return;
    g5_ipc_dispatch_revoke(d);
    session_g5_dispatcher = NULL;
}


static void clear_bytes(void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    if (buffer == NULL) return;
    for (size_t i = 0u; i < size; ++i) bytes[i] = 0u;
}

static bool bytes_equal(const void *left, const void *right, size_t size) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    if (left == NULL || right == NULL) return false;
    for (size_t i = 0u; i < size; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static bool receive_expected(
    uint32_t type,
    uint64_t request_id
) {
    uint64_t deadline = clock_now_ns() + USER_SESSION_HOST_TIMEOUT_NS;

    while (clock_now_ns() < deadline) {
        struct aurora_ipc_received received;
        clear_bytes(&received, sizeof(received));

        if (host.kernel_endpoint != NULL &&
            ipc_receive(
                host.kernel_endpoint,
                &host.kernel_caps,
                &received)) {
            const struct aurora_user_session_host_message expected = {
                .version = AURORA_USER_SESSION_HOST_PROTOCOL_VERSION,
                .type = type,
                .request_id = request_id
            };

            return received.capability_count == 0u &&
                received.length == sizeof(expected) &&
                bytes_equal(received.data, &expected, sizeof(expected));
        }

        if (host.thread != 0u &&
            scheduler_thread_finished(host.thread)) {
            return false;
        }

        arch_idle();
    }

    return false;
}

/* The secondary IPC endpoint is exclusively granted to a distinct Ring3
 * process. Never accept a frame receipt from the primary Shell channel. */
static bool receive_second_event(uint32_t type, uint64_t *out_request) {
    if (out_request) *out_request = 0u;
    if (host.second_kernel_endpoint == NULL || out_request == NULL)
        return false;
    uint64_t deadline = clock_now_ns() + USER_SESSION_HOST_TIMEOUT_NS;
    while (clock_now_ns() < deadline) {
        struct aurora_ipc_received received;
        clear_bytes(&received,sizeof(received));
        if (ipc_receive(host.second_kernel_endpoint,
                        &host.kernel_caps,&received)) {
            struct aurora_user_session_host_message expected = {
                .version = AURORA_USER_SESSION_HOST_PROTOCOL_VERSION,
                .type = type
            };
            if (received.capability_count != 0u ||
                received.length != sizeof(expected))
                return false;
            struct aurora_user_session_host_message incoming;
            clear_bytes(&incoming,sizeof(incoming));
            for (size_t i=0u;i<sizeof(incoming);++i)
                ((uint8_t *)&incoming)[i]=received.data[i];
            if (incoming.version != expected.version ||
                incoming.type != expected.type)
                return false;
            *out_request = incoming.request_id;
            return true;
        }
        if (host.second_thread != 0u &&
            scheduler_thread_finished(host.second_thread))
            return false;
        arch_idle();
    }
    return false;
}

static bool receive_primary_event(uint32_t type, uint64_t *out_request) {
    if (out_request) *out_request = 0u;
    if (host.kernel_endpoint == NULL || out_request == NULL)
        return false;
    uint64_t deadline = clock_now_ns() + USER_SESSION_HOST_TIMEOUT_NS;
    while (clock_now_ns() < deadline) {
        struct aurora_ipc_received received;
        clear_bytes(&received,sizeof(received));
        if (ipc_receive(host.kernel_endpoint,
                        &host.kernel_caps,&received)) {
            struct aurora_user_session_host_message expected = {
                .version = AURORA_USER_SESSION_HOST_PROTOCOL_VERSION,
                .type = type
            };
            if (received.capability_count != 0u ||
                received.length != sizeof(expected))
                return false;
            struct aurora_user_session_host_message incoming;
            clear_bytes(&incoming,sizeof(incoming));
            for (size_t i=0u;i<sizeof(incoming);++i)
                ((uint8_t *)&incoming)[i]=received.data[i];
            if (incoming.version != expected.version ||
                incoming.type != expected.type)
                return false;
            *out_request = incoming.request_id;
            return true;
        }
        if (host.thread != 0u &&
            scheduler_thread_finished(host.thread))
            return false;
        arch_idle();
    }
    return false;
}

static void revoke_g5_sender(void) {
    if (host.process != NULL &&
        host.g5_sender_handle != AURORA_CAP_INVALID) {
        (void)cap_revoke(&host.process->capabilities,host.g5_sender_handle);
        host.g5_sender_handle=AURORA_CAP_INVALID;
    }
}

static void revoke_g5_receiver(void) {
    g5_shell_scene_end(&host.scene);
    /* Remove receiver authority before releasing a session's kernel endpoint. */
    host.g5_binding.provisioned_exclusively=false;
    if (host.g5_authority_handle != AURORA_CAP_INVALID) {
        (void)cap_revoke(&host.kernel_caps,host.g5_authority_handle);
        host.g5_authority_handle=AURORA_CAP_INVALID;
    }
    if (host.g5_receiver_handle != AURORA_CAP_INVALID) {
        (void)cap_revoke(&host.kernel_caps,host.g5_receiver_handle);
        host.g5_receiver_handle=AURORA_CAP_INVALID;
    }
    host.g5_binding.receiver=NULL;
    host.g5_binding.receiver_caps=NULL;
    host.g5_binding.dispatch=NULL;
    host.g5_ready=false;
}

static bool shutdown_second_client(void) {
    if (host.second_process==NULL) return true;
    bool clean=true;
    if (host.second_thread!=0u &&
        !scheduler_thread_finished(host.second_thread)) {
        uint64_t id=host.next_request_id++;
        if (id==0u) id=host.next_request_id++;
        const struct aurora_user_session_host_message request={
            .version=AURORA_USER_SESSION_HOST_PROTOCOL_VERSION,
            .type=AURORA_USER_SESSION_HOST_SHUTDOWN,
            .request_id=id
        };
        uint64_t ack=0u;
        if (!host.second_kernel_endpoint ||
            !ipc_send(host.second_kernel_endpoint,&host.kernel_caps,
                      &request,(uint32_t)sizeof(request),NULL,0u) ||
            !receive_second_event(AURORA_USER_SESSION_HOST_SHUTDOWN_ACK,&ack) ||
            ack!=id)
            clean=false;
        uint64_t deadline=clock_now_ns()+USER_SESSION_HOST_TIMEOUT_NS;
        while (!scheduler_thread_finished(host.second_thread) &&
               clock_now_ns()<deadline)
            arch_idle();
    }
    if (host.second_thread!=0u &&
        scheduler_thread_finished(host.second_thread)) {
        (void)scheduler_reap_thread(host.second_thread);
        host.second_thread=0u;
    }
    if (host.second_control_handle!=AURORA_CAP_INVALID) {
        (void)cap_revoke(&host.second_process->capabilities,
                         host.second_control_handle);
        host.second_control_handle=AURORA_CAP_INVALID;
    }
    if (host.second_thread==0u &&
        process_live_thread_count(host.second_process)==0u) {
        if (process_state(host.second_process)==AURORA_PROCESS_RUNNING)
            process_mark_exited(host.second_process,clean?0:1);
        clean=clean &&
              process_state(host.second_process)==AURORA_PROCESS_EXITED &&
              host.second_process->exit_code==0;
        if (process_state(host.second_process)!=AURORA_PROCESS_RUNNING &&
            process_reap(host.second_process,NULL) &&
            process_release(host.second_process)) {
            host.second_process=NULL;
            host.second_kernel_endpoint=NULL;
        } else clean=false;
    } else clean=false;
    return clean;
}

static void cleanup_finished_host(void) {
    /* Scene teardown uses extra.owner->capabilities. Revoke graphics
     * while the second process object still exists; then reap it. */
    g5_shell_session_end(&shell_session);
    revoke_g5_receiver();
    (void)shutdown_second_client();
    if (session_g5_dispatcher != NULL)
        g5_ipc_dispatch_revoke(session_g5_dispatcher);
    revoke_g5_sender();
    if (host.process != NULL) {
        (void)session_profile_lease_revoke_process(host.process);
    }

    if (host.thread != 0u &&
        scheduler_thread_finished(host.thread)) {
        (void)scheduler_reap_thread(host.thread);
        host.thread = 0u;
    }

    if (host.process != NULL &&
        process_live_thread_count(host.process) == 0u &&
        process_state(host.process) != AURORA_PROCESS_RUNNING) {
        (void)process_reap(host.process, NULL);
        (void)process_release(host.process);
        host.process = NULL;
    }

    host.kernel_endpoint = NULL;
    host.control_handle = AURORA_CAP_INVALID;
    host.second_control_handle = AURORA_CAP_INVALID;
    host.profile_handle = AURORA_CAP_INVALID;
    host.active = false;
    host.g5_ready = false;
}

static void cleanup_unstarted_host(void) {
    if (host.process != NULL) {
        (void)session_profile_lease_revoke_process(host.process);
        if (process_state(host.process) == AURORA_PROCESS_RUNNING &&
            process_live_thread_count(host.process) == 0u) {
            process_mark_exited(host.process, 1);
        }
    }

    cleanup_finished_host();
}

/* Launch a real independent address space using the same small user
 * image in renderer mode. It has no primary Shell control/health authority,
 * no profile capability and no shared G5 sender endpoint. */
static bool start_second_client(
    const uint8_t user_id[AURORA_USER_SESSION_HOST_USER_ID_SIZE],
    uint64_t generation
) {
    if (host.second_process != NULL || host.second_thread != 0u ||
        !host.scene.active || !host.process || !generation || !user_id)
        return false;
    host.second_process=process_create_image(
        "g5-render-client-2",user_session_host_image(),
        user_session_host_image_size());
    if (!host.second_process) return false;
    ipc_channel_init(&host.second_channel);
    host.second_kernel_endpoint=ipc_channel_endpoint(&host.second_channel,0u);
    struct aurora_ipc_endpoint *user_endpoint=
        ipc_channel_endpoint(&host.second_channel,1u);
    if (!host.second_kernel_endpoint || !user_endpoint) return false;
    host.second_control_handle=cap_grant(
        &host.second_process->capabilities,user_endpoint,
        AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_READ|AURORA_RIGHT_WRITE);
    if (host.second_control_handle==AURORA_CAP_INVALID ||
        !g5_shell_scene_attach_second(&host.scene,host.second_process))
        return false;
    log_line("[g5-wp04-gate] primary graphics scene ready");
    struct aurora_user_session_host_startup startup;
    clear_bytes(&startup,sizeof(startup));
    startup.abi_version=AURORA_USER_SESSION_HOST_ABI_VERSION;
    startup.flags=AURORA_USER_SESSION_HOST_FLAG_RENDER_CLIENT;
    startup.control_endpoint=host.second_control_handle;
    startup.session_generation=generation;
    startup.graphics_buffer=host.scene.extra.user_buffer;
    startup.graphics_surface=host.scene.extra.user_surface;
    startup.graphics_object_id=host.scene.extra.surface->object_id;
    startup.graphics_object_generation=host.scene.extra.surface->generation;
    startup.graphics_width=host.scene.extra.width;
    startup.graphics_height=host.scene.extra.height;
    for (size_t i=0u;i<sizeof(startup.user_id);++i)
        startup.user_id[i]=user_id[i];
    if (sizeof(startup)!=AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET ||
        host.second_process->user_stack_top <
            AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET ||
        !copy_to_user(host.second_process,
            host.second_process->user_stack_top -
                AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET,
            &startup,sizeof(startup))) {
        clear_bytes(&startup,sizeof(startup));
        return false;
    }
    clear_bytes(&startup,sizeof(startup));
    log_line("[g5-wp04-gate] starting secondary Ring3 thread");
    host.second_thread=scheduler_create_user_thread(
        "g5-ring3-renderer-2",host.second_process);
    if (host.second_thread==0u) return false;
    uint64_t receipt=0u;
    log_line("[g5-wp04-gate] waiting for secondary READY");
    if (!receive_second_event(AURORA_USER_SESSION_HOST_READY,&receipt) ||
        receipt!=0u ||
        process_bootstrap_signal(host.second_process)!=
            AURORA_USER_SESSION_HOST_READY_MAGIC)
        return false;
    log_line("[g5-wp04-gate] waiting for secondary committed frame");
    if (!receive_second_event(
            AURORA_USER_SESSION_HOST_FRAME_COMMITTED,&receipt) ||
        receipt==0u || receipt>UINT64_C(0xffffffff))
        return false;
    /* Both independent surfaces share one presentation queue. Never
     * mint a client-local request ID larger than the host's next IDs:
     * later resize frames must remain globally monotonic. */
    uint64_t serial=0u;
    uint64_t submission_id=host.next_request_id++;
    if (submission_id==0u || submission_id==UINT64_MAX ||
        !g5_shell_scene_publish_second(
            &host.scene,host.second_process,
            submission_id,receipt,&serial) ||
        serial==0u)
        return false;
    log_line("[g5-wp04] second Ring3 process committed and displayed");
    return true;
}

static bool start_with_context(
    const uint8_t user_id[AURORA_USER_SESSION_HOST_USER_ID_SIZE],
    uint64_t generation
) {
    /* Reap a previously failed bootstrap only after its thread has exited. */
    if (!host.active && host.thread != 0u &&
        scheduler_thread_finished(host.thread))
        cleanup_finished_host();
    /* Never discard a live or unreaped process by clearing host state. */
    if (host.active || host.process != NULL || host.thread != 0u ||
        user_id == NULL ||
        generation == 0u ||
        !session_profile_lease_active()) {
        return false;
    }

    clear_bytes(&host, sizeof(host));
    host.control_handle = AURORA_CAP_INVALID;
    host.profile_handle = AURORA_CAP_INVALID;
    host.g5_receiver_handle = AURORA_CAP_INVALID;
    host.g5_authority_handle = AURORA_CAP_INVALID;
    host.g5_sender_handle = AURORA_CAP_INVALID;
    host.next_request_id = UINT64_C(0x5553455200000001);

    log_line("[g5-wp04-gate] creating primary Ring3 process");
    host.process = process_create_image(
        "user-session-host",
        user_session_host_image(),
        user_session_host_image_size()
    );
    if (host.process == NULL) return false;
    log_line("[g5-wp04-gate] primary process image allocated");

    ipc_channel_init(&host.channel);
    cap_table_init(&host.kernel_caps);

    host.kernel_endpoint = ipc_channel_endpoint(&host.channel, 0u);
    struct aurora_ipc_endpoint *user_endpoint =
        ipc_channel_endpoint(&host.channel, 1u);
    if (host.kernel_endpoint == NULL || user_endpoint == NULL) {
        cleanup_unstarted_host();
        return false;
    }

    host.control_handle = cap_grant(
        &host.process->capabilities,
        user_endpoint,
        AURORA_CAP_IPC_ENDPOINT,
        AURORA_RIGHT_READ | AURORA_RIGHT_WRITE
    );
    if (host.control_handle == AURORA_CAP_INVALID) {
        cleanup_unstarted_host();
        return false;
    }

    host.profile_handle = session_profile_lease_delegate(
        host.process,
        AURORA_RIGHT_READ |
        AURORA_RIGHT_WRITE |
        AURORA_RIGHT_ENUMERATE
    );
    if (host.profile_handle == AURORA_CAP_INVALID) {
        cleanup_unstarted_host();
        return false;
    }

    log_line("[g5-wp04-gate] primary control/profile capabilities delegated");
    /* Optional dedicated G5 channel: the only sender grant is held by this
     * authenticated session process. No TRANSFER and no sender READ right.
     * The receiver remains in the kernel-owned capability table. */
    if (session_g5_dispatcher != NULL) {
        ipc_channel_init(&host.g5_channel);
        struct aurora_ipc_endpoint *sender =
            ipc_channel_endpoint(&host.g5_channel, 0u);
        struct aurora_ipc_endpoint *receiver =
            ipc_channel_endpoint(&host.g5_channel, 1u);
        if (sender == NULL || receiver == NULL) {
            cleanup_unstarted_host();
            return false;
        }
        host.g5_sender_handle = cap_grant(
            &host.process->capabilities, sender,
            AURORA_CAP_IPC_ENDPOINT, AURORA_RIGHT_WRITE);
        host.g5_receiver_handle = cap_grant(
            &host.kernel_caps, receiver,
            AURORA_CAP_IPC_ENDPOINT, AURORA_RIGHT_READ);
        host.g5_authority_handle = cap_grant(
            &host.kernel_caps, &host.g5_channel,
            AURORA_CAP_SYSTEM,
            AURORA_RIGHT_READ|AURORA_RIGHT_CONTROL|AURORA_RIGHT_WRITE);
        if (host.g5_sender_handle == AURORA_CAP_INVALID ||
            host.g5_receiver_handle == AURORA_CAP_INVALID ||
            host.g5_authority_handle == AURORA_CAP_INVALID ||
            !g5_ipc_dispatch_bind_session(session_g5_dispatcher,generation)) {
            cleanup_unstarted_host();
            return false;
        }
        g5_pending_reset(&host.g5_pending,generation);
        host.g5_binding=(struct g5_ipc_endpoint_binding){
            .receiver=receiver,
            .receiver_endpoint_handle=host.g5_receiver_handle,
            .receiver_caps=&host.kernel_caps,
            .dispatch=session_g5_dispatcher,
            .receiver_authority=host.g5_authority_handle,
            .authority_type=AURORA_CAP_SYSTEM,
            .authority_rights=AURORA_RIGHT_READ|
                              AURORA_RIGHT_CONTROL|
                              AURORA_RIGHT_WRITE,
            .provisioned_exclusively=true,
            .pending_requests=&host.g5_pending
        };
        log_line("[g5-wp04-gate] starting primary graphics scene");
        if (!g5_shell_scene_begin(&host.scene,host.process,generation)) {
            log_line("[g5-shell-diagnostic] failed to initialize compositor scene");
            cleanup_unstarted_host();
            return false;
        }
    }

    struct aurora_user_session_host_startup startup;
    clear_bytes(&startup, sizeof(startup));
    startup.abi_version = AURORA_USER_SESSION_HOST_ABI_VERSION;
    startup.control_endpoint = host.control_handle;
    startup.profile_handle = host.profile_handle;
    startup.g5_endpoint = host.g5_sender_handle == AURORA_CAP_INVALID ?
        0u : host.g5_sender_handle;
    startup.session_generation = generation;
    startup.graphics_buffer=host.scene.user_buffer;
    startup.graphics_surface=host.scene.user_surface;
    startup.graphics_object_id=host.scene.surface ?
        host.scene.surface->object_id : 0u;
    startup.graphics_object_generation=host.scene.surface ?
        host.scene.surface->generation : 0u;
    startup.graphics_width=host.scene.active ? G5_SHELL_SCENE_WIDTH : 0u;
    startup.graphics_height=host.scene.active ? G5_SHELL_SCENE_HEIGHT : 0u;
    for (size_t i = 0u; i < sizeof(startup.user_id); ++i) {
        startup.user_id[i] = user_id[i];
    }

    if (sizeof(startup) != AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET ||
        host.process->user_stack_top <
            AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET ||
        !copy_to_user(
            host.process,
            host.process->user_stack_top -
                AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET,
            &startup,
            sizeof(startup))) {
        clear_bytes(&startup, sizeof(startup));
        cleanup_unstarted_host();
        return false;
    }
    clear_bytes(&startup, sizeof(startup));

    log_line("[g5-wp04-gate] launching primary Ring3 thread");
    host.thread = scheduler_create_user_thread(
        "user-session-host-main",
        host.process
    );
    if (host.thread == 0u) {
        cleanup_unstarted_host();
        return false;
    }

    if (!receive_expected(AURORA_USER_SESSION_HOST_READY, 0u) ||
        process_bootstrap_signal(host.process) !=
            AURORA_USER_SESSION_HOST_READY_MAGIC) {
        log_line("[g5-shell-diagnostic] Ring3 control READY or bootstrap proof missing");
        log_write("[g5-shell-diagnostic] host finished/exit-code/bootstrap-proof: ");
        log_u64(scheduler_thread_finished(host.thread));log_write("/");
        log_u64(host.process->exit_code);log_write("/");
        log_u64(process_bootstrap_signal(host.process));log_line("");
        /* A failed handshake must immediately invalidate both G5 ends,
         * not leave a privileged endpoint while a failed process winds down. */
        g5_shell_session_end(&shell_session);
        if (session_g5_dispatcher != NULL)
            g5_ipc_dispatch_revoke(session_g5_dispatcher);
        revoke_g5_sender();
        revoke_g5_receiver();
        if (scheduler_thread_finished(host.thread)) {
            cleanup_finished_host();
        } else {
            (void)session_profile_lease_revoke_process(host.process);
            host.profile_handle = AURORA_CAP_INVALID;
        }
        return false;
    }

    /* Startup generation comes only from the trusted Session Manager.
     * The Ring3 process must prove its possession of the exclusively
     * delegated endpoint by sending an authenticated framed READY. */
    if (session_g5_dispatcher != NULL) {
        enum g5_ipc_status ready=G5_IPC_DENIED;
        if (!g5_ipc_endpoint_poll(&host.g5_binding,&ready) ||
            ready!=G5_IPC_OK) {
            log_line("[g5-shell-diagnostic] first G5 READY rejected");
            g5_ipc_dispatch_revoke(session_g5_dispatcher);
            revoke_g5_sender();
            revoke_g5_receiver();
            (void)session_profile_lease_revoke_process(host.process);
            host.profile_handle=AURORA_CAP_INVALID;
            return false;
        }
        /* The Ring 3 principal must also complete a post-READY
         * authenticated health round-trip on its exclusive endpoint. */
        enum g5_ipc_status health=G5_IPC_DENIED;
        if (!g5_ipc_endpoint_poll(&host.g5_binding,&health) ||
            health!=G5_IPC_OK) {
            log_line("[g5-shell-diagnostic] second G5 HEALTH rejected");
            g5_ipc_dispatch_revoke(session_g5_dispatcher);
            revoke_g5_sender();
            revoke_g5_receiver();
            (void)session_profile_lease_revoke_process(host.process);
            host.profile_handle=AURORA_CAP_INVALID;
            return false;
        }
        /* The third message is a Ring3-written surface commit that must
         * traverse authenticated G5 dispatch and the actual display backend. */
        enum g5_ipc_status present=G5_IPC_DENIED;
        if (!g5_ipc_endpoint_poll(&host.g5_binding,&present) ||
            present!=G5_IPC_OK || host.scene.last_display_serial==0u) {
            log_line("[g5-shell-diagnostic] SCENE_PUBLISH rejected or no display serial");
            log_write("[g5-shell-diagnostic] publish IPC status/display serial: ");
            log_u64(present);log_write("/");
            log_u64(host.scene.last_display_serial);log_line("");
            g5_ipc_dispatch_revoke(session_g5_dispatcher);
            revoke_g5_sender();
            revoke_g5_receiver();
            (void)session_profile_lease_revoke_process(host.process);
            host.profile_handle=AURORA_CAP_INVALID;
            return false;
        }
        enum g5_ipc_status placed=G5_IPC_DENIED;
        if (!g5_ipc_endpoint_poll(&host.g5_binding,&placed) ||
            placed!=G5_IPC_OK || host.scene.x!=80 || host.scene.y!=72 ||
            host.scene.last_display_serial<2u) {
            log_line("[g5-shell-diagnostic] WINDOW_PLACE rejected or no display update");
            g5_ipc_dispatch_revoke(session_g5_dispatcher);
            revoke_g5_sender();
            revoke_g5_receiver();
            (void)session_profile_lease_revoke_process(host.process);
            host.profile_handle=AURORA_CAP_INVALID;
            return false;
        }
        host.g5_ready=true;
    }
    log_line("[g5-wp04-gate] primary ready, entering secondary Ring3 bootstrap");
    if (session_g5_dispatcher != NULL &&
        !start_second_client(user_id,generation)) {
        log_line("[g5-wp04] independent second Ring3 render client failed");
        cleanup_unstarted_host();
        return false;
    }
    if (!g5_shell_session_ready(&shell_session,generation)) {
        cleanup_unstarted_host();
        return false;
    }
    host.active = true;
    return true;
}

bool user_session_host_health_check(void) {
    if (!user_session_host_active() || !host.g5_ready ||
        session_g5_dispatcher == NULL || host.kernel_endpoint == NULL)
        return false;
    /* IDs 1 and 2 are reserved by Ring3 startup. All subsequent requests
     * consume a strictly increasing G5 and control ID in the same session. */
    uint64_t request_id=host.next_request_id++;
    if (request_id < 3u || request_id == UINT64_MAX)
        return false;
    struct aurora_user_session_host_message request={
        .version=AURORA_USER_SESSION_HOST_PROTOCOL_VERSION,
        .type=AURORA_USER_SESSION_HOST_HEALTH_POLL,
        .request_id=request_id
    };
    if (!ipc_send(host.kernel_endpoint,&host.kernel_caps,&request,
                  (uint32_t)sizeof(request),NULL,0u) ||
        !receive_expected(AURORA_USER_SESSION_HOST_HEALTH_ACK,request_id))
        return false;
    enum g5_ipc_status health=G5_IPC_DENIED;
    if (!g5_ipc_endpoint_poll(&host.g5_binding,&health) ||
        health!=G5_IPC_OK)
        return false;
    if (!host.scene.extra.active && host.second_process==NULL)
        return true; /* User closed the secondary window normally. */
    if (!host.scene.extra.active || !host.second_process ||
        !host.second_kernel_endpoint)
        return false;
    uint64_t second_id=host.next_request_id++;
    if (second_id==0u) second_id=host.next_request_id++;
    const struct aurora_user_session_host_message second_request={
        .version=AURORA_USER_SESSION_HOST_PROTOCOL_VERSION,
        .type=AURORA_USER_SESSION_HOST_HEALTH_POLL,
        .request_id=second_id
    };
    uint64_t second_ack=0u;
    return second_id!=0u &&
        ipc_send(host.second_kernel_endpoint,&host.kernel_caps,
                 &second_request,(uint32_t)sizeof(second_request),
                 NULL,0u) &&
        receive_second_event(AURORA_USER_SESSION_HOST_HEALTH_ACK,
                             &second_ack) &&
        second_ack==second_id;
}

bool user_session_host_stop(void) {
    /* Revoke graphics and capability bindings before releasing the
     * second process: scene teardown accesses its owner cap table. */
    g5_shell_session_end(&shell_session);
    if (session_g5_dispatcher != NULL)
        g5_ipc_dispatch_revoke(session_g5_dispatcher);
    revoke_g5_sender();
    revoke_g5_receiver();
    bool second_stopped=shutdown_second_client();
    if (!host.active ||
        host.process == NULL ||
        host.thread == 0u ||
        host.kernel_endpoint == NULL) {
        return false;
    }

    struct aurora_user_session_host_message request = {
        .version = AURORA_USER_SESSION_HOST_PROTOCOL_VERSION,
        .type = AURORA_USER_SESSION_HOST_SHUTDOWN,
        .request_id = host.next_request_id++
    };
    if (request.request_id == 0u) request.request_id = host.next_request_id++;

    if (!ipc_send(
            host.kernel_endpoint,
            &host.kernel_caps,
            &request,
            (uint32_t)sizeof(request),
            NULL,
            0u)) {
        (void)session_profile_lease_revoke_process(host.process);
        host.profile_handle = AURORA_CAP_INVALID;
        host.active = false;
        return false;
    }

    bool acknowledged = receive_expected(
        AURORA_USER_SESSION_HOST_SHUTDOWN_ACK,
        request.request_id);

    uint64_t deadline = clock_now_ns() + USER_SESSION_HOST_TIMEOUT_NS;
    while (!scheduler_thread_finished(host.thread) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }

    if (!scheduler_thread_finished(host.thread)) {
        (void)session_profile_lease_revoke_process(host.process);
        host.profile_handle = AURORA_CAP_INVALID;
        host.active = false;
        return false;
    }

    bool clean_exit =
        process_state(host.process) == AURORA_PROCESS_EXITED &&
        host.process->exit_code == 0;

    cleanup_finished_host();
    return acknowledged && clean_exit && second_stopped;
}

/* Production G5 receiver policy. The self-test registers its own dispatcher,
 * but a normal authenticated login must also provision the exclusive channel. */
static struct g5_dispatch_context production_g5_dispatcher;
static bool production_g5_registered;
/* The authenticated Session Manager identity may outlive a locked Shell.
 * Every Shell instance instead gets a fresh, monotonically increasing
 * receiver generation: never rebind a revoked G5 generation. */
static uint64_t production_manager_generation;
static uint64_t production_shell_generation;
static uint64_t production_g5_ready_count;
static uint64_t production_g5_health_count;
static uint64_t production_g5_present_count;
static uint64_t production_g5_place_count;

static bool production_g5_authorize(void *ctx,uint32_t operation,uint64_t generation) {
    (void)ctx;
    return generation!=0u &&
        generation==production_shell_generation &&
        production_manager_generation!=0u &&
        production_manager_generation==session_manager_client_generation() &&
        session_manager_client_state()==AURORA_SESSION_CLIENT_ACTIVE &&
        (operation==G5_OP_SHELL_READY || operation==G5_OP_SHELL_HEALTH ||
         ((operation==G5_OP_SCENE_PUBLISH ||
           operation==G5_OP_WINDOW_PLACE ||
           operation==G5_OP_WINDOW_CLOSE) && host.scene.active));
}

static bool production_g5_handle(void *ctx,const struct g5_ipc_header *header,
                                 const uint8_t *payload) {
    (void)ctx;(void)payload;
    if (header==NULL ||
        !production_g5_authorize(NULL,header->operation,
                                 header->session_generation))
        return false;
    if (header->operation==G5_OP_SHELL_READY) {
        ++production_g5_ready_count;
        return true;
    }
    if (header->operation==G5_OP_SHELL_HEALTH) {
        ++production_g5_health_count;
        return true;
    }
    if (header->operation==G5_OP_SCENE_PUBLISH &&
        g5_shell_scene_publish(&host.scene,header,payload)) {
        ++production_g5_present_count;
        return true;
    }
    if (header->operation==G5_OP_WINDOW_PLACE &&
        g5_shell_scene_place(&host.scene,header,payload)) {
        ++production_g5_place_count;
        return true;
    }
    if (header->operation==G5_OP_WINDOW_CLOSE)
        return g5_shell_scene_close(&host.scene,header,payload);
    return false;
}

bool user_session_host_start(void) {
    if (session_manager_client_state() != AURORA_SESSION_CLIENT_ACTIVE)
        return false;
    if (session_g5_dispatcher == NULL) {
        production_g5_dispatcher.authorize=production_g5_authorize;
        production_g5_dispatcher.handler=production_g5_handle;
        production_g5_dispatcher.context=NULL;
        if (!user_session_host_register_g5_dispatcher(&production_g5_dispatcher))
            return false;
        production_g5_registered=true;
    } else if (session_g5_dispatcher!=&production_g5_dispatcher) {
        /* Never replace an existing test or external service dispatcher. */
        return false;
    }
    uint64_t manager_generation=session_manager_client_generation();
    uint64_t latest=shell_session.last_ready_generation;
    if (production_g5_dispatcher.last_revoked_generation>latest)
        latest=production_g5_dispatcher.last_revoked_generation;
    if (production_shell_generation>latest)
        latest=production_shell_generation;
    if (manager_generation==0u || latest==UINT64_MAX)return false;
    production_manager_generation=manager_generation;
    production_shell_generation=latest+1u;
    production_g5_ready_count=0u;
    production_g5_health_count=0u;
    production_g5_present_count=0u;
    production_g5_place_count=0u;
    bool started=start_with_context(
        session_manager_client_user_id(),
        production_shell_generation);
    if (!started && production_g5_registered &&
        host.process==NULL && host.thread==0u) {
        user_session_host_unregister_g5_dispatcher(&production_g5_dispatcher);
        production_g5_registered=false;
    }
    if (started && (production_g5_ready_count!=1u ||
                    production_g5_health_count!=1u ||
                    production_g5_present_count!=1u ||
                    production_g5_place_count!=1u)) {
        (void)user_session_host_stop();
        return false;
    }
    return started;
}

bool user_session_host_active(void) {
    return host.active &&
        host.process != NULL &&
        host.thread != 0u &&
        process_state(host.process) == AURORA_PROCESS_RUNNING &&
        (!host.g5_ready ||
         (host.scene.extra.active &&
          host.second_process != NULL &&
          host.second_thread != 0u &&
          process_state(host.second_process)==AURORA_PROCESS_RUNNING) ||
         (!host.scene.extra.active && host.second_process==NULL &&
          host.second_thread==0u));
}

/* Deliver only dequeued, policy-authorized events through the corresponding
 * process's exclusive control IPC. No client can select the destination. */
static bool send_client_input(struct aurora_ipc_endpoint *endpoint,
                              const struct aurora_input_event *event) {
    if (!endpoint || !event) return false;
    struct aurora_user_session_host_input_message msg;
    clear_bytes(&msg,sizeof(msg));
    msg.header.version=AURORA_USER_SESSION_HOST_PROTOCOL_VERSION;
    msg.header.type=AURORA_USER_SESSION_HOST_INPUT_EVENT;
    msg.header.request_id=host.next_request_id++;
    if (msg.header.request_id==0u)
        msg.header.request_id=host.next_request_id++;
    if (msg.header.request_id==0u) return false;
    msg.event.type=event->type;
    msg.event.source=event->source;
    msg.event.device_id=event->device_id;
    msg.event.sequence=event->sequence;
    msg.event.synthetic=event->synthetic;
    msg.event.key=event->key;
    msg.event.button=event->button;
    msg.event.pressed=event->pressed;
    msg.event.delta_x=event->delta_x;
    msg.event.delta_y=event->delta_y;
    msg.event.absolute_x=event->absolute_x;
    msg.event.absolute_y=event->absolute_y;
    msg.event.scroll_x=event->scroll_x;
    msg.event.scroll_y=event->scroll_y;
    return ipc_send(endpoint,&host.kernel_caps,&msg,
                    (uint32_t)sizeof(msg),NULL,0u);
}

/* The primary Shell follows the same independent ACK/commit handshake as
 * the second process, but only its own private control endpoint is trusted. */
static bool resize_primary_client(uint32_t width,uint32_t height) {
    if (!user_session_host_active() || !host.process ||
        !host.kernel_endpoint || !host.scene.active)
        return false;
    uint64_t configure_serial=0u;
    aurora_cap_handle buffer=AURORA_CAP_INVALID;
    if (!g5_shell_scene_configure_primary(
            &host.scene,width,height,&configure_serial))
        return false;
    if (!g5_shell_scene_allocate_resize_buffer_primary(
            &host.scene,&buffer)) {
        g5_shell_scene_end(&host.scene);
        return false;
    }
    struct aurora_user_session_host_resize_message request;
    clear_bytes(&request,sizeof(request));
    request.header.version=AURORA_USER_SESSION_HOST_PROTOCOL_VERSION;
    request.header.type=AURORA_USER_SESSION_HOST_RESIZE_PREPARE;
    request.header.request_id=host.next_request_id++;
    if (request.header.request_id==0u)
        request.header.request_id=host.next_request_id++;
    request.graphics_buffer=buffer;
    request.configure_serial=configure_serial;
    request.width=width;
    request.height=height;
    uint64_t ack=0u,commit=0u;
    bool accepted=request.header.request_id!=0u &&
        ipc_send(host.kernel_endpoint,&host.kernel_caps,
                 &request,(uint32_t)sizeof(request),NULL,0u) &&
        receive_primary_event(AURORA_USER_SESSION_HOST_RESIZE_ACK,&ack) &&
        ack==configure_serial &&
        g5_shell_scene_ack_primary(&host.scene,ack) &&
        receive_primary_event(
            AURORA_USER_SESSION_HOST_FRAME_COMMITTED,&commit) &&
        commit!=0u &&
        g5_shell_scene_publish_primary_resized(
            &host.scene,request.header.request_id,commit);
    if (!accepted) {
        g5_shell_scene_end(&host.scene);
        log_line("[g5-wp04] primary client resize failed closed");
        return false;
    }
    log_line("[g5-wp04] primary Ring3 client resize ACK + frame presented");
    return true;
}

/* Complete the second client's resize transaction across real private
 * IPC. Both ACK and frame commit are proved by the Ring3 process itself. */
static bool resize_second_client(uint32_t width,uint32_t height) {
    if (!user_session_host_active() || !host.second_process ||
        !host.second_kernel_endpoint || !host.scene.extra.active)
        return false;
    uint64_t configure_serial=0u;
    aurora_cap_handle buffer=AURORA_CAP_INVALID;
    if (!g5_shell_scene_configure_second(
            &host.scene,host.second_process,width,height,&configure_serial))
        return false;
    if (!g5_shell_scene_allocate_resize_buffer_second(
            &host.scene,host.second_process,&buffer)) {
        g5_shell_scene_detach_second(&host.scene);
        return false;
    }
    struct aurora_user_session_host_resize_message request;
    clear_bytes(&request,sizeof(request));
    request.header.version=AURORA_USER_SESSION_HOST_PROTOCOL_VERSION;
    request.header.type=AURORA_USER_SESSION_HOST_RESIZE_PREPARE;
    request.header.request_id=host.next_request_id++;
    if (request.header.request_id==0u)
        request.header.request_id=host.next_request_id++;
    request.graphics_buffer=buffer;
    request.configure_serial=configure_serial;
    request.width=width;
    request.height=height;
    uint64_t ack=0u, commit=0u, display_serial=0u;
    bool accepted=request.header.request_id!=0u &&
        ipc_send(host.second_kernel_endpoint,&host.kernel_caps,
                 &request,(uint32_t)sizeof(request),NULL,0u) &&
        receive_second_event(AURORA_USER_SESSION_HOST_RESIZE_ACK,&ack) &&
        ack==configure_serial &&
        g5_shell_scene_ack_second(
            &host.scene,host.second_process,ack) &&
        receive_second_event(
            AURORA_USER_SESSION_HOST_FRAME_COMMITTED,&commit) &&
        commit!=0u &&
        g5_shell_scene_publish_second(
            &host.scene,host.second_process,
            request.header.request_id,commit,&display_serial) &&
        display_serial!=0u;
    if (!accepted) {
        /* The old and new configure states may no longer agree. Avoid
         * delivering further input to a half-resized client. */
        g5_shell_scene_detach_second(&host.scene);
        log_line("[g5-wp04] second client resize failed closed");
        return false;
    }
    log_line("[g5-wp04] second Ring3 client resize ACK + frame presented");
    return true;
}

bool user_session_host_route_input(const struct aurora_input_event *event) {
    if (!event || !user_session_host_active() || !host.scene.active ||
        !host.scene.input_router.initialized ||
        session_manager_client_state()!=AURORA_SESSION_CLIENT_ACTIVE)
        return false;
    if (!g5_shell_scene_route_input(&host.scene,event)) return false;
    struct aurora_graphics_input_router *r=&host.scene.input_router;
    if (event->type==AURORA_INPUT_EVENT_POINTER_BUTTON &&
        event->button==AURORA_POINTER_BUTTON_LEFT && !event->pressed) {
        if (host.drag_window_id) {
            (void)graphics_input_release_capture(
                r,host.drag_window_id);
            host.drag_window_id=0u;
        }
    }
    if (event->type==AURORA_INPUT_EVENT_POINTER_BUTTON &&
        event->button==AURORA_POINTER_BUTTON_LEFT && event->pressed &&
        !event->synthetic && event->sequence!=0u) {
        struct aurora_graphics_input_router *r=&host.scene.input_router;
        uint64_t clicked=0u, token=0u, focused=0u;
        if (window_policy_hit_test_committed(
                &host.scene.window_policy,r->pointer_x,r->pointer_y,&clicked) &&
            window_policy_issue_activation_token(
                &host.scene.window_policy,clicked,event->sequence,&token))
            (void)graphics_input_focus_pointer(
                r,token,event->sequence,&focused);
        if (focused!=0u) {
            struct aurora_window_toplevel w;
            if (window_policy_read_toplevel(
                    &host.scene.window_policy,focused,&w) &&
                graphics_input_request_capture(r,focused)) {
                host.drag_window_id=focused;
                host.drag_offset_x=r->pointer_x-w.placement.x;
                host.drag_offset_y=r->pointer_y-w.placement.y;
            }
        }
    }
    if (host.drag_window_id &&
        (event->type==AURORA_INPUT_EVENT_POINTER_ABSOLUTE ||
         event->type==AURORA_INPUT_EVENT_POINTER_RELATIVE)) {
        struct aurora_window_toplevel w;
        if (!window_policy_read_toplevel(&host.scene.window_policy,
                                         host.drag_window_id,&w) ||
            !window_policy_configure_ready(
                &host.scene.window_policy,host.drag_window_id,
                w.pending_configure.width,w.pending_configure.height)) {
            (void)graphics_input_release_capture(r,host.drag_window_id);
            host.drag_window_id=0u;
        } else {
            int64_t nx=(int64_t)r->pointer_x-host.drag_offset_x;
            int64_t ny=(int64_t)r->pointer_y-host.drag_offset_y;
            int64_t max_x=(int64_t)host.scene.window_policy.output_width-
                          w.pending_configure.width;
            int64_t max_y=(int64_t)host.scene.window_policy.output_height-
                          w.pending_configure.height;
            if (nx<0) nx=0;
            if (ny<0) ny=0;
            if (nx>max_x) nx=max_x;
            if (ny>max_y) ny=max_y;
            if (nx!=w.placement.x || ny!=w.placement.y) {
                bool moved=host.drag_window_id==host.scene.window_id
                    ? g5_shell_scene_move_primary(
                        &host.scene,(int32_t)nx,(int32_t)ny)
                    : host.scene.extra.active &&
                      host.drag_window_id==host.scene.extra.window_id &&
                      g5_shell_scene_move_second(
                        &host.scene,host.second_process,
                        (int32_t)nx,(int32_t)ny);
                if (!moved) {
                    (void)graphics_input_release_capture(
                        r,host.drag_window_id);
                    host.drag_window_id=0u;
                }
            }
        }
    }
    /* Middle click closes only the second toplevel. Shutdown its Ring3
     * process after capability revocation, then redraw the remaining Shell. */
    if (event->type==AURORA_INPUT_EVENT_POINTER_BUTTON &&
        event->button==AURORA_POINTER_BUTTON_MIDDLE && event->pressed &&
        !event->synthetic && host.scene.extra.active) {
        uint64_t hit=0u;
        if (window_policy_hit_test_committed(
                &host.scene.window_policy,r->pointer_x,
                r->pointer_y,&hit) &&
            hit==host.scene.extra.window_id) {
            if (host.drag_window_id==hit) {
                (void)graphics_input_release_capture(r,hit);
                host.drag_window_id=0u;
            }
            if (!g5_shell_scene_close_second(
                    &host.scene,host.second_process))
                return false;
            uint64_t display_serial=0u;
            bool painted=software_compositor_compose_present(
                &host.scene.compositor,&display_serial);
            bool stopped=shutdown_second_client();
            return painted && display_serial!=0u && stopped;
        }
    }
    /* Trusted demo interaction: right-click the second committed window to
     * toggle its negotiated size. Normal client input still routes only to
     * the owning process. */
    if (event->type==AURORA_INPUT_EVENT_POINTER_BUTTON &&
        event->button==AURORA_POINTER_BUTTON_RIGHT && event->pressed &&
        !event->synthetic) {
        uint64_t hit=0u;
        struct aurora_graphics_input_router *router=&host.scene.input_router;
        if (window_policy_hit_test_committed(
                &host.scene.window_policy,router->pointer_x,
                router->pointer_y,&hit) &&
            hit==host.scene.extra.window_id) {
            uint32_t new_width=host.scene.extra.width==G5_SHELL_SCENE_WIDTH
                ? 192u : G5_SHELL_SCENE_WIDTH;
            uint32_t new_height=host.scene.extra.height==G5_SHELL_SCENE_HEIGHT
                ? 120u : G5_SHELL_SCENE_HEIGHT;
            if (!resize_second_client(new_width,new_height))
                return false;
        } else if (hit==host.scene.window_id) {
            uint32_t new_width=host.scene.width==G5_SHELL_SCENE_WIDTH
                ? 192u : G5_SHELL_SCENE_WIDTH;
            uint32_t new_height=host.scene.height==G5_SHELL_SCENE_HEIGHT
                ? 120u : G5_SHELL_SCENE_HEIGHT;
            if (!resize_primary_client(new_width,new_height))
                return false;
        }
    }
    bool delivered=true;
    struct aurora_input_event queued;
    for (uint32_t i=0u;i<AURORA_GRAPHICS_INPUT_QUEUE_CAPACITY;++i) {
        if (!graphics_input_poll_target(&host.scene.input_router,
                                         host.scene.window_id,&queued))
            break;
        if (!send_client_input(host.kernel_endpoint,&queued))
            delivered=false;
    }
    for (uint32_t i=0u;
         host.scene.extra.active &&
         host.second_process!=NULL &&
         host.second_kernel_endpoint!=NULL &&
         i<AURORA_GRAPHICS_INPUT_QUEUE_CAPACITY;++i) {
        if (!g5_shell_scene_poll_input_second(
                 &host.scene,host.second_process,&queued))
            break;
        if (!send_client_input(host.second_kernel_endpoint,&queued))
            delivered=false;
    }
    return delivered;
}

static uint64_t g5_session_test_generation=1u;
static uint32_t g5_session_ready_events;
static uint32_t g5_session_health_events;
static uint32_t g5_session_present_events;
static uint32_t g5_session_place_events;
static bool g5_session_test_authorize(void *ctx,uint32_t operation,uint64_t generation) {
    (void)ctx;
    return (operation==G5_OP_SHELL_READY ||
            operation==G5_OP_SHELL_HEALTH ||
            operation==G5_OP_SCENE_PUBLISH ||
            operation==G5_OP_WINDOW_PLACE ||
            operation==G5_OP_WINDOW_CLOSE) &&
           generation==g5_session_test_generation;
}
static bool g5_session_test_handle(void *ctx,const struct g5_ipc_header *header,
                                   const uint8_t *payload) {
    (void)ctx;(void)payload;
    if (header->session_generation!=g5_session_test_generation) return false;
    if (header->operation==G5_OP_SHELL_READY) {
        ++g5_session_ready_events;
        return true;
    }
    if (header->operation==G5_OP_SHELL_HEALTH) {
        ++g5_session_health_events;
        return true;
    }
    if (header->operation==G5_OP_SCENE_PUBLISH &&
        g5_shell_scene_publish(&host.scene,header,payload)) {
        ++g5_session_present_events;
        return true;
    }
    if (header->operation==G5_OP_WINDOW_PLACE &&
        g5_shell_scene_place(&host.scene,header,payload)) {
        ++g5_session_place_events;
        return true;
    }
    if (header->operation==G5_OP_WINDOW_CLOSE)
        return g5_shell_scene_close(&host.scene,header,payload);
    return false;
}

/* Phase-end WP-04 interaction oracle. The surface was committed by
 * its own Ring3 process; the router never accepts a fabricated window ID. */
static bool g5_wp04_two_window_interaction_probe(void) {
    if (!host.scene.active || !host.scene.extra.active ||
        !host.second_process || !host.second_kernel_endpoint ||
        !host.scene.input_router.initialized)
        return false;
    log_line("[g5-wp04-interaction] begin hit-test/input/focus probe");
    struct aurora_window_toplevel w={0};
    if (!window_policy_read_toplevel(&host.scene.window_policy,
                                     host.scene.extra.window_id,&w) ||
        w.placement.x<0 || w.placement.y<0)
        return false;
    struct aurora_input_event pointer={0};
    pointer.type=AURORA_INPUT_EVENT_POINTER_ABSOLUTE;
    pointer.source=AURORA_INPUT_SOURCE_SYNTHETIC;
    pointer.synthetic=true;
    pointer.sequence=UINT64_C(1000);
    pointer.absolute_x=w.placement.x+8;
    pointer.absolute_y=w.placement.y+8;
    uint64_t hit=0u,token=0u,focused=0u;
    struct aurora_graphics_input_router *r=&host.scene.input_router;
    bool routed=graphics_input_route_event(r,&pointer);
    bool policy_hit=routed &&
        window_policy_hit_test_committed(
            &host.scene.window_policy,pointer.absolute_x,
            pointer.absolute_y,&hit);
    bool hit_second=policy_hit && hit==host.scene.extra.window_id;
    bool token_issued=hit_second &&
        window_policy_issue_activation_token(
            &host.scene.window_policy,hit,pointer.sequence,&token);
    bool focus_assigned=token_issued &&
        graphics_input_focus_pointer(
            r,token,pointer.sequence,&focused);
    if (!routed || !policy_hit || !hit_second || !token_issued ||
        !focus_assigned || focused!=host.scene.extra.window_id) {
        log_write("[g5-wp04-interaction] route/policy-hit/topmost/token/focus/window: ");
        log_u64(routed);log_write("/");
        log_u64(policy_hit);log_write("/");
        log_u64(hit_second);log_write("/");
        log_u64(token_issued);log_write("/");
        log_u64(focus_assigned);log_write("/");
        log_u64(hit);log_write("/");
        log_u64(focused);log_line("");
        return false;
    }
    log_line("[g5-wp04-interaction] trusted focus selected second window");
    struct aurora_input_event delivered={0};
    if (!g5_shell_scene_poll_input_second(
            &host.scene,host.second_process,&delivered) ||
        delivered.type!=AURORA_INPUT_EVENT_POINTER_ABSOLUTE ||
        graphics_input_poll_target(r,host.scene.window_id,&delivered))
        return false;
    log_line("[g5-wp04-interaction] pointer event dequeued only by second window");
    struct aurora_input_event key={0};
    key.type=AURORA_INPUT_EVENT_KEY;
    key.source=AURORA_INPUT_SOURCE_SYNTHETIC;
    key.synthetic=true;
    key.sequence=UINT64_C(1001);
    key.key=AURORA_KEY_A;
    key.pressed=true;
    if (!graphics_input_route_event(r,&key) ||
        !g5_shell_scene_poll_input_second(
            &host.scene,host.second_process,&delivered) ||
        delivered.type!=AURORA_INPUT_EVENT_KEY ||
        delivered.key!=AURORA_KEY_A ||
        graphics_input_poll_target(r,host.scene.window_id,&delivered))
        return false;
    log_line("[g5-wp04-interaction] keyboard event dequeued only by second window");
    /* Send actual client-owned input through the secondary's private IPC.
     * A following health ACK proves its Ring3 loop consumed the message. */
    if (!send_client_input(host.second_kernel_endpoint,&key))
        return false;
    uint64_t health_id=host.next_request_id++;
    const struct aurora_user_session_host_message health={
        .version=AURORA_USER_SESSION_HOST_PROTOCOL_VERSION,
        .type=AURORA_USER_SESSION_HOST_HEALTH_POLL,
        .request_id=health_id
    };
    uint64_t ack=0u;
    if (!health_id ||
        !ipc_send(host.second_kernel_endpoint,&host.kernel_caps,
                  &health,(uint32_t)sizeof(health),NULL,0u) ||
        !receive_second_event(AURORA_USER_SESSION_HOST_HEALTH_ACK,&ack) ||
        ack!=health_id)
        return false;
    log_line("[g5-wp04-interaction] private Ring3 input IPC acknowledged");
    int32_t moved_x=w.placement.x>0?w.placement.x-1:w.placement.x+1;
    int32_t moved_y=w.placement.y;
    if (!g5_shell_scene_move_second(
            &host.scene,host.second_process,moved_x,moved_y) ||
        !window_policy_read_toplevel(
            &host.scene.window_policy,host.scene.extra.window_id,&w) ||
        w.placement.x!=moved_x)
        return false;
    log_line("[g5-wp04-interaction] second window moved by trusted Shell");
    if (!g5_shell_scene_close_second(
            &host.scene,host.second_process))
        return false;
    uint64_t display_serial=0u;
    if (!software_compositor_compose_present(
            &host.scene.compositor,&display_serial) ||
        display_serial==0u || !shutdown_second_client() ||
        !host.scene.active || host.scene.extra.active ||
        !user_session_host_active())
        return false;
    log_line("[g5-wp04-interaction] secondary close and primary survival passed");
    return true;
}

bool user_session_host_self_test(void) {
    if (host.active || session_profile_lease_active()) return false;

    static struct aurora_process bridge;
    clear_bytes(&bridge, sizeof(bridge));
    cap_table_init(&bridge.capabilities);
    bridge.state = AURORA_PROCESS_RUNNING;

    const uint8_t user_id[AURORA_PROFILE_USER_ID_SIZE] = {
        0x51u, 0x52u, 0x53u, 0x54u,
        0x55u, 0x56u, 0x57u, 0x58u,
        0x61u, 0x62u, 0x63u, 0x64u,
        0x65u, 0x66u, 0x67u, 0x68u
    };

    aurora_cap_handle root = cap_grant(
        &bridge.capabilities,
        profile_root_authority(),
        AURORA_CAP_PROFILE_ROOT,
        AURORA_RIGHT_CONTROL
    );
    if (root == AURORA_CAP_INVALID) return false;

    aurora_cap_handle profile = profile_open_or_create(
        &bridge,
        root,
        user_id
    );
    if (profile == AURORA_CAP_INVALID) {
        (void)cap_revoke(&bridge.capabilities, root);
        return false;
    }

    if (!session_profile_lease_begin(
            &bridge.capabilities,
            profile,
            user_id,
            UINT64_C(1))) {
        (void)cap_revoke(&bridge.capabilities, profile);
        (void)cap_revoke(&bridge.capabilities, root);
        return false;
    }

    static struct g5_dispatch_context g5_test_dispatcher;
    clear_bytes(&g5_test_dispatcher,sizeof(g5_test_dispatcher));
    g5_test_dispatcher.authorize=g5_session_test_authorize;
    g5_test_dispatcher.handler=g5_session_test_handle;
    g5_session_test_generation=1u;
    g5_session_ready_events=0u;
    g5_session_health_events=0u;
    g5_session_present_events=0u;
    g5_session_place_events=0u;
    if (!user_session_host_register_g5_dispatcher(&g5_test_dispatcher)) {
        session_profile_lease_end();
        (void)cap_revoke(&bridge.capabilities,profile);
        (void)cap_revoke(&bridge.capabilities,root);
        return false;
    }
    log_line("[g5-wp04-gate] begin first-generation dual Ring3 bootstrap");
    bool started = start_with_context(user_id, UINT64_C(1));
    log_line("[g5-wp04-gate] first-generation bootstrap returned");
    bool running = started && user_session_host_active() &&
        host.g5_ready && g5_session_ready_events==1u &&
        g5_session_health_events==1u &&
        g5_session_present_events==1u &&
        g5_session_place_events==1u &&
        host.scene.x==80 && host.scene.y==72 &&
        host.scene.last_display_serial>=2u &&
        host.second_process!=NULL &&
        host.second_process!=host.process &&
        host.second_thread!=0u &&
        host.second_kernel_endpoint!=host.kernel_endpoint &&
        host.scene.extra.active &&
        host.scene.extra.owner==host.second_process &&
        host.scene.extra.surface!=host.scene.surface &&
        host.scene.extra.buffer!=host.scene.buffer &&
        host.scene.extra.last_commit_serial>0u &&
        host.scene.extra.last_display_serial>0u &&
        host.scene.extra.window_id!=host.scene.window_id &&
        host.scene.bridge.node_ids[host.scene.extra.slot]!=0u &&
        host.scene.bridge.node_ids[host.scene.extra.slot]!=
            host.scene.bridge.node_ids[host.scene.slot];
    /* A live session may not accept a duplicate or out-of-order G5 request. */
    bool replay_denied=false;
    if (running) {
        uint8_t wire[G5_IPC_WIRE_HEADER_BYTES]={0};
        size_t wire_len=0u;
        struct g5_ipc_header replay={
            .major=G5_IPC_WIRE_MAJOR,
            .minor=G5_IPC_WIRE_MINOR,
            .header_bytes=G5_IPC_WIRE_HEADER_BYTES,
            .kind=G5_IPC_REQUEST,
            .operation=G5_OP_SHELL_HEALTH,
            .request_id=2u,
            .session_generation=1u
        };
        enum g5_ipc_status replay_status=G5_IPC_OK;
        struct aurora_ipc_endpoint *sender=
            ipc_channel_endpoint(&host.g5_channel,0u);
        replay_denied=sender!=NULL &&
            g5_ipc_encode(&replay,NULL,wire,sizeof(wire),&wire_len)==G5_IPC_OK &&
            ipc_send(sender,NULL,wire,(uint32_t)wire_len,NULL,0u) &&
            g5_ipc_endpoint_poll(&host.g5_binding,&replay_status) &&
            replay_status==G5_IPC_DENIED &&
            g5_session_health_events==1u;
    }
    bool wrong_generation_denied=false;
    if (running && replay_denied) {
        uint8_t wire[G5_IPC_WIRE_HEADER_BYTES]={0};
        size_t wire_len=0u;
        struct g5_ipc_header wrong={
            .major=G5_IPC_WIRE_MAJOR,
            .minor=G5_IPC_WIRE_MINOR,
            .header_bytes=G5_IPC_WIRE_HEADER_BYTES,
            .kind=G5_IPC_REQUEST,
            .operation=G5_OP_SHELL_HEALTH,
            .request_id=3u,
            .session_generation=2u
        };
        enum g5_ipc_status status=G5_IPC_OK;
        struct aurora_ipc_endpoint *sender=
            ipc_channel_endpoint(&host.g5_channel,0u);
        wrong_generation_denied=sender!=NULL &&
            g5_ipc_encode(&wrong,NULL,wire,sizeof(wire),&wire_len)==G5_IPC_OK &&
            ipc_send(sender,NULL,wire,(uint32_t)wire_len,NULL,0u) &&
            g5_ipc_endpoint_poll(&host.g5_binding,&status) &&
            status==G5_IPC_DENIED &&
            g5_session_health_events==1u;
    }
    /* Even a permitted opcode cannot move a different object ID. */
    bool foreign_object_denied=false;
    if (running && wrong_generation_denied) {
        uint8_t wire[G5_IPC_WIRE_HEADER_BYTES+24u]={0};
        uint8_t args[24]={0};
        size_t wire_len=0u;
        args[0]=80u;args[4]=72u;args[8]=160u;args[12]=96u;
        uint64_t foreign=host.scene.surface->object_id+1u;
        for (unsigned i=0u;i<8u;++i)
            args[16u+i]=(uint8_t)(foreign>>(8u*i));
        struct g5_ipc_header header={
            .major=G5_IPC_WIRE_MAJOR,.minor=G5_IPC_WIRE_MINOR,
            .header_bytes=G5_IPC_WIRE_HEADER_BYTES,.kind=G5_IPC_REQUEST,
            .operation=G5_OP_WINDOW_PLACE,.payload_bytes=24u,
            .request_id=5u,.session_generation=1u,
            .object_generation=host.scene.surface->generation
        };
        enum g5_ipc_status status=G5_IPC_OK;
        struct aurora_ipc_endpoint *sender=
            ipc_channel_endpoint(&host.g5_channel,0u);
        uint64_t display_before=host.scene.last_display_serial;
        foreign_object_denied=sender!=NULL &&
            g5_ipc_encode(&header,args,wire,sizeof(wire),&wire_len)==G5_IPC_OK &&
            ipc_send(sender,NULL,wire,(uint32_t)wire_len,NULL,0u) &&
            g5_ipc_endpoint_poll(&host.g5_binding,&status) &&
            status==G5_IPC_DENIED && host.scene.x==80 &&
            host.scene.y==72 && host.scene.last_display_serial==display_before &&
            g5_session_place_events==1u;
    }
    bool close_opcode_denied=false;
    if (foreign_object_denied) {
        uint8_t wire[G5_IPC_WIRE_HEADER_BYTES+8u]={0},args[8]={0};
        size_t wire_len=0u;
        struct g5_ipc_header header={
            .major=G5_IPC_WIRE_MAJOR,.minor=G5_IPC_WIRE_MINOR,
            .header_bytes=G5_IPC_WIRE_HEADER_BYTES,.kind=G5_IPC_REQUEST,
            .operation=G5_OP_WINDOW_CLOSE,.payload_bytes=8u,
            .request_id=6u,.session_generation=1u,
            .object_generation=host.scene.surface->generation+1u
        };
        enum g5_ipc_status status=G5_IPC_OK;
        struct aurora_ipc_endpoint *sender=
            ipc_channel_endpoint(&host.g5_channel,0u);
        close_opcode_denied=sender!=NULL &&
            g5_ipc_encode(&header,args,wire,sizeof(wire),&wire_len)==G5_IPC_OK &&
            ipc_send(sender,NULL,wire,(uint32_t)wire_len,NULL,0u) &&
            g5_ipc_endpoint_poll(&host.g5_binding,&status) &&
            status==G5_IPC_DENIED && host.scene.active &&
            g5_session_place_events==1u && g5_session_present_events==1u;
    }
    /* Exercise the post-bootstrap Ring3 control loop and its kernel G5
     * dispatch, rather than just proving startup SHELL_READY/HEALTH. */
    bool live_health=running && replay_denied &&
        wrong_generation_denied && foreign_object_denied &&
        close_opcode_denied && user_session_host_health_check() &&
        g5_session_health_events==2u &&
        user_session_host_health_check() &&
        g5_session_health_events==3u;
    /* End-of-phase WP-04 native acceptance: the second *process* must
     * ACK two real configures and commit/present both replacement buffers. */
    uint64_t original_primary_serial=host.scene.last_display_serial;
    bool resized_primary=live_health &&
        resize_primary_client(128u,80u) &&
        host.scene.width==128u &&
        host.scene.height==80u &&
        host.scene.last_display_serial>original_primary_serial &&
        host.scene.pending_resize_buffer==NULL &&
        resize_primary_client(G5_SHELL_SCENE_WIDTH,G5_SHELL_SCENE_HEIGHT) &&
        host.scene.width==G5_SHELL_SCENE_WIDTH &&
        host.scene.height==G5_SHELL_SCENE_HEIGHT &&
        host.scene.pending_resize_buffer==NULL &&
        host.scene.last_commit_serial>=3u;
    uint64_t original_second_serial=host.scene.extra.last_display_serial;
    bool resized_roundtrip=resized_primary &&
        resize_second_client(128u,80u) &&
        host.scene.extra.width==128u &&
        host.scene.extra.height==80u &&
        host.scene.extra.last_display_serial>original_second_serial &&
        host.scene.extra.pending_resize_buffer==NULL &&
        resize_second_client(G5_SHELL_SCENE_WIDTH,G5_SHELL_SCENE_HEIGHT) &&
        host.scene.extra.width==G5_SHELL_SCENE_WIDTH &&
        host.scene.extra.height==G5_SHELL_SCENE_HEIGHT &&
        host.scene.extra.pending_resize_buffer==NULL &&
        host.scene.extra.last_commit_serial>=3u &&
        host.scene.extra.last_display_serial>original_second_serial;
    bool interaction_verified=resized_roundtrip &&
        g5_wp04_two_window_interaction_probe();
    bool stopped = user_session_host_active() && user_session_host_stop();
    bool post_stop_denied=stopped && !user_session_host_health_check() &&
        g5_session_health_events==3u &&
        g5_session_present_events==1u &&
        g5_session_place_events==1u &&
        !host.scene.active && host.scene.last_display_serial==0u;
    bool receiver_revoked = stopped &&
        host.g5_receiver_handle == AURORA_CAP_INVALID &&
        host.g5_authority_handle == AURORA_CAP_INVALID &&
        !host.g5_binding.provisioned_exclusively &&
        host.g5_binding.receiver == NULL &&
        !host.g5_ready;
    /* WP-03 crash/reauth acceptance: a second trusted generation must
     * bootstrap with new endpoints; deliberately terminate its Ring3
     * control loop without SHUTDOWN to prove crash cleanup fails closed. */
    bool reauthenticated=false, crashed=false, crash_revoked=false;
    if (post_stop_denied && receiver_revoked) {
        session_profile_lease_end();
        if (session_profile_lease_begin(
                &bridge.capabilities,profile,user_id,UINT64_C(2))) {
            g5_session_test_generation=2u;
            g5_session_ready_events=0u;
            g5_session_health_events=0u;
            g5_session_present_events=0u;
            g5_session_place_events=0u;
            reauthenticated=start_with_context(user_id,UINT64_C(2)) &&
                user_session_host_active() && host.g5_ready &&
                g5_session_ready_events==1u &&
                g5_session_health_events==1u &&
                g5_session_present_events==1u &&
                g5_session_place_events==1u &&
                host.scene.active && host.scene.last_display_serial>=2u &&
                user_session_host_health_check() &&
                g5_session_health_events==2u;
            if (reauthenticated) {
                struct aurora_user_session_host_message malformed={
                    .version=AURORA_USER_SESSION_HOST_PROTOCOL_VERSION,
                    .type=AURORA_USER_SESSION_HOST_HEALTH_POLL,
                    .request_id=0u
                };
                if (ipc_send(host.kernel_endpoint,&host.kernel_caps,
                             &malformed,(uint32_t)sizeof(malformed),NULL,0u)) {
                    uint64_t deadline=clock_now_ns()+USER_SESSION_HOST_TIMEOUT_NS;
                    while (!scheduler_thread_finished(host.thread) &&
                           clock_now_ns()<deadline) arch_idle();
                    crashed=scheduler_thread_finished(host.thread) &&
                        process_state(host.process)==AURORA_PROCESS_EXITED &&
                        host.process->exit_code!=0;
                }
                if (crashed) {
                    cleanup_finished_host();
                    crash_revoked=!host.active && !host.g5_ready &&
                        host.process==NULL && host.thread==0u &&
                        !host.scene.active &&
                        host.g5_sender_handle==AURORA_CAP_INVALID &&
                        host.g5_receiver_handle==AURORA_CAP_INVALID &&
                        host.g5_authority_handle==AURORA_CAP_INVALID &&
                        !host.g5_binding.provisioned_exclusively &&
                        g5_test_dispatcher.active_session_generation==0u;
                }
            }
        }
    }
    user_session_host_unregister_g5_dispatcher(&g5_test_dispatcher);

    session_profile_lease_end();
    bool source_revoked =
        cap_revoke(&bridge.capabilities, profile);
    bool root_revoked =
        cap_revoke(&bridge.capabilities, root);

    bool accepted=started &&
        running && stopped && live_health &&
        resized_roundtrip && interaction_verified &&
        post_stop_denied && receiver_revoked &&
        reauthenticated && crashed && crash_revoked &&
        source_revoked && root_revoked &&
        !user_session_host_active() &&
        !session_profile_lease_active();
    if (accepted) {
        log_line("[g5-wp03] Ring3 Shell crash and reauthentication lifecycle gate passed");
        log_line("[g5-wp04] two independent Ring3 clients and two-client resize roundtrips passed");
    }
    if (!accepted) {
        log_write("[g5-shell-diagnostic] self-test stages started/running/replay/gen/object/close/live/stop/revoke: ");
        log_u64(started);log_write("/");
        log_u64(running);log_write("/");
        log_u64(replay_denied);log_write("/");
        log_u64(wrong_generation_denied);log_write("/");
        log_u64(foreign_object_denied);log_write("/");
        log_u64(close_opcode_denied);log_write("/");
        log_u64(resized_roundtrip);log_write("/");
        log_u64(interaction_verified);log_write("/");
        log_u64(live_health);log_write("/");
        log_u64(stopped);log_write("/");
        log_u64(receiver_revoked);
        log_write("[g5-shell-diagnostic] reauthenticated/crashed/crash-revoked: ");
        log_u64(reauthenticated);log_write("/");
        log_u64(crashed);log_write("/");
        log_u64(crash_revoked);log_line("");
    }
    return accepted;
}
