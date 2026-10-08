#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
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
#include <aurora/usercopy.h>

#define USER_SESSION_HOST_TIMEOUT_NS UINT64_C(2000000000)

struct user_session_host_runtime {
    struct aurora_process *process;
    aurora_thread_id thread;
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
    aurora_cap_handle g5_receiver_handle;
    aurora_cap_handle g5_authority_handle;
    aurora_cap_handle g5_sender_handle;
    bool g5_ready;
};

static struct user_session_host_runtime host;
static struct g5_dispatch_context *session_g5_dispatcher;

bool user_session_host_register_g5_dispatcher(struct g5_dispatch_context *d) {
    if (d == NULL || host.active || session_g5_dispatcher != NULL ||
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

static void cleanup_finished_host(void) {
    if (session_g5_dispatcher != NULL)
        g5_ipc_dispatch_revoke(session_g5_dispatcher);
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
    if (host.active ||
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
            AURORA_CAP_SYSTEM, AURORA_RIGHT_READ);
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
            .authority_rights=AURORA_RIGHT_READ,
            .provisioned_exclusively=true,
            .pending_requests=&host.g5_pending
        };
    }

    struct aurora_user_session_host_startup startup;
    clear_bytes(&startup, sizeof(startup));
    startup.abi_version = AURORA_USER_SESSION_HOST_ABI_VERSION;
    startup.control_endpoint = host.control_handle;
    startup.profile_handle = host.profile_handle;
    startup.g5_endpoint = host.g5_sender_handle == AURORA_CAP_INVALID ?
        0u : host.g5_sender_handle;
    startup.session_generation = generation;
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
            g5_ipc_dispatch_revoke(session_g5_dispatcher);
            (void)session_profile_lease_revoke_process(host.process);
            host.profile_handle=AURORA_CAP_INVALID;
            return false;
        }
        host.g5_ready=true;
    }
    host.active = true;
    return true;
}

bool user_session_host_stop(void) {
    /* Fail closed immediately, including IPC send timeout/failure paths. */
    if (session_g5_dispatcher != NULL)
        g5_ipc_dispatch_revoke(session_g5_dispatcher);
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

bool user_session_host_start(void) {
    if (session_manager_client_state() != AURORA_SESSION_CLIENT_ACTIVE) {
        return false;
    }

    return start_with_context(
        session_manager_client_user_id(),
        session_manager_client_generation());
}

bool user_session_host_active(void) {
    return host.active &&
        host.process != NULL &&
        host.thread != 0u &&
        process_state(host.process) == AURORA_PROCESS_RUNNING;
}

static uint32_t g5_session_ready_events;
static bool g5_session_test_authorize(void *ctx,uint32_t operation,uint64_t generation) {
    (void)ctx;
    return operation==G5_OP_SHELL_READY && generation==1u;
}
static bool g5_session_test_handle(void *ctx,const struct g5_ipc_header *header,
                                   const uint8_t *payload) {
    (void)ctx;(void)payload;
    if (header->operation!=G5_OP_SHELL_READY || header->session_generation!=1u)
        return false;
    ++g5_session_ready_events;
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
    g5_session_ready_events=0u;
    if (!user_session_host_register_g5_dispatcher(&g5_test_dispatcher)) {
        session_profile_lease_end();
        (void)cap_revoke(&bridge.capabilities,profile);
        (void)cap_revoke(&bridge.capabilities,root);
        return false;
    }
    bool started = start_with_context(user_id, UINT64_C(1));
    bool running = started && user_session_host_active() &&
        host.g5_ready && g5_session_ready_events==1u;
    bool stopped = running && user_session_host_stop();
    user_session_host_unregister_g5_dispatcher(&g5_test_dispatcher);

    session_profile_lease_end();
    bool source_revoked =
        cap_revoke(&bridge.capabilities, profile);
    bool root_revoked =
        cap_revoke(&bridge.capabilities, root);

    return started &&
        running &&
        stopped &&
        source_revoked &&
        root_revoked &&
        !user_session_host_active() &&
        !session_profile_lease_active();
}
