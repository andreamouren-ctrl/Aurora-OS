#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/identity_reauth_probe.h>
#include <aurora/identity_service_probe.h>
#include <aurora/identity_service_protocol.h>
#include <aurora/ipc.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/service_bootstrap.h>

#define IDENTITY_REAUTH_PROBE_TIMEOUT_NS UINT64_C(3000000000)

static uint64_t reauth_probe_authority;

static void clear_bytes(void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    if (buffer == NULL) return;
    for (size_t i = 0u; i < size; ++i) bytes[i] = 0u;
}

static bool all_zero(const uint8_t *bytes, size_t size) {
    uint8_t aggregate = 0u;
    if (bytes == NULL) return false;
    for (size_t i = 0u; i < size; ++i) aggregate |= bytes[i];
    return aggregate == 0u;
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

static bool wait_receive(
    struct aurora_trusted_service *service,
    struct aurora_ipc_received *out
) {
    uint64_t deadline = clock_now_ns() + IDENTITY_REAUTH_PROBE_TIMEOUT_NS;
    if (service == NULL || out == NULL) return false;

    while (clock_now_ns() < deadline) {
        if (service_bootstrap_receive(service, out)) return true;
        if (service->thread != 0u &&
            scheduler_thread_finished(service->thread)) {
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

static bool send_begin(
    struct aurora_trusted_service *service,
    uint64_t request_id,
    enum aurora_cap_type authority_type
) {
    static const char key[] = "REAUTHPROBE123";
    struct aurora_identity_service_begin_reauth request;
    clear_bytes(&request, sizeof(request));

    request.header.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    request.header.type = AURORA_IDENTITY_SERVICE_BEGIN_REAUTH;
    request.header.request_id = request_id;
    request.key_length = (uint32_t)(sizeof(key) - 1u);
    request.purpose =
        AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY;
    request.session_generation = UINT64_C(1);
    request.expected_user_id[0] = 1u;
    for (size_t i = 0u; i < sizeof(key) - 1u; ++i) {
        request.key[i] = key[i];
    }

    reauth_probe_authority ^= request_id | UINT64_C(1);
    aurora_cap_handle authority = cap_grant(
        &service->supervisor_caps,
        &reauth_probe_authority,
        authority_type,
        AURORA_RIGHT_CONTROL | AURORA_RIGHT_TRANSFER);
    if (authority == AURORA_CAP_INVALID) {
        clear_bytes(&request, sizeof(request));
        return false;
    }

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

static bool send_control(
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

static bool decode_result(
    const struct aurora_ipc_received *received,
    struct aurora_identity_service_reauth_result *out
) {
    if (received == NULL || out == NULL ||
        received->length != sizeof(*out) ||
        received->capability_count != 0u) {
        return false;
    }

    clear_bytes(out, sizeof(*out));
    for (size_t i = 0u; i < sizeof(*out); ++i) {
        ((uint8_t *)out)[i] = received->data[i];
    }

    return out->header.version == AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION &&
        out->header.type == AURORA_IDENTITY_SERVICE_REAUTH_RESULT;
}

static bool wait_finished(struct aurora_trusted_service *service) {
    uint64_t deadline = clock_now_ns() + IDENTITY_REAUTH_PROBE_TIMEOUT_NS;
    if (service == NULL || service->thread == 0u) return false;

    while (!scheduler_thread_finished(service->thread) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }
    return scheduler_thread_finished(service->thread);
}

bool identity_reauth_ring3_self_test(void) {
    static struct aurora_trusted_service service;
    const struct aurora_trusted_service_manifest manifest = {
        .name = "identity-reauth-probe",
        .image = identity_service_probe_image(),
        .image_size = identity_service_probe_image_size(),
        .protected_state_scope = "identity-reauth-probe",
        .protected_state_rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
        .grant_entropy_seed = true
    };

    clear_bytes(&service, sizeof(service));
    if (!service_bootstrap_start_trusted(&manifest, &service) ||
        !wait_message(&service, AURORA_IDENTITY_SERVICE_READY, 0u)) {
        return false;
    }

    const uint64_t request_id = UINT64_C(0x5245415554480001);
    if (!send_begin(&service, request_id, AURORA_CAP_IDENTITY_REAUTH)) {
        return false;
    }

    struct aurora_ipc_received received;
    clear_bytes(&received, sizeof(received));
    if (!wait_receive(&service, &received)) return false;

    if (received.length == AURORA_IDENTITY_SERVICE_MESSAGE_SIZE) {
        const struct aurora_identity_service_message expected = {
            .version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION,
            .type = AURORA_IDENTITY_SERVICE_REAUTH_PENDING,
            .request_id = request_id
        };
        if (received.capability_count != 0u ||
            !bytes_equal(received.data, &expected, sizeof(expected)) ||
            !send_control(
                &service,
                AURORA_IDENTITY_SERVICE_QUERY_REAUTH,
                request_id)) {
            return false;
        }

        clear_bytes(&received, sizeof(received));
        if (!wait_receive(&service, &received)) return false;

        struct aurora_identity_service_reauth_result result;
        clear_bytes(&result, sizeof(result));
        if (!decode_result(&received, &result) ||
            result.header.request_id != request_id ||
            result.state != AURORA_IDENTITY_SERVICE_REAUTH_STATE_FAILED ||
            result.public_error != AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_AUTH_FAILED ||
            result.purpose !=
                AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_ROTATE_PRIMARY_KEY ||
            result.expires_at_ms != 0u ||
            !all_zero(result.proof, sizeof(result.proof))) {
            return false;
        }
    } else {
        struct aurora_identity_service_reauth_result result;
        clear_bytes(&result, sizeof(result));
        if (!decode_result(&received, &result) ||
            result.header.request_id != request_id ||
            result.state !=
                AURORA_IDENTITY_SERVICE_REAUTH_STATE_SERVICE_ERROR ||
            result.public_error !=
                AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_SERVICE_UNAVAILABLE ||
            !all_zero(result.proof, sizeof(result.proof))) {
            return false;
        }
    }

    const uint64_t wrong_cap_id = UINT64_C(0x5245415554480002);
    if (!send_begin(&service, wrong_cap_id, AURORA_CAP_IDENTITY_AUTH)) {
        return false;
    }

    clear_bytes(&received, sizeof(received));
    if (!wait_receive(&service, &received)) return false;
    struct aurora_identity_service_reauth_result denied;
    clear_bytes(&denied, sizeof(denied));
    if (!decode_result(&received, &denied) ||
        denied.header.request_id != wrong_cap_id ||
        denied.state != AURORA_IDENTITY_SERVICE_REAUTH_STATE_SERVICE_ERROR ||
        denied.public_error != AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_UNAUTHORIZED ||
        !all_zero(denied.proof, sizeof(denied.proof))) {
        return false;
    }

    const uint64_t shutdown_id = UINT64_C(0x5245415554485348);
    if (!send_control(
            &service,
            AURORA_IDENTITY_SERVICE_SHUTDOWN,
            shutdown_id) ||
        !wait_message(
            &service,
            AURORA_IDENTITY_SERVICE_SHUTDOWN_ACK,
            shutdown_id) ||
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
