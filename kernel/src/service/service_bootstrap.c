#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/clock.h>
#include <aurora/identity_service_probe.h>
#include <aurora/process_lifecycle_probe.h>
#include <aurora/service_bootstrap.h>
#include <aurora/usercopy.h>

#define SERVICE_BOOTSTRAP_TEST_TIMEOUT_NS 500000000ull

static struct aurora_trusted_service identity_probe_service;
static uint64_t entropy_seed_authority;

static void clear_bytes(void *buffer, size_t size) {
    uint8_t *bytes = buffer;
    if (buffer == NULL) return;
    for (size_t i = 0u; i < size; ++i) bytes[i] = 0u;
}

static bool bytes_equal(const void *left, const void *right, size_t length) {
    const uint8_t *a = left;
    const uint8_t *b = right;
    if (left == NULL || right == NULL) return false;
    for (size_t i = 0u; i < length; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static bool manifest_valid(
    const struct aurora_trusted_service_manifest *manifest
) {
    if (manifest == NULL || manifest->name == NULL ||
        manifest->image == NULL || manifest->image_size == 0u ||
        manifest->protected_state_scope == NULL) {
        return false;
    }

    if ((manifest->protected_state_rights & ~AURORA_PROTECTED_STATE_RIGHTS) != 0u ||
        manifest->protected_state_rights == 0u) {
        return false;
    }

    return true;
}

bool service_bootstrap_start_trusted(
    const struct aurora_trusted_service_manifest *manifest,
    struct aurora_trusted_service *service
) {
    if (!manifest_valid(manifest) || service == NULL) return false;

    clear_bytes(service, sizeof(*service));

    if (!protected_state_namespace_init(
            &service->protected_state,
            manifest->protected_state_scope) ||
        !protected_state_prepare(&service->protected_state)) {
        return false;
    }

    ipc_channel_init(&service->ipc_channel);
    cap_table_init(&service->supervisor_caps);

    service->service_endpoint =
        ipc_channel_endpoint(&service->ipc_channel, 0u);
    service->supervisor_endpoint =
        ipc_channel_endpoint(&service->ipc_channel, 1u);

    if (service->service_endpoint == NULL ||
        service->supervisor_endpoint == NULL) {
        return false;
    }

    service->process = process_create_image(
        manifest->name,
        manifest->image,
        manifest->image_size
    );

    if (service->process == NULL) return false;

    service->service_endpoint_handle = cap_grant(
        &service->process->capabilities,
        service->service_endpoint,
        AURORA_CAP_IPC_ENDPOINT,
        AURORA_RIGHT_READ | AURORA_RIGHT_WRITE
    );

    if (service->service_endpoint_handle == AURORA_CAP_INVALID) return false;

    service->protected_state_handle = protected_state_grant(
        &service->process->capabilities,
        &service->protected_state,
        manifest->protected_state_rights
    );

    if (service->protected_state_handle == AURORA_CAP_INVALID) return false;

    if (manifest->grant_entropy_seed) {
        entropy_seed_authority = 0x4155524F5241454Eull;
        service->entropy_seed_handle = cap_grant(
            &service->process->capabilities,
            &entropy_seed_authority,
            AURORA_CAP_ENTROPY,
            AURORA_RIGHT_READ
        );
        if (service->entropy_seed_handle == AURORA_CAP_INVALID) return false;
    }

    struct aurora_service_startup_block startup;
    clear_bytes(&startup, sizeof(startup));
    startup.abi_version = AURORA_SERVICE_STARTUP_ABI_VERSION;
    startup.ipc_endpoint = service->service_endpoint_handle;
    startup.protected_state = service->protected_state_handle;
    startup.entropy_seed = service->entropy_seed_handle;

    if (sizeof(startup) > AURORA_SERVICE_STARTUP_STACK_OFFSET ||
        service->process->user_stack_top < AURORA_SERVICE_STARTUP_STACK_OFFSET) {
        return false;
    }

    uint64_t startup_address =
        service->process->user_stack_top - AURORA_SERVICE_STARTUP_STACK_OFFSET;

    if (!copy_to_user(
            service->process,
            startup_address,
            &startup,
            sizeof(startup))) {
        return false;
    }

    service->thread = scheduler_create_user_thread(
        manifest->name,
        service->process
    );

    if (service->thread == 0u) return false;

    service->started = true;
    return true;
}

bool service_bootstrap_send(
    struct aurora_trusted_service *service,
    const void *data,
    uint32_t length
) {
    if (service == NULL || !service->started ||
        service->supervisor_endpoint == NULL ||
        length > AURORA_IPC_PAYLOAD_MAX ||
        (length != 0u && data == NULL)) {
        return false;
    }

    return ipc_send(
        service->supervisor_endpoint,
        &service->supervisor_caps,
        data,
        length,
        NULL,
        0u
    );
}

bool service_bootstrap_receive(
    struct aurora_trusted_service *service,
    struct aurora_ipc_received *out
) {
    if (service == NULL || !service->started ||
        service->supervisor_endpoint == NULL || out == NULL) {
        return false;
    }

    return ipc_receive(
        service->supervisor_endpoint,
        &service->supervisor_caps,
        out
    );
}

static bool identity_message_matches(
    const struct aurora_ipc_received *received,
    uint32_t type,
    uint64_t request_id
) {
    if (received == NULL ||
        received->length != AURORA_IDENTITY_SERVICE_MESSAGE_SIZE ||
        received->capability_count != 0u) {
        return false;
    }

    const struct aurora_identity_service_message expected = {
        .version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION,
        .type = type,
        .request_id = request_id
    };

    return bytes_equal(
        received->data,
        &expected,
        sizeof(expected)
    );
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

static bool wait_for_identity_message(
    struct aurora_trusted_service *service,
    uint32_t type,
    uint64_t request_id
) {
    if (service == NULL) return false;

    uint64_t deadline = clock_now_ns() + SERVICE_BOOTSTRAP_TEST_TIMEOUT_NS;
    while (clock_now_ns() < deadline) {
        struct aurora_ipc_received received;
        if (service_bootstrap_receive(service, &received)) {
            return identity_message_matches(&received, type, request_id);
        }

        if (service->thread != 0u && scheduler_thread_finished(service->thread)) {
            return false;
        }

        arch_idle();
    }

    return false;
}

static bool wait_for_service_blocked(struct aurora_trusted_service *service) {
    if (service == NULL || service->thread == 0u) return false;

    uint64_t deadline = clock_now_ns() + SERVICE_BOOTSTRAP_TEST_TIMEOUT_NS;
    while (!scheduler_thread_blocked(service->thread) &&
           !scheduler_thread_finished(service->thread) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }

    return scheduler_thread_blocked(service->thread) &&
        !scheduler_thread_finished(service->thread);
}

static bool wait_for_service_finished(struct aurora_trusted_service *service) {
    if (service == NULL || service->thread == 0u) return false;

    uint64_t deadline = clock_now_ns() + SERVICE_BOOTSTRAP_TEST_TIMEOUT_NS;
    while (!scheduler_thread_finished(service->thread) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }

    return scheduler_thread_finished(service->thread);
}

bool service_bootstrap_self_test(void) {
    const struct aurora_trusted_service_manifest manifest = {
        .name = "identity-service-probe",
        .image = identity_service_probe_image(),
        .image_size = identity_service_probe_image_size(),
        .protected_state_scope = "identity",
        .protected_state_rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
        .grant_entropy_seed = true
    };

    if (!service_bootstrap_start_trusted(&manifest, &identity_probe_service)) {
        return false;
    }

    struct aurora_capability_view view;
    if (!cap_lookup(
            &identity_probe_service.process->capabilities,
            identity_probe_service.protected_state_handle,
            AURORA_CAP_PROTECTED_STATE,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
            &view)) {
        return false;
    }
    if (cap_lookup(
            &identity_probe_service.process->capabilities,
            identity_probe_service.protected_state_handle,
            AURORA_CAP_PROTECTED_STATE,
            AURORA_RIGHT_CONTROL,
            &view)) {
        return false;
    }
    if (cap_lookup(
            &identity_probe_service.process->capabilities,
            identity_probe_service.protected_state_handle,
            AURORA_CAP_PROTECTED_STATE,
            AURORA_RIGHT_TRANSFER,
            &view)) {
        return false;
    }

    if (!cap_lookup(
            &identity_probe_service.process->capabilities,
            identity_probe_service.entropy_seed_handle,
            AURORA_CAP_ENTROPY,
            AURORA_RIGHT_READ,
            &view) ||
        cap_lookup(
            &identity_probe_service.process->capabilities,
            identity_probe_service.entropy_seed_handle,
            AURORA_CAP_ENTROPY,
            AURORA_RIGHT_WRITE,
            &view) ||
        cap_lookup(
            &identity_probe_service.process->capabilities,
            identity_probe_service.entropy_seed_handle,
            AURORA_CAP_ENTROPY,
            AURORA_RIGHT_TRANSFER,
            &view)) {
        return false;
    }

    const uint64_t ping_request_id = 0x494450494E470001ull;
    const uint64_t shutdown_request_id = 0x4944534855540001ull;

    if (!wait_for_identity_message(
            &identity_probe_service,
            AURORA_IDENTITY_SERVICE_READY,
            0u) ||
        !wait_for_service_blocked(&identity_probe_service) ||
        !send_identity_message(
            &identity_probe_service,
            AURORA_IDENTITY_SERVICE_PING,
            ping_request_id) ||
        !wait_for_identity_message(
            &identity_probe_service,
            AURORA_IDENTITY_SERVICE_PONG,
            ping_request_id) ||
        !wait_for_service_blocked(&identity_probe_service) ||
        !send_identity_message(
            &identity_probe_service,
            AURORA_IDENTITY_SERVICE_SHUTDOWN,
            shutdown_request_id) ||
        !wait_for_identity_message(
            &identity_probe_service,
            AURORA_IDENTITY_SERVICE_SHUTDOWN_ACK,
            shutdown_request_id) ||
        !wait_for_service_finished(&identity_probe_service)) {
        return false;
    }

    if (identity_probe_service.process == NULL ||
        process_state(identity_probe_service.process) != AURORA_PROCESS_EXITED ||
        identity_probe_service.process->exit_code != 0) {
        return false;
    }

    struct aurora_process *probe_process = identity_probe_service.process;
    aurora_thread_id probe_thread = identity_probe_service.thread;

    if (!scheduler_reap_thread(probe_thread) ||
        process_live_thread_count(probe_process) != 0u ||
        !process_reap(probe_process, NULL) ||
        !process_release(probe_process)) {
        return false;
    }

    clear_bytes(&identity_probe_service, sizeof(identity_probe_service));
    return process_lifecycle_self_test();
}
