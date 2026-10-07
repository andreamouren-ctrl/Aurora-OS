#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/identity_service_probe.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/service_bootstrap.h>
#include <aurora/session_manager_probe.h>
#include <aurora/session_manager_protocol.h>
#include <aurora/session_manager_service.h>

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

bool session_manager_ring3_self_test(void) {
    static struct aurora_trusted_service identity;
    static struct aurora_trusted_service session;
    static struct aurora_service_bootstrap_capability dependencies[2];

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

    const struct aurora_trusted_service_manifest session_manifest = {
        .name = "session-manager-probe",
        .image = session_manager_service_image(),
        .image_size = session_manager_service_image_size(),
        .protected_state_scope = "session-manager-probe",
        .protected_state_rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
        .grant_entropy_seed = false,
        .extra_capabilities = dependencies,
        .extra_capability_count = 2u
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

    return true;
}
