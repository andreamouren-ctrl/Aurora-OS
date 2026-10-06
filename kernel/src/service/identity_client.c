#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/identity_client.h>
#include <aurora/identity_service_probe.h>
#include <aurora/identity_service_protocol.h>
#include <aurora/ipc.h>
#include <aurora/service_supervisor.h>

#define IDENTITY_CLIENT_READY_TIMEOUT_NS 2000000000ull

static struct aurora_service_supervisor identity_supervisor;
static enum aurora_identity_client_state client_state =
    AURORA_IDENTITY_CLIENT_UNINITIALIZED;
static uint64_t current_request_id;
static uint64_t next_request_id = UINT64_C(0x4C4F47494E000001);
static uint64_t retry_after_ms;
static uint64_t auth_authority_object;

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

static bool wait_ready(void) {
    uint64_t deadline = clock_now_ns() + IDENTITY_CLIENT_READY_TIMEOUT_NS;
    while (clock_now_ns() < deadline) {
        struct aurora_ipc_received received;
        clear_bytes(&received, sizeof(received));
        if (service_supervisor_receive(&identity_supervisor, &received)) {
            const struct aurora_identity_service_message expected = {
                .version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION,
                .type = AURORA_IDENTITY_SERVICE_READY,
                .request_id = 0u
            };
            return received.length == sizeof(expected) &&
                received.capability_count == 0u &&
                bytes_equal(received.data, &expected, sizeof(expected));
        }

        if (identity_supervisor.service.thread != 0u &&
            scheduler_thread_finished(identity_supervisor.service.thread)) {
            return false;
        }

        arch_idle();
    }
    return false;
}

static bool send_query(void) {
    const struct aurora_identity_service_message query = {
        .version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION,
        .type = AURORA_IDENTITY_SERVICE_QUERY_AUTH,
        .request_id = current_request_id
    };
    return service_supervisor_send(
        &identity_supervisor,
        &query,
        (uint32_t)sizeof(query));
}

static void apply_auth_result(
    const struct aurora_identity_service_auth_result *result
) {
    if (result == NULL || result->header.request_id != current_request_id) {
        client_state = AURORA_IDENTITY_CLIENT_ERROR;
        return;
    }

    retry_after_ms = result->retry_after_ms;
    switch (result->state) {
        case AURORA_IDENTITY_SERVICE_AUTH_STATE_FAILED:
            client_state = AURORA_IDENTITY_CLIENT_AUTH_FAILED;
            return;

        case AURORA_IDENTITY_SERVICE_AUTH_STATE_THROTTLED:
            client_state = AURORA_IDENTITY_CLIENT_THROTTLED;
            return;

        case AURORA_IDENTITY_SERVICE_AUTH_STATE_SUCCESS:
            /*
             * The opaque grant is intentionally not retained in kernel UI
             * state. A future Session Manager will own grant consumption and
             * session establishment. Until then, successful verification is
             * visible to the login surface but cannot fabricate a session.
             */
            client_state = AURORA_IDENTITY_CLIENT_VERIFIED;
            return;

        case AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR:
            client_state =
                result->public_error ==
                    AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_SERVICE_UNAVAILABLE
                ? AURORA_IDENTITY_CLIENT_UNAVAILABLE
                : AURORA_IDENTITY_CLIENT_ERROR;
            return;

        default:
            client_state = AURORA_IDENTITY_CLIENT_ERROR;
            return;
    }
}

bool identity_client_init(void) {
    const struct aurora_trusted_service_manifest manifest = {
        .name = "identity-service",
        .image = identity_service_probe_image(),
        .image_size = identity_service_probe_image_size(),
        .protected_state_scope = "identity",
        .protected_state_rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
        .grant_entropy_seed = true
    };

    clear_bytes(&identity_supervisor, sizeof(identity_supervisor));
    current_request_id = 0u;
    retry_after_ms = 0u;

    if (!service_supervisor_init(
            &identity_supervisor,
            &manifest,
            AURORA_SERVICE_RESTART_ON_FAILURE,
            2u) ||
        !service_supervisor_start(&identity_supervisor) ||
        !wait_ready()) {
        client_state = AURORA_IDENTITY_CLIENT_UNAVAILABLE;
        return false;
    }

    client_state = AURORA_IDENTITY_CLIENT_READY;
    return true;
}

bool identity_client_begin_key_auth(
    const char *key,
    size_t key_length
) {
    if (key == NULL || key_length == 0u ||
        key_length > AURORA_IDENTITY_SERVICE_KEY_MAX_LEN ||
        client_state != AURORA_IDENTITY_CLIENT_READY ||
        identity_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        return false;
    }

    struct aurora_identity_service_begin_key_auth request;
    clear_bytes(&request, sizeof(request));
    current_request_id = next_request_id++;
    if (current_request_id == 0u) current_request_id = next_request_id++;

    request.header.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    request.header.type = AURORA_IDENTITY_SERVICE_BEGIN_KEY_AUTH;
    request.header.request_id = current_request_id;
    request.key_length = (uint32_t)key_length;
    for (size_t i = 0u; i < key_length; ++i) request.key[i] = key[i];

    auth_authority_object ^= current_request_id | 1u;
    aurora_cap_handle authority = cap_grant(
        &identity_supervisor.service.supervisor_caps,
        &auth_authority_object,
        AURORA_CAP_IDENTITY_AUTH,
        AURORA_RIGHT_CONTROL | AURORA_RIGHT_TRANSFER);
    if (authority == AURORA_CAP_INVALID) {
        clear_bytes(&request, sizeof(request));
        current_request_id = 0u;
        client_state = AURORA_IDENTITY_CLIENT_ERROR;
        return false;
    }

    const struct aurora_ipc_transfer transfer = {
        .handle = authority,
        .rights = AURORA_RIGHT_CONTROL
    };
    bool sent = ipc_send(
        identity_supervisor.service.supervisor_endpoint,
        &identity_supervisor.service.supervisor_caps,
        &request,
        (uint32_t)sizeof(request),
        &transfer,
        1u);
    bool revoked = cap_revoke(
        &identity_supervisor.service.supervisor_caps,
        authority);
    clear_bytes(&request, sizeof(request));

    if (!sent || !revoked) {
        current_request_id = 0u;
        client_state = AURORA_IDENTITY_CLIENT_ERROR;
        return false;
    }

    retry_after_ms = 0u;
    client_state = AURORA_IDENTITY_CLIENT_AUTHENTICATING;
    return true;
}

void identity_client_pump(void) {
    if (client_state == AURORA_IDENTITY_CLIENT_UNINITIALIZED) return;

    if (!service_supervisor_step(&identity_supervisor)) {
        client_state = AURORA_IDENTITY_CLIENT_ERROR;
        return;
    }

    if (identity_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        if (client_state == AURORA_IDENTITY_CLIENT_AUTHENTICATING) {
            client_state = AURORA_IDENTITY_CLIENT_UNAVAILABLE;
        }
        return;
    }

    if (client_state != AURORA_IDENTITY_CLIENT_AUTHENTICATING) return;

    struct aurora_ipc_received received;
    clear_bytes(&received, sizeof(received));
    if (!service_supervisor_receive(&identity_supervisor, &received)) return;

    if (received.capability_count != 0u) {
        client_state = AURORA_IDENTITY_CLIENT_ERROR;
        clear_bytes(&received, sizeof(received));
        return;
    }

    if (received.length == sizeof(struct aurora_identity_service_message)) {
        struct aurora_identity_service_message message;
        clear_bytes(&message, sizeof(message));
        for (size_t i = 0u; i < sizeof(message); ++i) {
            ((uint8_t *)&message)[i] = received.data[i];
        }

        bool pending =
            message.version == AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION &&
            message.type == AURORA_IDENTITY_SERVICE_AUTH_PENDING &&
            message.request_id == current_request_id;
        clear_bytes(&received, sizeof(received));

        if (!pending || !send_query()) {
            client_state = AURORA_IDENTITY_CLIENT_ERROR;
        }
        return;
    }

    if (received.length == sizeof(struct aurora_identity_service_auth_result)) {
        struct aurora_identity_service_auth_result result;
        clear_bytes(&result, sizeof(result));
        for (size_t i = 0u; i < sizeof(result); ++i) {
            ((uint8_t *)&result)[i] = received.data[i];
        }
        clear_bytes(&received, sizeof(received));
        apply_auth_result(&result);
        clear_bytes(&result, sizeof(result));
        current_request_id = 0u;
        return;
    }

    clear_bytes(&received, sizeof(received));
    client_state = AURORA_IDENTITY_CLIENT_ERROR;
}

void identity_client_reset_result(void) {
    if (client_state == AURORA_IDENTITY_CLIENT_AUTH_FAILED ||
        client_state == AURORA_IDENTITY_CLIENT_THROTTLED ||
        client_state == AURORA_IDENTITY_CLIENT_VERIFIED ||
        client_state == AURORA_IDENTITY_CLIENT_ERROR) {
        retry_after_ms = 0u;
        current_request_id = 0u;
        client_state = identity_supervisor.state == AURORA_SERVICE_SUPERVISOR_RUNNING
            ? AURORA_IDENTITY_CLIENT_READY
            : AURORA_IDENTITY_CLIENT_UNAVAILABLE;
    }
}

enum aurora_identity_client_state identity_client_state(void) {
    return client_state;
}

uint64_t identity_client_retry_after_ms(void) {
    return retry_after_ms;
}
