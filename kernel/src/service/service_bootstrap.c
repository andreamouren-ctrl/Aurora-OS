#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/clock.h>
#include <aurora/identity_service_probe.h>
#include <aurora/process_lifecycle_probe.h>
#include <aurora/service_bootstrap.h>
#include <aurora/usercopy.h>

static struct aurora_trusted_service identity_probe_service;

static void clear_bytes(void *buffer, size_t size) {
    uint8_t *bytes = buffer;
    if (buffer == NULL) return;
    for (size_t i = 0u; i < size; ++i) bytes[i] = 0u;
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

    struct aurora_service_startup_block startup;
    clear_bytes(&startup, sizeof(startup));
    startup.abi_version = AURORA_SERVICE_STARTUP_ABI_VERSION;
    startup.ipc_endpoint = service->service_endpoint_handle;
    startup.protected_state = service->protected_state_handle;

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

bool service_bootstrap_self_test(void) {
    const struct aurora_trusted_service_manifest manifest = {
        .name = "identity-service-probe",
        .image = identity_service_probe_image(),
        .image_size = identity_service_probe_image_size(),
        .protected_state_scope = "identity",
        .protected_state_rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE
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

    const uint8_t *expected = identity_service_ready_payload();
    struct aurora_ipc_received received;
    bool ready = false;
    uint64_t deadline = clock_now_ns() + 500000000ull;

    while (clock_now_ns() < deadline) {
        if (!ready && service_bootstrap_receive(&identity_probe_service, &received)) {
            if (received.length != AURORA_IDENTITY_SERVICE_READY_SIZE ||
                received.capability_count != 0u) {
                return false;
            }

            for (uint32_t i = 0u; i < received.length; ++i) {
                if (received.data[i] != expected[i]) return false;
            }
            ready = true;
        }

        if (ready && scheduler_thread_finished(identity_probe_service.thread)) {
            break;
        }

        arch_idle();
    }

    if (!ready || !scheduler_thread_finished(identity_probe_service.thread) ||
        identity_probe_service.process == NULL ||
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
