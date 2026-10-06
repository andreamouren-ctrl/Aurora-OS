#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/identity_auth_probe.h>
#include <aurora/identity_service_probe.h>
#include <aurora/identity_service_protocol.h>
#include <aurora/ipc.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/service_bootstrap.h>

#define IDENTITY_AUTH_PROBE_TIMEOUT_NS 2000000000ull

static uint64_t identity_auth_probe_authority;

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

    uint64_t deadline = clock_now_ns() + IDENTITY_AUTH_PROBE_TIMEOUT_NS;
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
        received.length != AURORA_IDENTITY_SERVICE_MESSAGE_SIZE ||
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

static bool wait_auth_failed(
    struct aurora_trusted_service *service,
    uint64_t request_id
) {
    struct aurora_ipc_received received;
    clear_bytes(&received, sizeof(received));
    if (!wait_receive(service, &received) ||
        received.length != sizeof(struct aurora_identity_service_auth_result) ||
        received.capability_count != 0u) {
        return false;
    }

    struct aurora_identity_service_auth_result result;
    clear_bytes(&result, sizeof(result));
    for (size_t i = 0u; i < sizeof(result); ++i) {
        ((uint8_t *)&result)[i] = received.data[i];
    }

    return result.header.version == AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION &&
        result.header.type == AURORA_IDENTITY_SERVICE_AUTH_RESULT &&
        result.header.request_id == request_id &&
        result.state == AURORA_IDENTITY_SERVICE_AUTH_STATE_FAILED &&
        result.public_error == AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_AUTH_FAILED &&
        result.retry_after_ms == 0u &&
        all_zero(result.session_grant, sizeof(result.session_grant));
}

static bool send_auth_begin(
    struct aurora_trusted_service *service,
    uint64_t request_id,
    const char *key,
    size_t key_length
) {
    if (service == NULL || key == NULL || key_length == 0u ||
        key_length > AURORA_IDENTITY_SERVICE_KEY_MAX_LEN) {
        return false;
    }

    identity_auth_probe_authority ^= request_id | 1ull;
    aurora_cap_handle authority = cap_grant(
        &service->supervisor_caps,
        &identity_auth_probe_authority,
        AURORA_CAP_IDENTITY_AUTH,
        AURORA_RIGHT_CONTROL | AURORA_RIGHT_TRANSFER);
    if (authority == AURORA_CAP_INVALID) return false;

    struct aurora_identity_service_begin_key_auth request;
    clear_bytes(&request, sizeof(request));
    request.header.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    request.header.type = AURORA_IDENTITY_SERVICE_BEGIN_KEY_AUTH;
    request.header.request_id = request_id;
    request.key_length = (uint32_t)key_length;
    for (size_t i = 0u; i < key_length; ++i) request.key[i] = key[i];

    const struct aurora_ipc_transfer transfer = {
        .handle = authority,
        .rights = AURORA_RIGHT_CONTROL
    };

    bool sent = ipc_send(
        service->supervisor_endpoint,
        &service->supervisor_caps,
        &request,
        (uint32_t)sizeof(request),
        &transfer,
        1u);
    bool revoked = cap_revoke(&service->supervisor_caps, authority);
    clear_bytes(&request, sizeof(request));
    return sent && revoked;
}

static bool send_control_message(
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
    uint64_t deadline = clock_now_ns() + IDENTITY_AUTH_PROBE_TIMEOUT_NS;
    while (!scheduler_thread_finished(service->thread) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }
    return scheduler_thread_finished(service->thread);
}

bool identity_auth_ring3_self_test(void) {
    static struct aurora_trusted_service service;
    const struct aurora_trusted_service_manifest manifest = {
        .name = "identity-auth-probe",
        .image = identity_service_probe_image(),
        .image_size = identity_service_probe_image_size(),
        .protected_state_scope = "identity",
        .protected_state_rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
        .grant_entropy_seed = true
    };

    clear_bytes(&service, sizeof(service));
    if (!service_bootstrap_start_trusted(&manifest, &service) ||
        !wait_message(&service, AURORA_IDENTITY_SERVICE_READY, 0u)) {
        return false;
    }

    static const char unknown_key[] = "AUR7K4PN9Q2XM6D";
    const uint64_t query_request_id = UINT64_C(0x4155544800000001);
    if (!send_auth_begin(
            &service,
            query_request_id,
            unknown_key,
            sizeof(unknown_key) - 1u) ||
        !wait_message(
            &service,
            AURORA_IDENTITY_SERVICE_AUTH_PENDING,
            query_request_id) ||
        !send_control_message(
            &service,
            AURORA_IDENTITY_SERVICE_QUERY_AUTH,
            query_request_id) ||
        !wait_auth_failed(&service, query_request_id)) {
        return false;
    }

    const uint64_t cancel_request_id = UINT64_C(0x4155544800000002);
    if (!send_auth_begin(
            &service,
            cancel_request_id,
            unknown_key,
            sizeof(unknown_key) - 1u) ||
        !wait_message(
            &service,
            AURORA_IDENTITY_SERVICE_AUTH_PENDING,
            cancel_request_id) ||
        !send_control_message(
            &service,
            AURORA_IDENTITY_SERVICE_CANCEL_AUTH,
            cancel_request_id) ||
        !wait_message(
            &service,
            AURORA_IDENTITY_SERVICE_AUTH_CANCELLED,
            cancel_request_id)) {
        return false;
    }

    const uint64_t shutdown_request_id = UINT64_C(0x4155544853485554);
    if (!send_control_message(
            &service,
            AURORA_IDENTITY_SERVICE_SHUTDOWN,
            shutdown_request_id) ||
        !wait_message(
            &service,
            AURORA_IDENTITY_SERVICE_SHUTDOWN_ACK,
            shutdown_request_id) ||
        !wait_finished(&service)) {
        return false;
    }

    if (service.process == NULL ||
        process_state(service.process) != AURORA_PROCESS_EXITED ||
        service.process->exit_code != 0) {
        return false;
    }

    struct aurora_process *process = service.process;
    aurora_thread_id thread = service.thread;
    if (!scheduler_reap_thread(thread) ||
        process_live_thread_count(process) != 0u ||
        !process_reap(process, NULL) ||
        !process_release(process)) {
        return false;
    }

    clear_bytes(&service, sizeof(service));
    return true;
}
