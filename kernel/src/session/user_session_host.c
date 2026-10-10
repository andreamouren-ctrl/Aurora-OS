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
            copy_bytes(&incoming,received.data,sizeof(incoming));
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

static void cleanup_finished_host(void) {
    g5_shell_session_end(&shell_session);
    revoke_g5_receiver();
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

    host.process = process_create_image(
        "user-session-host",
        user_session_host_image(),
        user_session_host_image_size()
    );
    if (host.process == NULL) return false;

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
    return g5_ipc_endpoint_poll(&host.g5_binding,&health) &&
           health==G5_IPC_OK;
}

bool user_session_host_stop(void) {
    /* Fail closed immediately, including IPC send timeout/failure paths. */
    g5_shell_session_end(&shell_session);
    if (session_g5_dispatcher != NULL)
        g5_ipc_dispatch_revoke(session_g5_dispatcher);
    revoke_g5_sender();
    revoke_g5_receiver();
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
    return acknowledged && clean_exit;
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
        process_state(host.process) == AURORA_PROCESS_RUNNING;
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
    bool started = start_with_context(user_id, UINT64_C(1));
    bool running = started && user_session_host_active() &&
        host.g5_ready && g5_session_ready_events==1u &&
        g5_session_health_events==1u &&
        g5_session_present_events==1u &&
        g5_session_place_events==1u &&
        host.scene.x==80 && host.scene.y==72 &&
        host.scene.last_display_serial>=2u;
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
            .object_generation=host.scene.surface->generation
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
    bool stopped = live_health && user_session_host_stop();
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
        post_stop_denied && receiver_revoked &&
        reauthenticated && crashed && crash_revoked &&
        source_revoked && root_revoked &&
        !user_session_host_active() &&
        !session_profile_lease_active();
    if (accepted) {
        log_line("[g5-wp03] Ring3 Shell crash and reauthentication lifecycle gate passed");
    }
    if (!accepted) {
        log_write("[g5-shell-diagnostic] self-test stages started/running/replay/gen/object/close/live/stop/revoke: ");
        log_u64(started);log_write("/");
        log_u64(running);log_write("/");
        log_u64(replay_denied);log_write("/");
        log_u64(wrong_generation_denied);log_write("/");
        log_u64(foreign_object_denied);log_write("/");
        log_u64(close_opcode_denied);log_write("/");
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
