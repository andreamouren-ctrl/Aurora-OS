#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/identity_service_probe.h>
#include <aurora/ipc.h>
#include <aurora/profile_session.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/service_bootstrap.h>
#include <aurora/session_manager_probe.h>
#include <aurora/session_manager_protocol.h>
#include <aurora/session_manager_service.h>
#include <aurora/session_profile_lease.h>

#define SESSION_MANAGER_PROBE_TIMEOUT_NS 2000000000ull

static uint64_t session_manager_probe_authority;

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

static bool all_zero(const uint8_t *bytes, size_t size) {
    if (bytes == NULL) return false;
    for (size_t i = 0u; i < size; ++i) {
        if (bytes[i] != 0u) return false;
    }
    return true;
}

static bool wait_receive(
    struct aurora_trusted_service *service,
    struct aurora_ipc_received *out
) {
    if (service == NULL || out == NULL) return false;

    uint64_t deadline = clock_now_ns() + SESSION_MANAGER_PROBE_TIMEOUT_NS;
    while (clock_now_ns() < deadline) {
        if (service_bootstrap_receive(service, out)) return true;
        if (service->thread != 0u && scheduler_thread_finished(service->thread)) {
            return false;
        }
        arch_idle();
    }

    return false;
}

static bool wait_message(
    struct aurora_trusted_service *service,
    uint32_t type,
    uint64_t request_id
) {
    struct aurora_ipc_received received;
    clear_bytes(&received, sizeof(received));
    if (!wait_receive(service, &received) ||
        received.length != sizeof(struct aurora_session_manager_message) ||
        received.capability_count != 0u) {
        return false;
    }

    const struct aurora_session_manager_message expected = {
        .version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION,
        .type = type,
        .request_id = request_id
    };

    return bytes_equal(received.data, &expected, sizeof(expected));
}

static bool wait_identity_message(
    struct aurora_trusted_service *service,
    uint32_t type,
    uint64_t request_id
) {
    struct aurora_ipc_received received;
    clear_bytes(&received, sizeof(received));
    if (!wait_receive(service, &received) ||
        received.length != sizeof(struct aurora_identity_service_message) ||
        received.capability_count != 0u) {
        return false;
    }

    const struct aurora_identity_service_message expected = {
        .version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION,
        .type = type,
        .request_id = request_id
    };

    return bytes_equal(received.data, &expected, sizeof(expected));
}

static bool send_manager_message(
    struct aurora_trusted_service *service,
    uint32_t type,
    uint64_t request_id
) {
    const struct aurora_session_manager_message message = {
        .version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION,
        .type = type,
        .request_id = request_id
    };

    return service_bootstrap_send(service, &message, sizeof(message));
}

static bool send_identity_message(
    struct aurora_trusted_service *service,
    uint32_t type,
    uint64_t request_id
) {
    const struct aurora_identity_service_message message = {
        .version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION,
        .type = type,
        .request_id = request_id
    };

    return service_bootstrap_send(service, &message, sizeof(message));
}

static bool wait_finished(struct aurora_trusted_service *service) {
    if (service == NULL || service->thread == 0u) return false;

    uint64_t deadline = clock_now_ns() + SESSION_MANAGER_PROBE_TIMEOUT_NS;
    while (!scheduler_thread_finished(service->thread) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }

    return scheduler_thread_finished(service->thread);
}

static bool reap_service(struct aurora_trusted_service *service) {
    if (service == NULL || service->process == NULL || service->thread == 0u ||
        !scheduler_thread_finished(service->thread) ||
        process_state(service->process) != AURORA_PROCESS_EXITED ||
        service->process->exit_code != 0) {
        return false;
    }

    struct aurora_process *process = service->process;
    aurora_thread_id thread = service->thread;
    if (!scheduler_reap_thread(thread) ||
        process_live_thread_count(process) != 0u ||
        !process_reap(process, NULL) ||
        !process_release(process)) {
        return false;
    }

    clear_bytes(service, sizeof(*service));
    return true;
}


static bool wait_fake_identity_request(
    struct aurora_ipc_endpoint *endpoint,
    struct aurora_cap_table *caps,
    struct aurora_ipc_received *out
) {
    if (endpoint == NULL || caps == NULL || out == NULL) return false;
    uint64_t deadline = clock_now_ns() + SESSION_MANAGER_PROBE_TIMEOUT_NS;
    while (clock_now_ns() < deadline) {
        clear_bytes(out, sizeof(*out));
        if (ipc_receive(endpoint, caps, out)) return true;
        arch_idle();
    }
    return false;
}

static bool session_manager_profile_binding_self_test(void) {
    static struct aurora_trusted_service session;
    static struct aurora_service_bootstrap_capability dependencies[3];
    static struct aurora_ipc_channel identity_channel;
    static struct aurora_cap_table identity_caps;

    clear_bytes(&session, sizeof(session));
    clear_bytes(dependencies, sizeof(dependencies));
    ipc_channel_init(&identity_channel);
    cap_table_init(&identity_caps);

    struct aurora_ipc_endpoint *identity_service_endpoint =
        ipc_channel_endpoint(&identity_channel, 1u);
    struct aurora_ipc_endpoint *identity_probe_endpoint =
        ipc_channel_endpoint(&identity_channel, 0u);
    if (identity_service_endpoint == NULL || identity_probe_endpoint == NULL) {
        return false;
    }

    session_manager_probe_authority ^= UINT64_C(0x50524F46494C4501);

    dependencies[0].object = identity_service_endpoint;
    dependencies[0].type = AURORA_CAP_IPC_ENDPOINT;
    dependencies[0].rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE;

    dependencies[1].object = &session_manager_probe_authority;
    dependencies[1].type = AURORA_CAP_IDENTITY_SESSION;
    dependencies[1].rights =
        AURORA_RIGHT_CONTROL | AURORA_RIGHT_TRANSFER;

    dependencies[2].object = profile_root_authority();
    dependencies[2].type = AURORA_CAP_PROFILE_ROOT;
    dependencies[2].rights = AURORA_RIGHT_CONTROL;

    const struct aurora_trusted_service_manifest manifest = {
        .name = "session-manager-profile-probe",
        .image = session_manager_service_image(),
        .image_size = session_manager_service_image_size(),
        .protected_state_scope = "session-manager-profile-probe",
        .protected_state_rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
        .grant_entropy_seed = false,
        .extra_capabilities = dependencies,
        .extra_capability_count = 3u
    };

    if (!service_bootstrap_start_trusted(&manifest, &session) ||
        !wait_message(&session, AURORA_SESSION_MANAGER_READY, 0u)) {
        return false;
    }

    const uint64_t request_id = UINT64_C(0x50524F46494C4502);
    struct aurora_session_manager_begin_session request;
    clear_bytes(&request, sizeof(request));
    request.header.version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION;
    request.header.type = AURORA_SESSION_MANAGER_BEGIN_SESSION;
    request.header.request_id = request_id;
    for (size_t i = 0u; i < sizeof(request.session_grant); ++i) {
        request.session_grant[i] = (uint8_t)(0xA0u + (uint8_t)i);
    }

    if (!service_bootstrap_send(&session, &request, sizeof(request))) {
        clear_bytes(&request, sizeof(request));
        return false;
    }
    clear_bytes(&request, sizeof(request));

    struct aurora_ipc_received identity_received;
    clear_bytes(&identity_received, sizeof(identity_received));
    if (!wait_fake_identity_request(
            identity_probe_endpoint,
            &identity_caps,
            &identity_received) ||
        identity_received.length !=
            sizeof(struct aurora_identity_service_consume_session_grant) ||
        identity_received.capability_count != 1u) {
        return false;
    }

    struct aurora_identity_service_consume_session_grant consume;
    clear_bytes(&consume, sizeof(consume));
    for (size_t i = 0u; i < sizeof(consume); ++i) {
        ((uint8_t *)&consume)[i] = identity_received.data[i];
    }

    aurora_cap_handle session_authority =
        identity_received.capabilities[0];
    struct aurora_capability_view authority_view;
    if (consume.header.version != AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION ||
        consume.header.type != AURORA_IDENTITY_SERVICE_CONSUME_SESSION_GRANT ||
        consume.header.request_id != request_id ||
        !cap_lookup(
            &identity_caps,
            session_authority,
            AURORA_CAP_IDENTITY_SESSION,
            AURORA_RIGHT_CONTROL,
            &authority_view) ||
        cap_lookup(
            &identity_caps,
            session_authority,
            AURORA_CAP_IDENTITY_SESSION,
            AURORA_RIGHT_TRANSFER,
            &authority_view) ||
        !cap_revoke(&identity_caps, session_authority)) {
        clear_bytes(&consume, sizeof(consume));
        return false;
    }
    clear_bytes(&consume, sizeof(consume));
    clear_bytes(&identity_received, sizeof(identity_received));

    static const uint8_t expected_user_id[AURORA_SESSION_MANAGER_USER_ID_SIZE] = {
        0x12u,0x34u,0x56u,0x78u,0x9Au,0xBCu,0xDEu,0xF0u,
        0x11u,0x22u,0x33u,0x44u,0x55u,0x66u,0x77u,0x88u
    };

    struct aurora_identity_service_session_grant_result identity_result;
    clear_bytes(&identity_result, sizeof(identity_result));
    identity_result.header.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    identity_result.header.type =
        AURORA_IDENTITY_SERVICE_SESSION_GRANT_RESULT;
    identity_result.header.request_id = request_id;
    identity_result.state =
        AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SUCCESS;
    identity_result.public_error =
        AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_NONE;
    for (size_t i = 0u; i < sizeof(expected_user_id); ++i) {
        identity_result.user_id[i] = expected_user_id[i];
    }

    if (!ipc_send(
            identity_probe_endpoint,
            &identity_caps,
            &identity_result,
            (uint32_t)sizeof(identity_result),
            NULL,
            0u)) {
        clear_bytes(&identity_result, sizeof(identity_result));
        return false;
    }
    clear_bytes(&identity_result, sizeof(identity_result));

    struct aurora_ipc_received session_received;
    clear_bytes(&session_received, sizeof(session_received));
    if (!wait_receive(&session, &session_received) ||
        session_received.length != sizeof(struct aurora_session_manager_result) ||
        session_received.capability_count != 1u) {
        return false;
    }

    struct aurora_session_manager_result session_result;
    clear_bytes(&session_result, sizeof(session_result));
    for (size_t i = 0u; i < sizeof(session_result); ++i) {
        ((uint8_t *)&session_result)[i] = session_received.data[i];
    }

    aurora_cap_handle profile_handle = session_received.capabilities[0];
    if (session_result.header.version != AURORA_SESSION_MANAGER_PROTOCOL_VERSION ||
        session_result.header.type != AURORA_SESSION_MANAGER_SESSION_RESULT ||
        session_result.header.request_id != request_id ||
        session_result.state != AURORA_SESSION_MANAGER_STATE_ACTIVE ||
        session_result.public_error != AURORA_SESSION_MANAGER_ERROR_NONE ||
        session_result.session_generation == 0u ||
        !bytes_equal(
            session_result.user_id,
            expected_user_id,
            sizeof(expected_user_id)) ||
        !profile_capability_matches_user(
            &session.supervisor_caps,
            profile_handle,
            expected_user_id)) {
        clear_bytes(&session_received, sizeof(session_received));
        clear_bytes(&session_result, sizeof(session_result));
        return false;
    }

    uint64_t active_session_generation =
        session_result.session_generation;

    struct aurora_capability_view profile_view;
    if (!cap_lookup(
            &session.supervisor_caps,
            profile_handle,
            AURORA_CAP_FILE,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_ENUMERATE |
            AURORA_RIGHT_TRANSFER,
            &profile_view) ||
        cap_lookup(
            &session.supervisor_caps,
            profile_handle,
            AURORA_CAP_FILE,
            AURORA_RIGHT_CONTROL,
            &profile_view)) {
        return false;
    }

    static struct aurora_process desktop_process;
    clear_bytes(&desktop_process, sizeof(desktop_process));
    cap_table_init(&desktop_process.capabilities);
    desktop_process.state = AURORA_PROCESS_RUNNING;

    if (!session_profile_lease_begin(
            &session.supervisor_caps,
            profile_handle,
            expected_user_id,
            session_result.session_generation)) {
        return false;
    }

    aurora_cap_handle desktop_profile =
        session_profile_lease_delegate(
            &desktop_process,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_ENUMERATE);

    if (desktop_profile == AURORA_CAP_INVALID ||
        session_profile_lease_count() != 1u ||
        !profile_capability_matches_user(
            &desktop_process.capabilities,
            desktop_profile,
            expected_user_id) ||
        !cap_lookup(
            &desktop_process.capabilities,
            desktop_profile,
            AURORA_CAP_FILE,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_ENUMERATE,
            &profile_view) ||
        cap_lookup(
            &desktop_process.capabilities,
            desktop_profile,
            AURORA_CAP_FILE,
            AURORA_RIGHT_TRANSFER,
            &profile_view) ||
        cap_lookup(
            &desktop_process.capabilities,
            desktop_profile,
            AURORA_CAP_FILE,
            AURORA_RIGHT_CONTROL,
            &profile_view) ||
        session_profile_lease_delegate(
            &desktop_process,
            AURORA_RIGHT_READ) != AURORA_CAP_INVALID) {
        session_profile_lease_end();
        return false;
    }

    session_profile_lease_suspend();

    if (!session_profile_lease_active() ||
        session_profile_lease_count() != 0u ||
        cap_lookup(
            &desktop_process.capabilities,
            desktop_profile,
            AURORA_CAP_FILE,
            0u,
            &profile_view)) {
        session_profile_lease_end();
        return false;
    }

    aurora_cap_handle redelegated_profile =
        session_profile_lease_delegate(
            &desktop_process,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_ENUMERATE);

    if (redelegated_profile == AURORA_CAP_INVALID ||
        session_profile_lease_count() != 1u ||
        !profile_capability_matches_user(
            &desktop_process.capabilities,
            redelegated_profile,
            expected_user_id)) {
        session_profile_lease_end();
        return false;
    }

    session_profile_lease_end();

    if (session_profile_lease_active() ||
        session_profile_lease_count() != 0u ||
        cap_lookup(
            &desktop_process.capabilities,
            redelegated_profile,
            AURORA_CAP_FILE,
            0u,
            &profile_view)) {
        return false;
    }

    clear_bytes(&session_received, sizeof(session_received));
    clear_bytes(&session_result, sizeof(session_result));

    const uint64_t logout_id = UINT64_C(0x50524F464C4F474F);
    if (!send_manager_message(
            &session,
            AURORA_SESSION_MANAGER_LOGOUT,
            logout_id)) {
        return false;
    }

    clear_bytes(&session_received, sizeof(session_received));
    if (!wait_receive(&session, &session_received) ||
        session_received.length != sizeof(struct aurora_session_manager_result) ||
        session_received.capability_count != 0u) {
        return false;
    }

    clear_bytes(&session_result, sizeof(session_result));
    for (size_t i = 0u; i < sizeof(session_result); ++i) {
        ((uint8_t *)&session_result)[i] = session_received.data[i];
    }

    if (session_result.header.version != AURORA_SESSION_MANAGER_PROTOCOL_VERSION ||
        session_result.header.type != AURORA_SESSION_MANAGER_LOGOUT_RESULT ||
        session_result.header.request_id != logout_id ||
        session_result.state != AURORA_SESSION_MANAGER_STATE_LOGGED_OUT ||
        session_result.public_error != AURORA_SESSION_MANAGER_ERROR_NONE) {
        clear_bytes(&session_received, sizeof(session_received));
        clear_bytes(&session_result, sizeof(session_result));
        return false;
    }

    clear_bytes(&session_received, sizeof(session_received));
    clear_bytes(&session_result, sizeof(session_result));

    /*
     * The Session Manager revoked its source profile authority before
     * acknowledging logout. The session bridge owns the delegated copy and
     * must revoke that copy as part of logout completion.
     */
    if (!cap_revoke(&session.supervisor_caps, profile_handle) ||
        cap_lookup(
            &session.supervisor_caps,
            profile_handle,
            AURORA_CAP_FILE,
            0u,
            &profile_view)) {
        return false;
    }

    const uint64_t shutdown_id = UINT64_C(0x50524F4653485554);
    if (!send_manager_message(
            &session,
            AURORA_SESSION_MANAGER_SHUTDOWN,
            shutdown_id) ||
        !wait_message(
            &session,
            AURORA_SESSION_MANAGER_SHUTDOWN_ACK,
            shutdown_id) ||
        !wait_finished(&session)) {
        return false;
    }

    return reap_service(&session);
}

bool session_manager_ring3_self_test(void) {
    static struct aurora_trusted_service identity;
    static struct aurora_trusted_service session;
    static struct aurora_service_bootstrap_capability dependencies[3];

    clear_bytes(&identity, sizeof(identity));
    clear_bytes(&session, sizeof(session));
    clear_bytes(dependencies, sizeof(dependencies));

    const struct aurora_trusted_service_manifest identity_manifest = {
        .name = "session-probe-identity",
        .image = identity_service_probe_image(),
        .image_size = identity_service_probe_image_size(),
        .protected_state_scope = "session-probe-identity",
        .protected_state_rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
        .grant_entropy_seed = true
    };

    if (!service_bootstrap_start_trusted(&identity_manifest, &identity) ||
        !wait_identity_message(
            &identity,
            AURORA_IDENTITY_SERVICE_READY,
            0u)) {
        return false;
    }

    session_manager_probe_authority = UINT64_C(0x53455353494F4E50);

    dependencies[0].object = identity.supervisor_endpoint;
    dependencies[0].type = AURORA_CAP_IPC_ENDPOINT;
    dependencies[0].rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE;

    dependencies[1].object = &session_manager_probe_authority;
    dependencies[1].type = AURORA_CAP_IDENTITY_SESSION;
    dependencies[1].rights =
        AURORA_RIGHT_CONTROL | AURORA_RIGHT_TRANSFER;

    dependencies[2].object = profile_root_authority();
    dependencies[2].type = AURORA_CAP_PROFILE_ROOT;
    dependencies[2].rights = AURORA_RIGHT_CONTROL;

    const struct aurora_trusted_service_manifest session_manifest = {
        .name = "session-manager-probe",
        .image = session_manager_service_image(),
        .image_size = session_manager_service_image_size(),
        .protected_state_scope = "session-manager-probe",
        .protected_state_rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
        .grant_entropy_seed = false,
        .extra_capabilities = dependencies,
        .extra_capability_count = 3u
    };

    if (!service_bootstrap_start_trusted(&session_manifest, &session) ||
        !wait_message(
            &session,
            AURORA_SESSION_MANAGER_READY,
            0u)) {
        return false;
    }

    const uint64_t begin_request_id = UINT64_C(0x5345535300000001);
    struct aurora_session_manager_begin_session request;
    clear_bytes(&request, sizeof(request));
    request.header.version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION;
    request.header.type = AURORA_SESSION_MANAGER_BEGIN_SESSION;
    request.header.request_id = begin_request_id;

    /*
     * Deliberately invalid but non-zero. This forces the Session Manager to
     * cross the service-to-service Identity boundary instead of rejecting the
     * request locally.
     */
    for (size_t i = 0u; i < sizeof(request.session_grant); ++i) {
        request.session_grant[i] = (uint8_t)(0x31u + (uint8_t)i);
    }

    bool sent = service_bootstrap_send(
        &session,
        &request,
        sizeof(request));
    clear_bytes(&request, sizeof(request));
    if (!sent) return false;

    struct aurora_ipc_received received;
    struct aurora_session_manager_result result;
    clear_bytes(&received, sizeof(received));
    clear_bytes(&result, sizeof(result));
    if (!wait_receive(&session, &received) ||
        received.length != sizeof(result) ||
        received.capability_count != 0u) {
        return false;
    }

    for (size_t i = 0u; i < sizeof(result); ++i) {
        ((uint8_t *)&result)[i] = received.data[i];
    }
    clear_bytes(&received, sizeof(received));

    if (result.header.version != AURORA_SESSION_MANAGER_PROTOCOL_VERSION ||
        result.header.type != AURORA_SESSION_MANAGER_SESSION_RESULT ||
        result.header.request_id != begin_request_id ||
        result.session_generation != 0u ||
        !all_zero(result.user_id, sizeof(result.user_id))) {
        clear_bytes(&result, sizeof(result));
        return false;
    }

    bool operational_rejection =
        result.state == AURORA_SESSION_MANAGER_STATE_REJECTED &&
        result.public_error == AURORA_SESSION_MANAGER_ERROR_IDENTITY_REJECTED;
    bool degraded_rejection =
        result.state == AURORA_SESSION_MANAGER_STATE_SERVICE_ERROR &&
        result.public_error == AURORA_SESSION_MANAGER_ERROR_IDENTITY_UNAVAILABLE;
    clear_bytes(&result, sizeof(result));

    if (!operational_rejection && !degraded_rejection) {
        return false;
    }

    const uint64_t session_shutdown_id = UINT64_C(0x5345535353485554);
    if (!send_manager_message(
            &session,
            AURORA_SESSION_MANAGER_SHUTDOWN,
            session_shutdown_id) ||
        !wait_message(
            &session,
            AURORA_SESSION_MANAGER_SHUTDOWN_ACK,
            session_shutdown_id) ||
        !wait_finished(&session) ||
        !reap_service(&session)) {
        return false;
    }

    const uint64_t identity_shutdown_id = UINT64_C(0x4944454E53485554);
    if (!send_identity_message(
            &identity,
            AURORA_IDENTITY_SERVICE_SHUTDOWN,
            identity_shutdown_id) ||
        !wait_identity_message(
            &identity,
            AURORA_IDENTITY_SERVICE_SHUTDOWN_ACK,
            identity_shutdown_id) ||
        !wait_finished(&identity) ||
        !reap_service(&identity)) {
        return false;
    }

    if (!session_manager_profile_binding_self_test()) {
        return false;
    }

    return true;
}
