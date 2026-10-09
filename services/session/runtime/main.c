#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/capability_abi.h>
#include <aurora/identity_service_protocol.h>
#include <aurora/service_abi.h>
#include <aurora/session_manager_protocol.h>
#include <aurora/syscall_abi.h>

struct session_runtime_context {
    uint64_t supervisor_endpoint;
    uint64_t identity_endpoint;
    uint64_t identity_session_authority;
    uint64_t identity_audit_authority;
    uint64_t profile_root_authority;
    uint64_t profile_handle;
    bool active;
    bool locked;
    uint64_t generation;
    uint8_t user_id[AURORA_SESSION_MANAGER_USER_ID_SIZE];
};

static void secure_zero(void *buffer, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    if (buffer == NULL) return;
    while (size != 0u) {
        *bytes++ = 0u;
        --size;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    if (destination == NULL || source == NULL) return;
    for (size_t i = 0u; i < size; ++i) dst[i] = src[i];
}

static bool bytes_all_zero(const uint8_t *bytes, size_t size) {
    if (bytes == NULL) return true;
    uint8_t combined = 0u;
    for (size_t i = 0u; i < size; ++i) combined |= bytes[i];
    return combined == 0u;
}

static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t size) {
    if (a == NULL || b == NULL) return false;
    uint8_t diff = 0u;
    for (size_t i = 0u; i < size; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0u;
}

static uint64_t syscall1(uint64_t number, uint64_t a1) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    __asm__ volatile (
        "syscall"
        : "+a"(rax)
        : "D"(rdi)
        : "rcx", "r11", "memory"
    );
    return rax;
}

static uint64_t syscall2(uint64_t number, uint64_t a1, uint64_t a2) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    __asm__ volatile (
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi)
        : "rcx", "r11", "memory"
    );
    return rax;
}

static uint64_t syscall3(
    uint64_t number,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3
) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    register uint64_t rdx __asm__("rdx") = a3;
    __asm__ volatile (
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx)
        : "rcx", "r11", "memory"
    );
    return rax;
}

static uint64_t syscall5(
    uint64_t number,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5
) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    register uint64_t rdx __asm__("rdx") = a3;
    register uint64_t r10 __asm__("r10") = a4;
    register uint64_t r8 __asm__("r8") = a5;
    __asm__ volatile (
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );
    return rax;
}

static bool capability_has(
    uint64_t handle,
    enum aurora_cap_type type,
    uint64_t rights
) {
    return handle != 0u &&
        syscall3(
            AURORA_SYS_CAP_CHECK,
            handle,
            (uint64_t)type,
            rights
        ) == 1u;
}

static bool receive_message(
    uint64_t endpoint,
    struct aurora_sys_ipc_received *received
) {
    if (endpoint == 0u || received == NULL) return false;
    secure_zero(received, sizeof(*received));
    return syscall2(
        AURORA_SYS_IPC_RECEIVE,
        endpoint,
        (uint64_t)(uintptr_t)received
    ) == 0u;
}

static bool wait_for_message(uint64_t endpoint) {
    return endpoint != 0u &&
        syscall2(AURORA_SYS_IPC_WAIT, endpoint, 0u) == 0u;
}

static bool send_payload(
    uint64_t endpoint,
    const void *payload,
    size_t size
) {
    if (endpoint == 0u || payload == NULL || size == 0u ||
        size > AURORA_SYS_IPC_PAYLOAD_MAX) {
        return false;
    }

    return syscall5(
        AURORA_SYS_IPC_SEND,
        endpoint,
        (uint64_t)(uintptr_t)payload,
        size,
        0u,
        0u
    ) == 0u;
}

static bool send_manager_message(
    uint64_t endpoint,
    uint32_t type,
    uint64_t request_id
) {
    const struct aurora_session_manager_message message = {
        .version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION,
        .type = type,
        .request_id = request_id
    };
    return send_payload(endpoint, &message, sizeof(message));
}

static bool send_manager_result(
    uint64_t endpoint,
    uint64_t request_id,
    uint32_t state,
    uint32_t public_error,
    uint64_t generation,
    const uint8_t *user_id,
    uint64_t profile_handle
) {
    struct aurora_session_manager_result result;
    secure_zero(&result, sizeof(result));
    result.header.version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION;
    result.header.type = AURORA_SESSION_MANAGER_SESSION_RESULT;
    result.header.request_id = request_id;
    result.state = state;
    result.public_error = public_error;
    result.session_generation = generation;
    if (user_id != NULL) {
        copy_bytes(result.user_id, user_id, sizeof(result.user_id));
    }

    bool sent;
    if (profile_handle != 0u) {
        const struct aurora_sys_ipc_transfer transfer = {
            .handle = profile_handle,
            .rights =
                AURORA_RIGHT_READ |
                AURORA_RIGHT_WRITE |
                AURORA_RIGHT_ENUMERATE |
                AURORA_RIGHT_TRANSFER
        };
        sent = syscall5(
            AURORA_SYS_IPC_SEND,
            endpoint,
            (uint64_t)(uintptr_t)&result,
            sizeof(result),
            (uint64_t)(uintptr_t)&transfer,
            1u
        ) == 0u;
    } else {
        sent = send_payload(endpoint, &result, sizeof(result));
    }

    secure_zero(&result, sizeof(result));
    return sent;
}

static bool send_logout_result(
    uint64_t endpoint,
    uint64_t request_id,
    uint64_t generation
) {
    struct aurora_session_manager_result result;
    secure_zero(&result, sizeof(result));
    result.header.version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION;
    result.header.type = AURORA_SESSION_MANAGER_LOGOUT_RESULT;
    result.header.request_id = request_id;
    result.state = AURORA_SESSION_MANAGER_STATE_LOGGED_OUT;
    result.public_error = AURORA_SESSION_MANAGER_ERROR_NONE;
    result.session_generation = generation;
    bool sent = send_payload(endpoint, &result, sizeof(result));
    secure_zero(&result, sizeof(result));
    return sent;
}

static bool send_lifecycle_result(
    uint64_t endpoint,
    uint32_t type,
    uint64_t request_id,
    uint32_t state,
    uint32_t public_error,
    uint64_t generation,
    const uint8_t *user_id
) {
    struct aurora_session_manager_result result;
    secure_zero(&result, sizeof(result));
    result.header.version = AURORA_SESSION_MANAGER_PROTOCOL_VERSION;
    result.header.type = type;
    result.header.request_id = request_id;
    result.state = state;
    result.public_error = public_error;
    result.session_generation = generation;
    if (user_id != NULL) {
        copy_bytes(result.user_id, user_id, sizeof(result.user_id));
    }
    bool sent = send_payload(endpoint, &result, sizeof(result));
    secure_zero(&result, sizeof(result));
    return sent;
}

static bool validate_startup(
    const struct aurora_service_startup_block *startup
) {
    if (startup == NULL ||
        startup->abi_version != AURORA_SERVICE_STARTUP_ABI_VERSION ||
        startup->flags != 0u ||
        startup->reserved != 0u ||
        startup->ipc_endpoint == 0u ||
        startup->protected_state == 0u ||
        startup->entropy_seed != 0u ||
        startup->extra_capability_count != 4u) {
        return false;
    }

    uint64_t identity_endpoint = startup->extra_capabilities[0];
    uint64_t session_authority = startup->extra_capabilities[1];
    uint64_t profile_root = startup->extra_capabilities[2];
    uint64_t audit_authority = startup->extra_capabilities[3];

    if (!capability_has(
            startup->ipc_endpoint,
            AURORA_CAP_IPC_ENDPOINT,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE) ||
        capability_has(
            startup->ipc_endpoint,
            AURORA_CAP_IPC_ENDPOINT,
            AURORA_RIGHT_TRANSFER)) {
        return false;
    }

    if (!capability_has(
            startup->protected_state,
            AURORA_CAP_PROTECTED_STATE,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE) ||
        capability_has(
            startup->protected_state,
            AURORA_CAP_PROTECTED_STATE,
            AURORA_RIGHT_TRANSFER)) {
        return false;
    }

    if (!capability_has(
            identity_endpoint,
            AURORA_CAP_IPC_ENDPOINT,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE) ||
        capability_has(
            identity_endpoint,
            AURORA_CAP_IPC_ENDPOINT,
            AURORA_RIGHT_TRANSFER)) {
        return false;
    }

    if (!capability_has(
            session_authority,
            AURORA_CAP_IDENTITY_SESSION,
            AURORA_RIGHT_CONTROL | AURORA_RIGHT_TRANSFER)) {
        return false;
    }

    if (!capability_has(
            profile_root,
            AURORA_CAP_PROFILE_ROOT,
            AURORA_RIGHT_CONTROL) ||
        capability_has(
            profile_root,
            AURORA_CAP_PROFILE_ROOT,
            AURORA_RIGHT_TRANSFER)) {
        return false;
    }

    if (!capability_has(
            audit_authority,
            AURORA_CAP_IDENTITY_AUDIT_EMIT,
            AURORA_RIGHT_CONTROL | AURORA_RIGHT_TRANSFER)) {
        return false;
    }

    return true;
}

static bool send_identity_consume(
    const struct session_runtime_context *context,
    uint64_t request_id,
    const uint8_t grant[AURORA_SESSION_MANAGER_GRANT_SIZE]
) {
    if (context == NULL || grant == NULL || request_id == 0u) return false;

    struct aurora_identity_service_consume_session_grant request;
    secure_zero(&request, sizeof(request));
    request.header.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    request.header.type = AURORA_IDENTITY_SERVICE_CONSUME_SESSION_GRANT;
    request.header.request_id = request_id;
    copy_bytes(request.session_grant, grant, sizeof(request.session_grant));

    const struct aurora_sys_ipc_transfer transfer = {
        .handle = context->identity_session_authority,
        .rights = AURORA_RIGHT_CONTROL
    };

    bool sent = syscall5(
        AURORA_SYS_IPC_SEND,
        context->identity_endpoint,
        (uint64_t)(uintptr_t)&request,
        sizeof(request),
        (uint64_t)(uintptr_t)&transfer,
        1u
    ) == 0u;

    secure_zero(&request, sizeof(request));
    return sent;
}

static bool receive_identity_result(
    const struct session_runtime_context *context,
    uint64_t request_id,
    struct aurora_identity_service_session_grant_result *out
) {
    if (context == NULL || out == NULL ||
        !wait_for_message(context->identity_endpoint)) {
        return false;
    }

    struct aurora_sys_ipc_received received;
    secure_zero(&received, sizeof(received));
    if (!receive_message(context->identity_endpoint, &received) ||
        received.capability_count != 0u ||
        received.length != sizeof(*out)) {
        secure_zero(&received, sizeof(received));
        return false;
    }

    secure_zero(out, sizeof(*out));
    copy_bytes(out, received.data, sizeof(*out));
    secure_zero(&received, sizeof(received));

    return out->header.version == AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION &&
        out->header.type == AURORA_IDENTITY_SERVICE_SESSION_GRANT_RESULT &&
        out->header.request_id == request_id;
}

static bool emit_session_audit(
    struct session_runtime_context *context,
    uint64_t request_id,
    uint32_t event,
    uint64_t generation,
    const uint8_t user_id[AURORA_SESSION_MANAGER_USER_ID_SIZE]
) {
    if (context == NULL || request_id == 0u || generation == 0u ||
        user_id == NULL || bytes_all_zero(user_id, AURORA_SESSION_MANAGER_USER_ID_SIZE)) {
        return false;
    }

    struct aurora_identity_service_audit_session_event request;
    secure_zero(&request, sizeof(request));
    request.header.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    request.header.type = AURORA_IDENTITY_SERVICE_AUDIT_SESSION_EVENT;
    request.header.request_id = request_id;
    request.event = event;
    request.session_generation = generation;
    copy_bytes(request.user_id, user_id, sizeof(request.user_id));

    const struct aurora_sys_ipc_transfer transfer = {
        .handle = context->identity_audit_authority,
        .rights = AURORA_RIGHT_CONTROL
    };

    bool sent = syscall5(
        AURORA_SYS_IPC_SEND,
        context->identity_endpoint,
        (uint64_t)(uintptr_t)&request,
        sizeof(request),
        (uint64_t)(uintptr_t)&transfer,
        1u
    ) == 0u;
    secure_zero(&request, sizeof(request));
    if (!sent || !wait_for_message(context->identity_endpoint)) return false;

    struct aurora_sys_ipc_received received;
    secure_zero(&received, sizeof(received));
    if (!receive_message(context->identity_endpoint, &received) ||
        received.capability_count != 0u ||
        received.length != sizeof(struct aurora_identity_service_audit_session_result)) {
        secure_zero(&received, sizeof(received));
        return false;
    }

    struct aurora_identity_service_audit_session_result result;
    secure_zero(&result, sizeof(result));
    copy_bytes(&result, received.data, sizeof(result));
    secure_zero(&received, sizeof(received));

    bool ok =
        result.header.version == AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION &&
        result.header.type == AURORA_IDENTITY_SERVICE_AUDIT_SESSION_RESULT &&
        result.header.request_id == request_id &&
        result.state == AURORA_IDENTITY_SERVICE_AUDIT_STATE_SUCCESS &&
        result.public_error == AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_NONE;
    secure_zero(&result, sizeof(result));
    return ok;
}

static bool begin_session(
    struct session_runtime_context *context,
    const struct aurora_session_manager_begin_session *request
) {
    if (context == NULL || request == NULL ||
        request->header.version != AURORA_SESSION_MANAGER_PROTOCOL_VERSION ||
        request->header.type != AURORA_SESSION_MANAGER_BEGIN_SESSION ||
        request->header.request_id == 0u) {
        return false;
    }

    uint64_t request_id = request->header.request_id;

    if (context->active) {
        return send_manager_result(
            context->supervisor_endpoint,
            request_id,
            AURORA_SESSION_MANAGER_STATE_BUSY,
            AURORA_SESSION_MANAGER_ERROR_BUSY,
            context->generation,
            NULL,
            0u);
    }

    uint8_t grant[AURORA_SESSION_MANAGER_GRANT_SIZE];
    copy_bytes(grant, request->session_grant, sizeof(grant));

    if (bytes_all_zero(grant, sizeof(grant))) {
        secure_zero(grant, sizeof(grant));
        return send_manager_result(
            context->supervisor_endpoint,
            request_id,
            AURORA_SESSION_MANAGER_STATE_REJECTED,
            AURORA_SESSION_MANAGER_ERROR_IDENTITY_REJECTED,
            context->generation,
            NULL,
            0u);
    }

    if (!send_identity_consume(context, request_id, grant)) {
        secure_zero(grant, sizeof(grant));
        return send_manager_result(
            context->supervisor_endpoint,
            request_id,
            AURORA_SESSION_MANAGER_STATE_SERVICE_ERROR,
            AURORA_SESSION_MANAGER_ERROR_INTERNAL_FAILURE,
            context->generation,
            NULL,
            0u);
    }
    secure_zero(grant, sizeof(grant));

    struct aurora_identity_service_session_grant_result identity_result;
    secure_zero(&identity_result, sizeof(identity_result));
    if (!receive_identity_result(context, request_id, &identity_result)) {
        secure_zero(&identity_result, sizeof(identity_result));
        return send_manager_result(
            context->supervisor_endpoint,
            request_id,
            AURORA_SESSION_MANAGER_STATE_SERVICE_ERROR,
            AURORA_SESSION_MANAGER_ERROR_INTERNAL_FAILURE,
            context->generation,
            NULL,
            0u);
    }

    if (identity_result.state ==
            AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SUCCESS &&
        identity_result.public_error ==
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_NONE &&
        !bytes_all_zero(identity_result.user_id, sizeof(identity_result.user_id))) {
        uint8_t verified_user_id[AURORA_SESSION_MANAGER_USER_ID_SIZE];
        copy_bytes(
            verified_user_id,
            identity_result.user_id,
            sizeof(verified_user_id));
        secure_zero(&identity_result, sizeof(identity_result));

        uint64_t profile_handle = syscall2(
            AURORA_SYS_PROFILE_OPEN_OR_CREATE,
            context->profile_root_authority,
            (uint64_t)(uintptr_t)verified_user_id
        );

        if (profile_handle == AURORA_SYS_RESULT_ERROR ||
            !capability_has(
                profile_handle,
                AURORA_CAP_FILE,
                AURORA_RIGHT_READ |
                AURORA_RIGHT_WRITE |
                AURORA_RIGHT_ENUMERATE |
                AURORA_RIGHT_CONTROL |
                AURORA_RIGHT_TRANSFER)) {
            secure_zero(verified_user_id, sizeof(verified_user_id));
            if (profile_handle != AURORA_SYS_RESULT_ERROR &&
                profile_handle != 0u) {
                (void)syscall1(AURORA_SYS_CAP_REVOKE, profile_handle);
            }
            return send_manager_result(
                context->supervisor_endpoint,
                request_id,
                AURORA_SESSION_MANAGER_STATE_SERVICE_ERROR,
                AURORA_SESSION_MANAGER_ERROR_INTERNAL_FAILURE,
                context->generation,
                NULL,
                0u);
        }

        uint64_t next_generation = context->generation + 1u;
        if (next_generation == 0u) next_generation = 1u;
        if (!emit_session_audit(
                context,
                request_id,
                AURORA_IDENTITY_SERVICE_AUDIT_SESSION_STARTED,
                next_generation,
                verified_user_id)) {
            (void)syscall1(AURORA_SYS_CAP_REVOKE, profile_handle);
            secure_zero(verified_user_id, sizeof(verified_user_id));
            return send_manager_result(
                context->supervisor_endpoint,
                request_id,
                AURORA_SESSION_MANAGER_STATE_SERVICE_ERROR,
                AURORA_SESSION_MANAGER_ERROR_AUDIT_UNAVAILABLE,
                context->generation,
                NULL,
                0u);
        }

        context->profile_handle = profile_handle;
        context->active = true;
        context->locked = false;
        context->generation = next_generation;
        copy_bytes(
            context->user_id,
            verified_user_id,
            sizeof(context->user_id));
        secure_zero(verified_user_id, sizeof(verified_user_id));

        bool sent = send_manager_result(
            context->supervisor_endpoint,
            request_id,
            AURORA_SESSION_MANAGER_STATE_ACTIVE,
            AURORA_SESSION_MANAGER_ERROR_NONE,
            context->generation,
            context->user_id,
            context->profile_handle);

        if (!sent) {
            (void)emit_session_audit(
                context,
                request_id,
                AURORA_IDENTITY_SERVICE_AUDIT_SESSION_TERMINATED,
                context->generation,
                context->user_id);
            (void)syscall1(AURORA_SYS_CAP_REVOKE, context->profile_handle);
            context->profile_handle = 0u;
            context->active = false;
            secure_zero(context->user_id, sizeof(context->user_id));
        }
        return sent;
    }

    uint32_t state = AURORA_SESSION_MANAGER_STATE_REJECTED;
    uint32_t error = AURORA_SESSION_MANAGER_ERROR_IDENTITY_REJECTED;
    if (identity_result.state ==
            AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SERVICE_ERROR) {
        state = AURORA_SESSION_MANAGER_STATE_SERVICE_ERROR;
        error = identity_result.public_error ==
                AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_SERVICE_UNAVAILABLE
            ? AURORA_SESSION_MANAGER_ERROR_IDENTITY_UNAVAILABLE
            : AURORA_SESSION_MANAGER_ERROR_INTERNAL_FAILURE;
    }

    secure_zero(&identity_result, sizeof(identity_result));
    return send_manager_result(
        context->supervisor_endpoint,
        request_id,
        state,
        error,
        context->generation,
        NULL,
        0u);
}

static bool lock_session(
    struct session_runtime_context *context,
    const struct aurora_session_manager_message *request
) {
    if (context == NULL || request == NULL ||
        request->version != AURORA_SESSION_MANAGER_PROTOCOL_VERSION ||
        request->type != AURORA_SESSION_MANAGER_LOCK ||
        request->request_id == 0u) {
        return false;
    }

    if (!context->active || context->locked) {
        return send_lifecycle_result(
            context->supervisor_endpoint,
            AURORA_SESSION_MANAGER_LOCK_RESULT,
            request->request_id,
            AURORA_SESSION_MANAGER_STATE_BUSY,
            AURORA_SESSION_MANAGER_ERROR_BUSY,
            context->generation,
            NULL);
    }

    if (!emit_session_audit(
            context,
            request->request_id,
            AURORA_IDENTITY_SERVICE_AUDIT_SESSION_LOCKED,
            context->generation,
            context->user_id)) {
        return send_lifecycle_result(
            context->supervisor_endpoint,
            AURORA_SESSION_MANAGER_LOCK_RESULT,
            request->request_id,
            AURORA_SESSION_MANAGER_STATE_ACTIVE,
            AURORA_SESSION_MANAGER_ERROR_AUDIT_UNAVAILABLE,
            context->generation,
            context->user_id);
    }

    context->locked = true;
    return send_lifecycle_result(
        context->supervisor_endpoint,
        AURORA_SESSION_MANAGER_LOCK_RESULT,
        request->request_id,
        AURORA_SESSION_MANAGER_STATE_LOCKED,
        AURORA_SESSION_MANAGER_ERROR_NONE,
        context->generation,
        context->user_id);
}

static bool unlock_session(
    struct session_runtime_context *context,
    const struct aurora_session_manager_unlock *request
) {
    if (context == NULL || request == NULL ||
        request->header.version != AURORA_SESSION_MANAGER_PROTOCOL_VERSION ||
        request->header.type != AURORA_SESSION_MANAGER_UNLOCK ||
        request->header.request_id == 0u) {
        return false;
    }

    if (!context->active || !context->locked) {
        return send_lifecycle_result(
            context->supervisor_endpoint,
            AURORA_SESSION_MANAGER_UNLOCK_RESULT,
            request->header.request_id,
            AURORA_SESSION_MANAGER_STATE_BUSY,
            AURORA_SESSION_MANAGER_ERROR_BUSY,
            context->generation,
            NULL);
    }

    uint8_t grant[AURORA_SESSION_MANAGER_GRANT_SIZE];
    copy_bytes(grant, request->session_grant, sizeof(grant));
    if (bytes_all_zero(grant, sizeof(grant))) {
        secure_zero(grant, sizeof(grant));
        return send_lifecycle_result(
            context->supervisor_endpoint,
            AURORA_SESSION_MANAGER_UNLOCK_RESULT,
            request->header.request_id,
            AURORA_SESSION_MANAGER_STATE_REJECTED,
            AURORA_SESSION_MANAGER_ERROR_IDENTITY_REJECTED,
            context->generation,
            NULL);
    }

    if (!send_identity_consume(context, request->header.request_id, grant)) {
        secure_zero(grant, sizeof(grant));
        return send_lifecycle_result(
            context->supervisor_endpoint,
            AURORA_SESSION_MANAGER_UNLOCK_RESULT,
            request->header.request_id,
            AURORA_SESSION_MANAGER_STATE_SERVICE_ERROR,
            AURORA_SESSION_MANAGER_ERROR_INTERNAL_FAILURE,
            context->generation,
            NULL);
    }
    secure_zero(grant, sizeof(grant));

    struct aurora_identity_service_session_grant_result identity_result;
    secure_zero(&identity_result, sizeof(identity_result));
    if (!receive_identity_result(
            context,
            request->header.request_id,
            &identity_result)) {
        secure_zero(&identity_result, sizeof(identity_result));
        return send_lifecycle_result(
            context->supervisor_endpoint,
            AURORA_SESSION_MANAGER_UNLOCK_RESULT,
            request->header.request_id,
            AURORA_SESSION_MANAGER_STATE_SERVICE_ERROR,
            AURORA_SESSION_MANAGER_ERROR_INTERNAL_FAILURE,
            context->generation,
            NULL);
    }

    bool same_user = bytes_equal(
        identity_result.user_id,
        context->user_id,
        sizeof(context->user_id));

    if (identity_result.state ==
            AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SUCCESS &&
        identity_result.public_error ==
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_NONE &&
        same_user) {
        secure_zero(&identity_result, sizeof(identity_result));
        if (!emit_session_audit(
                context,
                request->header.request_id,
                AURORA_IDENTITY_SERVICE_AUDIT_SESSION_UNLOCKED,
                context->generation,
                context->user_id)) {
            return send_lifecycle_result(
                context->supervisor_endpoint,
                AURORA_SESSION_MANAGER_UNLOCK_RESULT,
                request->header.request_id,
                AURORA_SESSION_MANAGER_STATE_LOCKED,
                AURORA_SESSION_MANAGER_ERROR_AUDIT_UNAVAILABLE,
                context->generation,
                context->user_id);
        }
        context->locked = false;
        return send_lifecycle_result(
            context->supervisor_endpoint,
            AURORA_SESSION_MANAGER_UNLOCK_RESULT,
            request->header.request_id,
            AURORA_SESSION_MANAGER_STATE_ACTIVE,
            AURORA_SESSION_MANAGER_ERROR_NONE,
            context->generation,
            context->user_id);
    }

    uint32_t state = AURORA_SESSION_MANAGER_STATE_REJECTED;
    uint32_t error = AURORA_SESSION_MANAGER_ERROR_IDENTITY_REJECTED;
    if (identity_result.state ==
            AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SERVICE_ERROR) {
        state = AURORA_SESSION_MANAGER_STATE_SERVICE_ERROR;
        error = identity_result.public_error ==
                AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_SERVICE_UNAVAILABLE
            ? AURORA_SESSION_MANAGER_ERROR_IDENTITY_UNAVAILABLE
            : AURORA_SESSION_MANAGER_ERROR_INTERNAL_FAILURE;
    }

    secure_zero(&identity_result, sizeof(identity_result));
    return send_lifecycle_result(
        context->supervisor_endpoint,
        AURORA_SESSION_MANAGER_UNLOCK_RESULT,
        request->header.request_id,
        state,
        error,
        context->generation,
        NULL);
}

static bool logout_session(
    struct session_runtime_context *context,
    const struct aurora_session_manager_message *request
) {
    if (context == NULL || request == NULL ||
        request->version != AURORA_SESSION_MANAGER_PROTOCOL_VERSION ||
        request->type != AURORA_SESSION_MANAGER_LOGOUT ||
        request->request_id == 0u) {
        return false;
    }

    if (!context->active) {
        return send_lifecycle_result(
            context->supervisor_endpoint,
            AURORA_SESSION_MANAGER_LOGOUT_RESULT,
            request->request_id,
            AURORA_SESSION_MANAGER_STATE_BUSY,
            AURORA_SESSION_MANAGER_ERROR_BUSY,
            context->generation,
            NULL);
    }

    if (!emit_session_audit(
            context,
            request->request_id,
            AURORA_IDENTITY_SERVICE_AUDIT_SESSION_LOGOUT,
            context->generation,
            context->user_id)) {
        return send_lifecycle_result(
            context->supervisor_endpoint,
            AURORA_SESSION_MANAGER_LOGOUT_RESULT,
            request->request_id,
            context->locked
                ? AURORA_SESSION_MANAGER_STATE_LOCKED
                : AURORA_SESSION_MANAGER_STATE_ACTIVE,
            AURORA_SESSION_MANAGER_ERROR_AUDIT_UNAVAILABLE,
            context->generation,
            context->user_id);
    }

    if (context->profile_handle != 0u) {
        (void)syscall1(AURORA_SYS_CAP_REVOKE, context->profile_handle);
        context->profile_handle = 0u;
    }
    context->active = false;
    context->locked = false;
    secure_zero(context->user_id, sizeof(context->user_id));
    return send_logout_result(
        context->supervisor_endpoint,
        request->request_id,
        context->generation);
}

static bool terminate_session(
    struct session_runtime_context *context,
    const struct aurora_session_manager_message *request
) {
    if (context == NULL || request == NULL ||
        request->version != AURORA_SESSION_MANAGER_PROTOCOL_VERSION ||
        request->type != AURORA_SESSION_MANAGER_TERMINATE ||
        request->request_id == 0u) {
        return false;
    }

    if (!context->active) {
        return send_lifecycle_result(
            context->supervisor_endpoint,
            AURORA_SESSION_MANAGER_TERMINATE_RESULT,
            request->request_id,
            AURORA_SESSION_MANAGER_STATE_LOGGED_OUT,
            AURORA_SESSION_MANAGER_ERROR_NONE,
            context->generation,
            NULL);
    }

    bool audited = emit_session_audit(
        context,
        request->request_id,
        AURORA_IDENTITY_SERVICE_AUDIT_SESSION_TERMINATED,
        context->generation,
        context->user_id);

    if (context->profile_handle != 0u) {
        (void)syscall1(AURORA_SYS_CAP_REVOKE, context->profile_handle);
        context->profile_handle = 0u;
    }
    context->active = false;
    context->locked = false;
    secure_zero(context->user_id, sizeof(context->user_id));

    return send_lifecycle_result(
        context->supervisor_endpoint,
        AURORA_SESSION_MANAGER_TERMINATE_RESULT,
        request->request_id,
        AURORA_SESSION_MANAGER_STATE_LOGGED_OUT,
        audited
            ? AURORA_SESSION_MANAGER_ERROR_NONE
            : AURORA_SESSION_MANAGER_ERROR_AUDIT_UNAVAILABLE,
        context->generation,
        NULL);
}

int64_t session_manager_runtime_main(uint64_t initial_rsp) {
    if (initial_rsp < AURORA_SERVICE_STARTUP_STACK_OFFSET) return 1;

    const struct aurora_service_startup_block *startup =
        (const struct aurora_service_startup_block *)(uintptr_t)(
            initial_rsp - AURORA_SERVICE_STARTUP_STACK_OFFSET);

    if (!validate_startup(startup)) return 1;

    struct session_runtime_context context;
    secure_zero(&context, sizeof(context));
    context.supervisor_endpoint = startup->ipc_endpoint;
    context.identity_endpoint = startup->extra_capabilities[0];
    context.identity_session_authority = startup->extra_capabilities[1];
    context.profile_root_authority = startup->extra_capabilities[2];
    context.identity_audit_authority = startup->extra_capabilities[3];

    if (!send_manager_message(
            context.supervisor_endpoint,
            AURORA_SESSION_MANAGER_READY,
            0u)) {
        secure_zero(&context, sizeof(context));
        return 1;
    }

    for (;;) {
        if (!wait_for_message(context.supervisor_endpoint)) {
            secure_zero(&context, sizeof(context));
            return 1;
        }

        struct aurora_sys_ipc_received received;
        secure_zero(&received, sizeof(received));
        if (!receive_message(context.supervisor_endpoint, &received)) {
            secure_zero(&context, sizeof(context));
            return 1;
        }

        if (received.capability_count != 0u ||
            received.length < sizeof(struct aurora_session_manager_message)) {
            secure_zero(&received, sizeof(received));
            if (!send_manager_message(
                    context.supervisor_endpoint,
                    AURORA_SESSION_MANAGER_ERROR,
                    0u)) {
                secure_zero(&context, sizeof(context));
                return 1;
            }
            continue;
        }

        struct aurora_session_manager_message header;
        secure_zero(&header, sizeof(header));
        copy_bytes(&header, received.data, sizeof(header));

        if (header.version != AURORA_SESSION_MANAGER_PROTOCOL_VERSION) {
            uint64_t request_id = header.request_id;
            secure_zero(&received, sizeof(received));
            secure_zero(&header, sizeof(header));
            if (!send_manager_message(
                    context.supervisor_endpoint,
                    AURORA_SESSION_MANAGER_ERROR,
                    request_id)) {
                secure_zero(&context, sizeof(context));
                return 1;
            }
            continue;
        }

        if (header.type == AURORA_SESSION_MANAGER_BEGIN_SESSION &&
            received.length == sizeof(struct aurora_session_manager_begin_session)) {
            struct aurora_session_manager_begin_session request;
            secure_zero(&request, sizeof(request));
            copy_bytes(&request, received.data, sizeof(request));
            secure_zero(&received, sizeof(received));
            secure_zero(&header, sizeof(header));
            bool ok = begin_session(&context, &request);
            secure_zero(&request, sizeof(request));
            if (!ok) {
                secure_zero(&context, sizeof(context));
                return 1;
            }
            continue;
        }

        if (header.type == AURORA_SESSION_MANAGER_LOCK &&
            received.length == sizeof(struct aurora_session_manager_message)) {
            secure_zero(&received, sizeof(received));
            bool ok = lock_session(&context, &header);
            secure_zero(&header, sizeof(header));
            if (!ok) {
                secure_zero(&context, sizeof(context));
                return 1;
            }
            continue;
        }

        if (header.type == AURORA_SESSION_MANAGER_UNLOCK &&
            received.length == sizeof(struct aurora_session_manager_unlock)) {
            struct aurora_session_manager_unlock request;
            secure_zero(&request, sizeof(request));
            copy_bytes(&request, received.data, sizeof(request));
            secure_zero(&received, sizeof(received));
            secure_zero(&header, sizeof(header));
            bool ok = unlock_session(&context, &request);
            secure_zero(&request, sizeof(request));
            if (!ok) {
                secure_zero(&context, sizeof(context));
                return 1;
            }
            continue;
        }

        if (header.type == AURORA_SESSION_MANAGER_LOGOUT &&
            received.length == sizeof(struct aurora_session_manager_message)) {
            secure_zero(&received, sizeof(received));
            bool ok = logout_session(&context, &header);
            secure_zero(&header, sizeof(header));
            if (!ok) {
                secure_zero(&context, sizeof(context));
                return 1;
            }
            continue;
        }

        if (header.type == AURORA_SESSION_MANAGER_TERMINATE &&
            received.length == sizeof(struct aurora_session_manager_message)) {
            secure_zero(&received, sizeof(received));
            bool ok = terminate_session(&context, &header);
            secure_zero(&header, sizeof(header));
            if (!ok) {
                secure_zero(&context, sizeof(context));
                return 1;
            }
            continue;
        }

        if (header.type == AURORA_SESSION_MANAGER_SHUTDOWN &&
            received.length == sizeof(struct aurora_session_manager_message)) {
            uint64_t request_id = header.request_id;
            secure_zero(&received, sizeof(received));
            secure_zero(&header, sizeof(header));
            bool sent = send_manager_message(
                context.supervisor_endpoint,
                AURORA_SESSION_MANAGER_SHUTDOWN_ACK,
                request_id);
            secure_zero(&context, sizeof(context));
            return sent ? 0 : 1;
        }

        uint64_t request_id = header.request_id;
        secure_zero(&received, sizeof(received));
        secure_zero(&header, sizeof(header));
        if (!send_manager_message(
                context.supervisor_endpoint,
                AURORA_SESSION_MANAGER_ERROR,
                request_id)) {
            secure_zero(&context, sizeof(context));
            return 1;
        }
    }
}
