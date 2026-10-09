#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/identity_client.h>
#include <aurora/profile_session.h>
#include <aurora/service_supervisor.h>
#include <aurora/session_manager_client.h>
#include <aurora/session_manager_service.h>
#include <aurora/session_profile_lease.h>

#define SESSION_MANAGER_READY_TIMEOUT_NS 2000000000ull

static struct aurora_service_supervisor session_supervisor;
static struct aurora_service_bootstrap_capability session_dependencies[4];
static uint64_t session_authority_object;
static uint64_t session_audit_authority_object;
static aurora_cap_handle active_profile_handle;
static enum aurora_session_manager_client_state client_state =
    AURORA_SESSION_CLIENT_UNINITIALIZED;
static uint64_t current_request_id;
static uint64_t next_request_id = UINT64_C(0x5345535300000001);
static uint64_t active_generation;
static uint64_t observed_service_generation;
static bool terminated_requires_ready;
static bool logout_origin_locked;
static uint8_t active_user_id[AURORA_SESSION_MANAGER_USER_ID_SIZE];

static void clear_bytes(void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    if (buffer == NULL) return;
    for (size_t i = 0u; i < size; ++i) bytes[i] = 0u;
}

static void drop_active_profile(void) {
    session_profile_lease_end();

    if (active_profile_handle != AURORA_CAP_INVALID) {
        (void)cap_revoke(
            &session_supervisor.service.supervisor_caps,
            active_profile_handle);
    }
    active_profile_handle = AURORA_CAP_INVALID;
    active_generation = 0u;
    clear_bytes(active_user_id, sizeof(active_user_id));
}

static void revoke_received_capabilities(
    const struct aurora_ipc_received *received
) {
    if (received == NULL) return;
    for (uint32_t i = 0u; i < received->capability_count; ++i) {
        (void)cap_revoke(
            &session_supervisor.service.supervisor_caps,
            received->capabilities[i]);
    }
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
    uint64_t deadline = clock_now_ns() + SESSION_MANAGER_READY_TIMEOUT_NS;
    while (clock_now_ns() < deadline) {
        struct aurora_ipc_received received;
        clear_bytes(&received, sizeof(received));
        if (service_supervisor_receive(&session_supervisor, &received)) {
            const struct aurora_session_manager_message expected = {
                .version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION,
                .type = AURORA_SESSION_MANAGER_READY,
                .request_id = 0u
            };
            return received.length == sizeof(expected) &&
                received.capability_count == 0u &&
                bytes_equal(received.data, &expected, sizeof(expected));
        }

        if (session_supervisor.service.thread != 0u &&
            scheduler_thread_finished(session_supervisor.service.thread)) {
            return false;
        }

        arch_idle();
    }
    return false;
}

bool session_manager_client_init(void) {
    struct aurora_ipc_endpoint *identity_peer =
        identity_client_session_peer_endpoint();
    if (identity_peer == NULL) {
        client_state = AURORA_SESSION_CLIENT_UNAVAILABLE;
        return false;
    }

    session_authority_object = UINT64_C(0x4155524F52415345);

    session_dependencies[0].object = identity_peer;
    session_dependencies[0].type = AURORA_CAP_IPC_ENDPOINT;
    session_dependencies[0].rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE;

    session_dependencies[1].object = &session_authority_object;
    session_dependencies[1].type = AURORA_CAP_IDENTITY_SESSION;
    session_dependencies[1].rights =
        AURORA_RIGHT_CONTROL | AURORA_RIGHT_TRANSFER;

    session_dependencies[2].object = profile_root_authority();
    session_dependencies[2].type = AURORA_CAP_PROFILE_ROOT;
    session_dependencies[2].rights = AURORA_RIGHT_CONTROL;

    session_audit_authority_object = UINT64_C(0x4155444954534553);
    session_dependencies[3].object = &session_audit_authority_object;
    session_dependencies[3].type = AURORA_CAP_IDENTITY_AUDIT_EMIT;
    session_dependencies[3].rights =
        AURORA_RIGHT_CONTROL | AURORA_RIGHT_TRANSFER;

    const struct aurora_trusted_service_manifest manifest = {
        .name = "session-manager",
        .image = session_manager_service_image(),
        .image_size = session_manager_service_image_size(),
        .protected_state_scope = "session-manager",
        .protected_state_rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
        .grant_entropy_seed = false,
        .extra_capabilities = session_dependencies,
        .extra_capability_count = 4u
    };

    clear_bytes(&session_supervisor, sizeof(session_supervisor));
    clear_bytes(active_user_id, sizeof(active_user_id));
    current_request_id = 0u;
    active_generation = 0u;
    active_profile_handle = AURORA_CAP_INVALID;
    observed_service_generation = 0u;
    terminated_requires_ready = false;
    logout_origin_locked = false;

    if (!service_supervisor_init(
            &session_supervisor,
            &manifest,
            AURORA_SERVICE_RESTART_ON_FAILURE,
            2u) ||
        !service_supervisor_start(&session_supervisor) ||
        !wait_ready()) {
        client_state = AURORA_SESSION_CLIENT_UNAVAILABLE;
        return false;
    }

    observed_service_generation = session_supervisor.generation;
    client_state = AURORA_SESSION_CLIENT_READY;
    return true;
}

bool session_manager_client_begin(
    const uint8_t grant[AURORA_SESSION_MANAGER_GRANT_SIZE]
) {
    if (grant == NULL ||
        client_state != AURORA_SESSION_CLIENT_READY ||
        session_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        return false;
    }

    struct aurora_session_manager_begin_session request;
    clear_bytes(&request, sizeof(request));
    current_request_id = next_request_id++;
    if (current_request_id == 0u) current_request_id = next_request_id++;
    request.header.version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION;
    request.header.type = AURORA_SESSION_MANAGER_BEGIN_SESSION;
    request.header.request_id = current_request_id;

    uint8_t combined = 0u;
    for (size_t i = 0u; i < sizeof(request.session_grant); ++i) {
        request.session_grant[i] = grant[i];
        combined |= grant[i];
    }
    if (combined == 0u) {
        clear_bytes(&request, sizeof(request));
        current_request_id = 0u;
        return false;
    }

    bool sent = service_supervisor_send(
        &session_supervisor,
        &request,
        (uint32_t)sizeof(request));
    clear_bytes(&request, sizeof(request));

    if (!sent) {
        current_request_id = 0u;
        client_state = AURORA_SESSION_CLIENT_ERROR;
        return false;
    }

    client_state = AURORA_SESSION_CLIENT_STARTING;
    return true;
}

bool session_manager_client_lock(void) {
    if (client_state != AURORA_SESSION_CLIENT_ACTIVE ||
        active_profile_handle == AURORA_CAP_INVALID ||
        session_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        return false;
    }

    const struct aurora_session_manager_message request = {
        .version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION,
        .type = AURORA_SESSION_MANAGER_LOCK,
        .request_id = next_request_id++
    };
    if (request.request_id == 0u) return false;

    if (!service_supervisor_send(
            &session_supervisor,
            &request,
            (uint32_t)sizeof(request))) {
        session_profile_lease_end();
        client_state = AURORA_SESSION_CLIENT_ERROR;
        return false;
    }

    session_profile_lease_end();
    current_request_id = request.request_id;
    client_state = AURORA_SESSION_CLIENT_LOCKING;
    return true;
}

bool session_manager_client_unlock(
    const uint8_t grant[AURORA_SESSION_MANAGER_GRANT_SIZE]
) {
    if (grant == NULL ||
        client_state != AURORA_SESSION_CLIENT_LOCKED ||
        active_profile_handle == AURORA_CAP_INVALID ||
        session_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        return false;
    }

    struct aurora_session_manager_unlock request;
    clear_bytes(&request, sizeof(request));
    current_request_id = next_request_id++;
    if (current_request_id == 0u) current_request_id = next_request_id++;
    request.header.version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION;
    request.header.type = AURORA_SESSION_MANAGER_UNLOCK;
    request.header.request_id = current_request_id;

    uint8_t combined = 0u;
    for (size_t i = 0u; i < sizeof(request.session_grant); ++i) {
        request.session_grant[i] = grant[i];
        combined |= grant[i];
    }
    if (combined == 0u) {
        clear_bytes(&request, sizeof(request));
        current_request_id = 0u;
        return false;
    }

    bool sent = service_supervisor_send(
        &session_supervisor,
        &request,
        (uint32_t)sizeof(request));
    clear_bytes(&request, sizeof(request));

    if (!sent) {
        current_request_id = 0u;
        client_state = AURORA_SESSION_CLIENT_ERROR;
        return false;
    }

    client_state = AURORA_SESSION_CLIENT_UNLOCKING;
    return true;
}

bool session_manager_client_logout(void) {
    if ((client_state != AURORA_SESSION_CLIENT_ACTIVE &&
         client_state != AURORA_SESSION_CLIENT_LOCKED) ||
        active_profile_handle == AURORA_CAP_INVALID ||
        session_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        return false;
    }

    struct aurora_session_manager_message request = {
        .version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION,
        .type = AURORA_SESSION_MANAGER_LOGOUT,
        .request_id = next_request_id++
    };
    if (request.request_id == 0u) request.request_id = next_request_id++;

    if (!service_supervisor_send(
            &session_supervisor,
            &request,
            (uint32_t)sizeof(request))) {
        drop_active_profile();
        client_state = AURORA_SESSION_CLIENT_ERROR;
        return false;
    }

    logout_origin_locked = client_state == AURORA_SESSION_CLIENT_LOCKED;
    current_request_id = request.request_id;
    client_state = AURORA_SESSION_CLIENT_LOGGING_OUT;
    return true;
}

bool session_manager_client_terminate(void) {
    if ((client_state != AURORA_SESSION_CLIENT_ACTIVE &&
         client_state != AURORA_SESSION_CLIENT_LOCKED) ||
        active_profile_handle == AURORA_CAP_INVALID) {
        return false;
    }

    session_profile_lease_end();

    if (session_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        drop_active_profile();
        current_request_id = 0u;
        terminated_requires_ready = true;
        client_state = AURORA_SESSION_CLIENT_TERMINATED;
        return true;
    }

    struct aurora_session_manager_message request = {
        .version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION,
        .type = AURORA_SESSION_MANAGER_TERMINATE,
        .request_id = next_request_id++
    };
    if (request.request_id == 0u) request.request_id = next_request_id++;

    if (!service_supervisor_send(
            &session_supervisor,
            &request,
            (uint32_t)sizeof(request))) {
        drop_active_profile();
        current_request_id = 0u;
        client_state = AURORA_SESSION_CLIENT_TERMINATED;
        return true;
    }

    current_request_id = request.request_id;
    client_state = AURORA_SESSION_CLIENT_TERMINATING;
    return true;
}

bool session_manager_client_acknowledge_terminated(void) {
    if (client_state != AURORA_SESSION_CLIENT_TERMINATED ||
        active_profile_handle != AURORA_CAP_INVALID ||
        session_profile_lease_active()) {
        return false;
    }

    if (terminated_requires_ready) {
        if (session_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING ||
            !wait_ready()) {
            return false;
        }
        observed_service_generation = session_supervisor.generation;
    }

    terminated_requires_ready = false;
    current_request_id = 0u;
    client_state = AURORA_SESSION_CLIENT_READY;
    return true;
}

void session_manager_client_pump(void) {
    if (client_state == AURORA_SESSION_CLIENT_UNINITIALIZED) return;

    if (!service_supervisor_step(&session_supervisor)) {
        bool had_session =
            active_profile_handle != AURORA_CAP_INVALID ||
            client_state == AURORA_SESSION_CLIENT_ACTIVE ||
            client_state == AURORA_SESSION_CLIENT_LOCKED ||
            client_state == AURORA_SESSION_CLIENT_TERMINATING;
        drop_active_profile();
        if (had_session) {
            terminated_requires_ready = true;
            client_state = AURORA_SESSION_CLIENT_TERMINATED;
        } else {
            client_state = AURORA_SESSION_CLIENT_ERROR;
        }
        return;
    }

    if (session_supervisor.generation != observed_service_generation) {
        bool had_session =
            active_profile_handle != AURORA_CAP_INVALID ||
            client_state == AURORA_SESSION_CLIENT_ACTIVE ||
            client_state == AURORA_SESSION_CLIENT_LOCKED ||
            client_state == AURORA_SESSION_CLIENT_TERMINATING;
        drop_active_profile();
        observed_service_generation = session_supervisor.generation;
        if (had_session) {
            terminated_requires_ready = true;
            client_state = AURORA_SESSION_CLIENT_TERMINATED;
        } else {
            client_state = AURORA_SESSION_CLIENT_UNAVAILABLE;
        }
        return;
    }

    if (session_supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        if (client_state == AURORA_SESSION_CLIENT_STARTING ||
            client_state == AURORA_SESSION_CLIENT_ACTIVE ||
            client_state == AURORA_SESSION_CLIENT_LOCKING ||
            client_state == AURORA_SESSION_CLIENT_LOCKED ||
            client_state == AURORA_SESSION_CLIENT_UNLOCKING ||
            client_state == AURORA_SESSION_CLIENT_LOGGING_OUT ||
            client_state == AURORA_SESSION_CLIENT_TERMINATING) {
            bool had_session =
                active_profile_handle != AURORA_CAP_INVALID ||
                client_state != AURORA_SESSION_CLIENT_STARTING;
            drop_active_profile();
            if (had_session) {
                terminated_requires_ready = true;
                client_state = AURORA_SESSION_CLIENT_TERMINATED;
            } else {
                client_state = AURORA_SESSION_CLIENT_UNAVAILABLE;
            }
        }
        return;
    }

    if (client_state != AURORA_SESSION_CLIENT_STARTING &&
        client_state != AURORA_SESSION_CLIENT_LOCKING &&
        client_state != AURORA_SESSION_CLIENT_UNLOCKING &&
        client_state != AURORA_SESSION_CLIENT_LOGGING_OUT &&
        client_state != AURORA_SESSION_CLIENT_TERMINATING) {
        return;
    }

    struct aurora_ipc_received received;
    clear_bytes(&received, sizeof(received));
    if (!service_supervisor_receive(&session_supervisor, &received)) return;

    if (received.length != sizeof(struct aurora_session_manager_result)) {
        revoke_received_capabilities(&received);
        clear_bytes(&received, sizeof(received));
        client_state = AURORA_SESSION_CLIENT_ERROR;
        return;
    }

    struct aurora_session_manager_result result;
    clear_bytes(&result, sizeof(result));
    for (size_t i = 0u; i < sizeof(result); ++i) {
        ((uint8_t *)&result)[i] = received.data[i];
    }

    uint32_t expected_type =
        client_state == AURORA_SESSION_CLIENT_TERMINATING
            ? AURORA_SESSION_MANAGER_TERMINATE_RESULT
            : client_state == AURORA_SESSION_CLIENT_LOGGING_OUT
                ? AURORA_SESSION_MANAGER_LOGOUT_RESULT
            : client_state == AURORA_SESSION_CLIENT_LOCKING
                ? AURORA_SESSION_MANAGER_LOCK_RESULT
                : client_state == AURORA_SESSION_CLIENT_UNLOCKING
                    ? AURORA_SESSION_MANAGER_UNLOCK_RESULT
                    : AURORA_SESSION_MANAGER_SESSION_RESULT;

    if (result.header.version != AURORA_SESSION_MANAGER_PROTOCOL_VERSION ||
        result.header.type != expected_type ||
        result.header.request_id != current_request_id) {
        revoke_received_capabilities(&received);
        clear_bytes(&received, sizeof(received));
        clear_bytes(&result, sizeof(result));
        client_state = AURORA_SESSION_CLIENT_ERROR;
        return;
    }

    current_request_id = 0u;

    if (client_state == AURORA_SESSION_CLIENT_LOCKING) {
        if (received.capability_count == 0u &&
            result.state == AURORA_SESSION_MANAGER_STATE_ACTIVE &&
            result.public_error == AURORA_SESSION_MANAGER_ERROR_AUDIT_UNAVAILABLE &&
            result.session_generation == active_generation &&
            bytes_equal(result.user_id, active_user_id, sizeof(active_user_id))) {
            clear_bytes(&received, sizeof(received));
            clear_bytes(&result, sizeof(result));
            if (!session_profile_lease_begin(
                    &session_supervisor.service.supervisor_caps,
                    active_profile_handle,
                    active_user_id,
                    active_generation)) {
                client_state = AURORA_SESSION_CLIENT_ERROR;
                return;
            }
            client_state = AURORA_SESSION_CLIENT_ACTIVE;
            return;
        }

        if (received.capability_count != 0u ||
            result.state != AURORA_SESSION_MANAGER_STATE_LOCKED ||
            result.public_error != AURORA_SESSION_MANAGER_ERROR_NONE ||
            result.session_generation != active_generation ||
            !bytes_equal(
                result.user_id,
                active_user_id,
                sizeof(active_user_id))) {
            revoke_received_capabilities(&received);
            clear_bytes(&received, sizeof(received));
            clear_bytes(&result, sizeof(result));
            client_state = AURORA_SESSION_CLIENT_ERROR;
            return;
        }

        clear_bytes(&received, sizeof(received));
        clear_bytes(&result, sizeof(result));
        client_state = AURORA_SESSION_CLIENT_LOCKED;
        return;
    }

    if (client_state == AURORA_SESSION_CLIENT_UNLOCKING) {
        if (received.capability_count != 0u) {
            revoke_received_capabilities(&received);
            clear_bytes(&received, sizeof(received));
            clear_bytes(&result, sizeof(result));
            client_state = AURORA_SESSION_CLIENT_ERROR;
            return;
        }

        if (result.state == AURORA_SESSION_MANAGER_STATE_LOCKED &&
            result.public_error == AURORA_SESSION_MANAGER_ERROR_AUDIT_UNAVAILABLE &&
            result.session_generation == active_generation &&
            bytes_equal(result.user_id, active_user_id, sizeof(active_user_id))) {
            clear_bytes(&received, sizeof(received));
            clear_bytes(&result, sizeof(result));
            client_state = AURORA_SESSION_CLIENT_LOCKED;
            return;
        }

        if (result.state == AURORA_SESSION_MANAGER_STATE_REJECTED &&
            result.public_error ==
                AURORA_SESSION_MANAGER_ERROR_IDENTITY_REJECTED &&
            result.session_generation == active_generation) {
            clear_bytes(&received, sizeof(received));
            clear_bytes(&result, sizeof(result));
            client_state = AURORA_SESSION_CLIENT_LOCKED;
            return;
        }

        if (result.state != AURORA_SESSION_MANAGER_STATE_ACTIVE ||
            result.public_error != AURORA_SESSION_MANAGER_ERROR_NONE ||
            result.session_generation != active_generation ||
            !bytes_equal(
                result.user_id,
                active_user_id,
                sizeof(active_user_id)) ||
            !session_profile_lease_begin(
                &session_supervisor.service.supervisor_caps,
                active_profile_handle,
                active_user_id,
                active_generation)) {
            clear_bytes(&received, sizeof(received));
            clear_bytes(&result, sizeof(result));
            client_state = AURORA_SESSION_CLIENT_ERROR;
            return;
        }

        clear_bytes(&received, sizeof(received));
        clear_bytes(&result, sizeof(result));
        client_state = AURORA_SESSION_CLIENT_ACTIVE;
        return;
    }

    if (client_state == AURORA_SESSION_CLIENT_LOGGING_OUT) {
        if (received.capability_count == 0u &&
            result.public_error == AURORA_SESSION_MANAGER_ERROR_AUDIT_UNAVAILABLE &&
            result.session_generation == active_generation &&
            ((logout_origin_locked &&
              result.state == AURORA_SESSION_MANAGER_STATE_LOCKED) ||
             (!logout_origin_locked &&
              result.state == AURORA_SESSION_MANAGER_STATE_ACTIVE)) &&
            bytes_equal(result.user_id, active_user_id, sizeof(active_user_id))) {
            clear_bytes(&received, sizeof(received));
            clear_bytes(&result, sizeof(result));
            client_state = logout_origin_locked
                ? AURORA_SESSION_CLIENT_LOCKED
                : AURORA_SESSION_CLIENT_ACTIVE;
            logout_origin_locked = false;
            return;
        }

        if (received.capability_count != 0u ||
            result.state != AURORA_SESSION_MANAGER_STATE_LOGGED_OUT ||
            (result.public_error != AURORA_SESSION_MANAGER_ERROR_NONE &&
             result.public_error != AURORA_SESSION_MANAGER_ERROR_AUDIT_UNAVAILABLE)) {
            revoke_received_capabilities(&received);
            clear_bytes(&received, sizeof(received));
            clear_bytes(&result, sizeof(result));
            drop_active_profile();
            client_state = AURORA_SESSION_CLIENT_ERROR;
            return;
        }

        clear_bytes(&received, sizeof(received));
        clear_bytes(&result, sizeof(result));
        drop_active_profile();
        logout_origin_locked = false;
        client_state = AURORA_SESSION_CLIENT_READY;
        return;
    }

    if (client_state == AURORA_SESSION_CLIENT_TERMINATING) {
        if (received.capability_count != 0u ||
            result.state != AURORA_SESSION_MANAGER_STATE_LOGGED_OUT ||
            result.public_error != AURORA_SESSION_MANAGER_ERROR_NONE) {
            revoke_received_capabilities(&received);
            clear_bytes(&received, sizeof(received));
            clear_bytes(&result, sizeof(result));
            drop_active_profile();
            client_state = AURORA_SESSION_CLIENT_TERMINATED;
            return;
        }

        clear_bytes(&received, sizeof(received));
        clear_bytes(&result, sizeof(result));
        drop_active_profile();
        terminated_requires_ready = false;
        client_state = AURORA_SESSION_CLIENT_TERMINATED;
        return;
    }

    if (result.state == AURORA_SESSION_MANAGER_STATE_ACTIVE &&
        result.public_error == AURORA_SESSION_MANAGER_ERROR_NONE &&
        result.session_generation != 0u &&
        received.capability_count == 1u) {
        struct aurora_capability_view profile_view;
        aurora_cap_handle profile_handle =
            (aurora_cap_handle)received.capabilities[0];

        if (!cap_lookup(
                &session_supervisor.service.supervisor_caps,
                profile_handle,
                AURORA_CAP_FILE,
                AURORA_RIGHT_READ |
                AURORA_RIGHT_WRITE |
                AURORA_RIGHT_ENUMERATE,
                &profile_view) ||
            !profile_capability_matches_user(
                &session_supervisor.service.supervisor_caps,
                profile_handle,
                result.user_id)) {
            (void)cap_revoke(
                &session_supervisor.service.supervisor_caps,
                profile_handle);
            clear_bytes(&received, sizeof(received));
            clear_bytes(&result, sizeof(result));
            client_state = AURORA_SESSION_CLIENT_ERROR;
            return;
        }

        active_profile_handle = profile_handle;
        active_generation = result.session_generation;
        for (size_t i = 0u; i < sizeof(active_user_id); ++i) {
            active_user_id[i] = result.user_id[i];
        }

        if (!session_profile_lease_begin(
                &session_supervisor.service.supervisor_caps,
                active_profile_handle,
                active_user_id,
                active_generation)) {
            (void)cap_revoke(
                &session_supervisor.service.supervisor_caps,
                active_profile_handle);
            active_profile_handle = AURORA_CAP_INVALID;
            active_generation = 0u;
            clear_bytes(active_user_id, sizeof(active_user_id));
            clear_bytes(&received, sizeof(received));
            clear_bytes(&result, sizeof(result));
            client_state = AURORA_SESSION_CLIENT_ERROR;
            return;
        }

        clear_bytes(&received, sizeof(received));
        clear_bytes(&result, sizeof(result));
        client_state = AURORA_SESSION_CLIENT_ACTIVE;
        return;
    }

    if (received.capability_count != 0u) {
        revoke_received_capabilities(&received);
        clear_bytes(&received, sizeof(received));
        clear_bytes(&result, sizeof(result));
        client_state = AURORA_SESSION_CLIENT_ERROR;
        return;
    }

    clear_bytes(&received, sizeof(received));

    if (result.state == AURORA_SESSION_MANAGER_STATE_REJECTED) {
        clear_bytes(&result, sizeof(result));
        client_state = AURORA_SESSION_CLIENT_REJECTED;
        return;
    }

    if (result.public_error ==
        AURORA_SESSION_MANAGER_ERROR_IDENTITY_UNAVAILABLE) {
        clear_bytes(&result, sizeof(result));
        client_state = AURORA_SESSION_CLIENT_UNAVAILABLE;
        return;
    }

    clear_bytes(&result, sizeof(result));
    client_state = AURORA_SESSION_CLIENT_ERROR;
}

enum aurora_session_manager_client_state session_manager_client_state(void) {
    return client_state;
}

uint64_t session_manager_client_generation(void) {
    return active_generation;
}

const uint8_t *session_manager_client_user_id(void) {
    return (client_state == AURORA_SESSION_CLIENT_ACTIVE ||
            client_state == AURORA_SESSION_CLIENT_LOCKED ||
            client_state == AURORA_SESSION_CLIENT_LOCKING ||
            client_state == AURORA_SESSION_CLIENT_UNLOCKING)
        ? active_user_id
        : NULL;
}


aurora_cap_handle session_manager_client_profile_handle(void) {
    return (client_state == AURORA_SESSION_CLIENT_ACTIVE ||
            client_state == AURORA_SESSION_CLIENT_LOCKED ||
            client_state == AURORA_SESSION_CLIENT_LOCKING ||
            client_state == AURORA_SESSION_CLIENT_UNLOCKING)
        ? active_profile_handle
        : AURORA_CAP_INVALID;
}


aurora_cap_handle session_manager_client_delegate_profile(
    struct aurora_process *process,
    uint64_t rights
) {
    if (client_state != AURORA_SESSION_CLIENT_ACTIVE ||
        active_profile_handle == AURORA_CAP_INVALID ||
        active_generation == 0u) {
        return AURORA_CAP_INVALID;
    }

    return session_profile_lease_delegate(process, rights);
}

bool session_manager_client_revoke_profile(
    struct aurora_process *process
) {
    return session_profile_lease_revoke_process(process);
}
