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
#include <aurora/session_manager_client.h>

#define IDENTITY_CLIENT_READY_TIMEOUT_NS 2000000000ull

static struct aurora_service_supervisor identity_supervisor;
static enum aurora_identity_client_state client_state =
    AURORA_IDENTITY_CLIENT_UNINITIALIZED;
static uint64_t current_request_id;
static uint64_t next_request_id = UINT64_C(0x4C4F47494E000001);
static uint64_t retry_after_ms;
static uint64_t auth_authority_object;
static uint64_t create_authority_object;
static uint64_t reauth_authority_object;
static uint8_t pending_session_grant[AURORA_IDENTITY_SERVICE_GRANT_TOKEN_SIZE];
static uint8_t pending_reauth_proof[AURORA_IDENTITY_SERVICE_REAUTH_PROOF_SIZE];
static uint32_t pending_reauth_purpose;
static uint64_t pending_reauth_expires_at_ms;

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

static bool send_query(uint32_t type) {
    const struct aurora_identity_service_message query = {
        .version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION,
        .type = type,
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
    clear_bytes(pending_session_grant, sizeof(pending_session_grant));

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
            clear_bytes(
                pending_session_grant,
                sizeof(pending_session_grant));
            for (size_t i = 0u; i < sizeof(pending_session_grant); ++i) {
                pending_session_grant[i] = result->session_grant[i];
            }
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

static void apply_create_result(
    const struct aurora_identity_service_create_result *result
) {
    if (result == NULL || result->header.request_id != current_request_id) {
        client_state = AURORA_IDENTITY_CLIENT_ERROR;
        return;
    }

    switch (result->state) {
        case AURORA_IDENTITY_SERVICE_CREATE_STATE_SUCCESS:
            /*
             * User and credential identifiers intentionally stay inside this
             * transient reply object. Creation does not establish a session;
             * a future Session Manager must perform the next authority step.
             */
            client_state = AURORA_IDENTITY_CLIENT_CREATED;
            return;

        case AURORA_IDENTITY_SERVICE_CREATE_STATE_ALREADY_EXISTS:
            client_state = AURORA_IDENTITY_CLIENT_CREATE_EXISTS;
            return;

        case AURORA_IDENTITY_SERVICE_CREATE_STATE_SERVICE_ERROR:
            if (result->public_error ==
                AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_SERVICE_UNAVAILABLE) {
                client_state = AURORA_IDENTITY_CLIENT_UNAVAILABLE;
            } else if (result->public_error ==
                       AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_POLICY_DENIED) {
                client_state = AURORA_IDENTITY_CLIENT_CREATE_DENIED;
            } else {
                client_state = AURORA_IDENTITY_CLIENT_ERROR;
            }
            return;

        default:
            client_state = AURORA_IDENTITY_CLIENT_ERROR;
            return;
    }
}

static void apply_reauth_result(
    const struct aurora_identity_service_reauth_result *result
) {
    clear_bytes(pending_reauth_proof, sizeof(pending_reauth_proof));
    pending_reauth_purpose = 0u;
    pending_reauth_expires_at_ms = 0u;

    if (result == NULL || result->header.request_id != current_request_id) {
        client_state = AURORA_IDENTITY_CLIENT_ERROR;
        return;
    }

    retry_after_ms = result->retry_after_ms;

    switch (result->state) {
        case AURORA_IDENTITY_SERVICE_REAUTH_STATE_SUCCESS:
            if (result->purpose <= AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_NONE ||
                result->purpose >= AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_COUNT ||
                result->expires_at_ms == 0u) {
                client_state = AURORA_IDENTITY_CLIENT_ERROR;
                return;
            }

            for (size_t i = 0u; i < sizeof(pending_reauth_proof); ++i) {
                pending_reauth_proof[i] = result->proof[i];
            }
            pending_reauth_purpose = result->purpose;
            pending_reauth_expires_at_ms = result->expires_at_ms;
            client_state = AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED;
            return;

        case AURORA_IDENTITY_SERVICE_REAUTH_STATE_FAILED:
            client_state = AURORA_IDENTITY_CLIENT_REAUTH_FAILED;
            return;

        case AURORA_IDENTITY_SERVICE_REAUTH_STATE_THROTTLED:
            client_state = AURORA_IDENTITY_CLIENT_REAUTH_THROTTLED;
            return;

        case AURORA_IDENTITY_SERVICE_REAUTH_STATE_SERVICE_ERROR:
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

static uint64_t allocate_request_id(void) {
    uint64_t request_id = next_request_id++;
    if (request_id == 0u) request_id = next_request_id++;
    return request_id;
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
    clear_bytes(pending_session_grant, sizeof(pending_session_grant));
    clear_bytes(pending_reauth_proof, sizeof(pending_reauth_proof));
    pending_reauth_purpose = 0u;
    pending_reauth_expires_at_ms = 0u;
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
    current_request_id = allocate_request_id();

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

bool identity_client_begin_create(
    const char *key,
    size_t key_length
) {
    if (key == NULL || key_length == 0u ||
        key_length > AURORA_IDENTITY_SERVICE_KEY_MAX_LEN ||
        client_state != AURORA_IDENTITY_CLIENT_READY ||
        identity_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        return false;
    }

    struct aurora_identity_service_begin_create request;
    clear_bytes(&request, sizeof(request));
    current_request_id = allocate_request_id();

    request.header.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    request.header.type = AURORA_IDENTITY_SERVICE_BEGIN_CREATE;
    request.header.request_id = current_request_id;
    request.key_length = (uint32_t)key_length;
    for (size_t i = 0u; i < key_length; ++i) request.key[i] = key[i];

    create_authority_object ^= current_request_id | 1u;
    aurora_cap_handle authority = cap_grant(
        &identity_supervisor.service.supervisor_caps,
        &create_authority_object,
        AURORA_CAP_IDENTITY_CREATE,
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
    client_state = AURORA_IDENTITY_CLIENT_CREATING;
    return true;
}

bool identity_client_begin_reauth(
    const char *key,
    size_t key_length,
    uint32_t purpose
) {
    if (key == NULL || key_length == 0u ||
        key_length > AURORA_IDENTITY_SERVICE_KEY_MAX_LEN ||
        purpose <= AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_NONE ||
        purpose >= AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_COUNT ||
        client_state != AURORA_IDENTITY_CLIENT_READY ||
        identity_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING ||
        session_manager_client_state() != AURORA_SESSION_CLIENT_ACTIVE) {
        return false;
    }

    const uint8_t *user_id = session_manager_client_user_id();
    if (user_id == NULL) return false;

    uint8_t aggregate = 0u;
    for (size_t i = 0u; i < AURORA_IDENTITY_SERVICE_USER_ID_SIZE; ++i) {
        aggregate |= user_id[i];
    }
    if (aggregate == 0u) return false;

    struct aurora_identity_service_begin_reauth request;
    clear_bytes(&request, sizeof(request));
    current_request_id = allocate_request_id();

    request.header.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    request.header.type = AURORA_IDENTITY_SERVICE_BEGIN_REAUTH;
    request.header.request_id = current_request_id;
    request.key_length = (uint32_t)key_length;
    request.purpose = purpose;
    for (size_t i = 0u; i < AURORA_IDENTITY_SERVICE_USER_ID_SIZE; ++i) {
        request.expected_user_id[i] = user_id[i];
    }
    for (size_t i = 0u; i < key_length; ++i) {
        request.key[i] = key[i];
    }

    reauth_authority_object ^= current_request_id | 1u;
    aurora_cap_handle authority = cap_grant(
        &identity_supervisor.service.supervisor_caps,
        &reauth_authority_object,
        AURORA_CAP_IDENTITY_REAUTH,
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
    clear_bytes(pending_reauth_proof, sizeof(pending_reauth_proof));
    pending_reauth_purpose = 0u;
    pending_reauth_expires_at_ms = 0u;

    if (!sent || !revoked) {
        current_request_id = 0u;
        client_state = AURORA_IDENTITY_CLIENT_ERROR;
        return false;
    }

    retry_after_ms = 0u;
    client_state = AURORA_IDENTITY_CLIENT_REAUTHENTICATING;
    return true;
}

void identity_client_pump(void) {
    if (client_state == AURORA_IDENTITY_CLIENT_UNINITIALIZED) return;

    if (!service_supervisor_step(&identity_supervisor)) {
        clear_bytes(pending_session_grant, sizeof(pending_session_grant));
        clear_bytes(pending_reauth_proof, sizeof(pending_reauth_proof));
        pending_reauth_purpose = 0u;
        pending_reauth_expires_at_ms = 0u;
        client_state = AURORA_IDENTITY_CLIENT_ERROR;
        return;
    }

    if (identity_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        clear_bytes(pending_session_grant, sizeof(pending_session_grant));
        clear_bytes(pending_reauth_proof, sizeof(pending_reauth_proof));
        pending_reauth_purpose = 0u;
        pending_reauth_expires_at_ms = 0u;
        if (client_state == AURORA_IDENTITY_CLIENT_AUTHENTICATING ||
            client_state == AURORA_IDENTITY_CLIENT_CREATING ||
            client_state == AURORA_IDENTITY_CLIENT_REAUTHENTICATING ||
            client_state == AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED ||
            client_state == AURORA_IDENTITY_CLIENT_VERIFIED) {
            client_state = AURORA_IDENTITY_CLIENT_UNAVAILABLE;
        }
        return;
    }

    if (client_state != AURORA_IDENTITY_CLIENT_AUTHENTICATING &&
        client_state != AURORA_IDENTITY_CLIENT_CREATING &&
        client_state != AURORA_IDENTITY_CLIENT_REAUTHENTICATING) {
        return;
    }

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

        uint32_t pending_type =
            client_state == AURORA_IDENTITY_CLIENT_AUTHENTICATING
                ? AURORA_IDENTITY_SERVICE_AUTH_PENDING
                : client_state == AURORA_IDENTITY_CLIENT_CREATING
                    ? AURORA_IDENTITY_SERVICE_CREATE_PENDING
                    : AURORA_IDENTITY_SERVICE_REAUTH_PENDING;
        uint32_t query_type =
            client_state == AURORA_IDENTITY_CLIENT_AUTHENTICATING
                ? AURORA_IDENTITY_SERVICE_QUERY_AUTH
                : client_state == AURORA_IDENTITY_CLIENT_CREATING
                    ? AURORA_IDENTITY_SERVICE_QUERY_CREATE
                    : AURORA_IDENTITY_SERVICE_QUERY_REAUTH;
        bool pending =
            message.version == AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION &&
            message.type == pending_type &&
            message.request_id == current_request_id;
        clear_bytes(&received, sizeof(received));

        if (!pending || !send_query(query_type)) {
            client_state = AURORA_IDENTITY_CLIENT_ERROR;
        }
        return;
    }

    if (client_state == AURORA_IDENTITY_CLIENT_AUTHENTICATING &&
        received.length == sizeof(struct aurora_identity_service_auth_result)) {
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

    if (client_state == AURORA_IDENTITY_CLIENT_CREATING &&
        received.length == sizeof(struct aurora_identity_service_create_result)) {
        struct aurora_identity_service_create_result result;
        clear_bytes(&result, sizeof(result));
        for (size_t i = 0u; i < sizeof(result); ++i) {
            ((uint8_t *)&result)[i] = received.data[i];
        }
        clear_bytes(&received, sizeof(received));
        apply_create_result(&result);
        clear_bytes(&result, sizeof(result));
        current_request_id = 0u;
        return;
    }

    if (client_state == AURORA_IDENTITY_CLIENT_REAUTHENTICATING &&
        received.length == sizeof(struct aurora_identity_service_reauth_result)) {
        struct aurora_identity_service_reauth_result result;
        clear_bytes(&result, sizeof(result));
        for (size_t i = 0u; i < sizeof(result); ++i) {
            ((uint8_t *)&result)[i] = received.data[i];
        }
        clear_bytes(&received, sizeof(received));
        apply_reauth_result(&result);
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
        client_state == AURORA_IDENTITY_CLIENT_CREATED ||
        client_state == AURORA_IDENTITY_CLIENT_CREATE_EXISTS ||
        client_state == AURORA_IDENTITY_CLIENT_CREATE_DENIED ||
        client_state == AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED ||
        client_state == AURORA_IDENTITY_CLIENT_REAUTH_FAILED ||
        client_state == AURORA_IDENTITY_CLIENT_REAUTH_THROTTLED ||
        client_state == AURORA_IDENTITY_CLIENT_ERROR) {
        clear_bytes(pending_session_grant, sizeof(pending_session_grant));
        clear_bytes(pending_reauth_proof, sizeof(pending_reauth_proof));
        pending_reauth_purpose = 0u;
        pending_reauth_expires_at_ms = 0u;
        retry_after_ms = 0u;
        current_request_id = 0u;
        clear_bytes(pending_session_grant, sizeof(pending_session_grant));
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


bool identity_client_take_session_grant(
    uint8_t out_grant[AURORA_IDENTITY_SERVICE_GRANT_TOKEN_SIZE]
) {
    if (out_grant == NULL ||
        client_state != AURORA_IDENTITY_CLIENT_VERIFIED) {
        return false;
    }

    uint8_t combined = 0u;
    for (size_t i = 0u; i < sizeof(pending_session_grant); ++i) {
        combined |= pending_session_grant[i];
    }
    if (combined == 0u) return false;

    for (size_t i = 0u; i < sizeof(pending_session_grant); ++i) {
        out_grant[i] = pending_session_grant[i];
    }
    clear_bytes(pending_session_grant, sizeof(pending_session_grant));
    client_state = AURORA_IDENTITY_CLIENT_READY;
    return true;
}

bool identity_client_take_reauth_proof(
    uint8_t out_proof[AURORA_IDENTITY_SERVICE_REAUTH_PROOF_SIZE],
    uint32_t *out_purpose,
    uint64_t *out_expires_at_ms
) {
    if (out_proof == NULL || out_purpose == NULL || out_expires_at_ms == NULL ||
        client_state != AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED ||
        pending_reauth_purpose <= AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_NONE ||
        pending_reauth_purpose >= AURORA_IDENTITY_SERVICE_REAUTH_PURPOSE_COUNT ||
        pending_reauth_expires_at_ms == 0u) {
        return false;
    }

    uint8_t aggregate = 0u;
    for (size_t i = 0u; i < sizeof(pending_reauth_proof); ++i) {
        aggregate |= pending_reauth_proof[i];
    }
    if (aggregate == 0u) return false;

    for (size_t i = 0u; i < sizeof(pending_reauth_proof); ++i) {
        out_proof[i] = pending_reauth_proof[i];
    }
    *out_purpose = pending_reauth_purpose;
    *out_expires_at_ms = pending_reauth_expires_at_ms;

    clear_bytes(pending_reauth_proof, sizeof(pending_reauth_proof));
    pending_reauth_purpose = 0u;
    pending_reauth_expires_at_ms = 0u;
    client_state = AURORA_IDENTITY_CLIENT_READY;
    return true;
}

struct aurora_ipc_endpoint *identity_client_session_peer_endpoint(void) {
    if (identity_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        return NULL;
    }
    return identity_supervisor.service.supervisor_endpoint;
}
