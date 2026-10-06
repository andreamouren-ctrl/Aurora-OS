#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/clock.h>
#include <aurora/identity_service_probe.h>
#include <aurora/pmm.h>
#include <aurora/service_supervisor.h>

#define SERVICE_SUPERVISOR_TEST_TIMEOUT_NS 500000000ull

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

static bool supervisor_manifest_valid(
    const struct aurora_trusted_service_manifest *manifest
) {
    return manifest != NULL &&
        manifest->name != NULL &&
        manifest->image != NULL &&
        manifest->image_size != 0u &&
        manifest->protected_state_scope != NULL &&
        manifest->protected_state_rights != 0u &&
        (manifest->protected_state_rights & ~AURORA_PROTECTED_STATE_RIGHTS) == 0u;
}

static bool start_instance(struct aurora_service_supervisor *supervisor) {
    if (supervisor == NULL) return false;

    if (!service_bootstrap_start_trusted(
            &supervisor->manifest,
            &supervisor->service)) {
        supervisor->state = AURORA_SERVICE_SUPERVISOR_FAILED;
        return false;
    }

    ++supervisor->generation;
    supervisor->state = AURORA_SERVICE_SUPERVISOR_RUNNING;
    return true;
}

static bool reap_terminal_instance(
    struct aurora_service_supervisor *supervisor
) {
    if (supervisor == NULL ||
        supervisor->state != AURORA_SERVICE_SUPERVISOR_RUNNING ||
        supervisor->service.process == NULL ||
        supervisor->service.thread == 0u) {
        return false;
    }

    struct aurora_process *process = supervisor->service.process;
    enum aurora_process_state terminal_state = process_state(process);
    if (terminal_state != AURORA_PROCESS_EXITED &&
        terminal_state != AURORA_PROCESS_FAULTED) {
        return false;
    }

    if (!scheduler_reap_thread(supervisor->service.thread) ||
        process_live_thread_count(process) != 0u) {
        return false;
    }

    struct aurora_process_result result;
    if (!process_reap(process, &result)) return false;

    if (!process_release(process)) return false;

    clear_bytes(&supervisor->service, sizeof(supervisor->service));
    supervisor->last_result = result;
    supervisor->has_last_result = true;
    supervisor->state = result.terminal_state == AURORA_PROCESS_FAULTED
        ? AURORA_SERVICE_SUPERVISOR_FAULTED
        : AURORA_SERVICE_SUPERVISOR_EXITED;
    return true;
}

static bool restart_allowed(const struct aurora_service_supervisor *supervisor) {
    if (supervisor == NULL || !supervisor->has_last_result) return false;

    if (supervisor->restart_policy == AURORA_SERVICE_RESTART_ALWAYS) return true;
    if (supervisor->restart_policy != AURORA_SERVICE_RESTART_ON_FAILURE) return false;

    return supervisor->last_result.terminal_state == AURORA_PROCESS_FAULTED ||
        (supervisor->last_result.terminal_state == AURORA_PROCESS_EXITED &&
         supervisor->last_result.exit_code != 0);
}

bool service_supervisor_init(
    struct aurora_service_supervisor *supervisor,
    const struct aurora_trusted_service_manifest *manifest,
    enum aurora_service_restart_policy restart_policy,
    uint32_t restart_limit
) {
    if (supervisor == NULL || !supervisor_manifest_valid(manifest) ||
        restart_policy > AURORA_SERVICE_RESTART_ALWAYS) {
        return false;
    }

    clear_bytes(supervisor, sizeof(*supervisor));
    supervisor->manifest = *manifest;
    supervisor->restart_policy = restart_policy;
    supervisor->restart_limit = restart_limit;
    supervisor->state = AURORA_SERVICE_SUPERVISOR_STOPPED;
    return true;
}

bool service_supervisor_start(struct aurora_service_supervisor *supervisor) {
    if (supervisor == NULL ||
        supervisor->state != AURORA_SERVICE_SUPERVISOR_STOPPED ||
        supervisor->generation != 0u) {
        return false;
    }

    return start_instance(supervisor);
}

bool service_supervisor_step(struct aurora_service_supervisor *supervisor) {
    if (supervisor == NULL) return false;

    if (supervisor->state != AURORA_SERVICE_SUPERVISOR_RUNNING) return true;
    if (supervisor->service.thread == 0u || supervisor->service.process == NULL) {
        supervisor->state = AURORA_SERVICE_SUPERVISOR_FAILED;
        return false;
    }

    if (!scheduler_thread_finished(supervisor->service.thread)) return true;

    if (!reap_terminal_instance(supervisor)) {
        supervisor->state = AURORA_SERVICE_SUPERVISOR_FAILED;
        return false;
    }

    if (!restart_allowed(supervisor)) return true;
    if (supervisor->restart_count >= supervisor->restart_limit) return true;

    ++supervisor->restart_count;
    return start_instance(supervisor);
}

bool service_supervisor_send(
    struct aurora_service_supervisor *supervisor,
    const void *data,
    uint32_t length
) {
    if (supervisor == NULL ||
        supervisor->state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        return false;
    }

    return service_bootstrap_send(&supervisor->service, data, length);
}

bool service_supervisor_receive(
    struct aurora_service_supervisor *supervisor,
    struct aurora_ipc_received *out
) {
    if (supervisor == NULL || out == NULL ||
        supervisor->state != AURORA_SERVICE_SUPERVISOR_RUNNING) {
        return false;
    }

    return service_bootstrap_receive(&supervisor->service, out);
}

static bool verify_identity_instance_authority(
    struct aurora_service_supervisor *supervisor
) {
    if (supervisor == NULL ||
        supervisor->state != AURORA_SERVICE_SUPERVISOR_RUNNING ||
        supervisor->service.process == NULL) {
        return false;
    }

    struct aurora_capability_view view;
    if (!cap_lookup(
            &supervisor->service.process->capabilities,
            supervisor->service.protected_state_handle,
            AURORA_CAP_PROTECTED_STATE,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
            &view)) {
        return false;
    }

    if (cap_lookup(
            &supervisor->service.process->capabilities,
            supervisor->service.protected_state_handle,
            AURORA_CAP_PROTECTED_STATE,
            AURORA_RIGHT_CONTROL,
            &view) ||
        cap_lookup(
            &supervisor->service.process->capabilities,
            supervisor->service.protected_state_handle,
            AURORA_CAP_PROTECTED_STATE,
            AURORA_RIGHT_TRANSFER,
            &view)) {
        return false;
    }

    return cap_lookup(
        &supervisor->service.process->capabilities,
        supervisor->service.service_endpoint_handle,
        AURORA_CAP_IPC_ENDPOINT,
        AURORA_RIGHT_READ | AURORA_RIGHT_WRITE,
        &view
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

    return bytes_equal(received->data, &expected, sizeof(expected));
}

static bool send_identity_message(
    struct aurora_service_supervisor *supervisor,
    uint32_t type,
    uint64_t request_id
) {
    const struct aurora_identity_service_message message = {
        .version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION,
        .type = type,
        .request_id = request_id
    };

    return service_supervisor_send(supervisor, &message, sizeof(message));
}

static bool wait_for_identity_message(
    struct aurora_service_supervisor *supervisor,
    uint32_t type,
    uint64_t request_id
) {
    if (supervisor == NULL) return false;

    uint64_t deadline = clock_now_ns() + SERVICE_SUPERVISOR_TEST_TIMEOUT_NS;
    while (clock_now_ns() < deadline) {
        struct aurora_ipc_received received;
        if (service_supervisor_receive(supervisor, &received)) {
            return identity_message_matches(&received, type, request_id);
        }

        if (supervisor->service.thread != 0u &&
            scheduler_thread_finished(supervisor->service.thread)) {
            return false;
        }

        arch_idle();
    }

    return false;
}

static bool wait_for_identity_blocked(
    struct aurora_service_supervisor *supervisor
) {
    if (supervisor == NULL || supervisor->service.thread == 0u) return false;

    uint64_t deadline = clock_now_ns() + SERVICE_SUPERVISOR_TEST_TIMEOUT_NS;
    while (!scheduler_thread_blocked(supervisor->service.thread) &&
           !scheduler_thread_finished(supervisor->service.thread) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }

    return scheduler_thread_blocked(supervisor->service.thread) &&
        !scheduler_thread_finished(supervisor->service.thread);
}

static bool wait_for_terminal(struct aurora_service_supervisor *supervisor) {
    if (supervisor == NULL || supervisor->service.thread == 0u) return false;

    uint64_t deadline = clock_now_ns() + SERVICE_SUPERVISOR_TEST_TIMEOUT_NS;
    while (!scheduler_thread_finished(supervisor->service.thread) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }

    return scheduler_thread_finished(supervisor->service.thread);
}

static bool exercise_identity_request_loop(
    struct aurora_service_supervisor *supervisor,
    uint64_t ping_request_id,
    uint64_t shutdown_request_id
) {
    return wait_for_identity_message(
            supervisor,
            AURORA_IDENTITY_SERVICE_READY,
            0u) &&
        wait_for_identity_blocked(supervisor) &&
        send_identity_message(
            supervisor,
            AURORA_IDENTITY_SERVICE_PING,
            ping_request_id) &&
        wait_for_identity_message(
            supervisor,
            AURORA_IDENTITY_SERVICE_PONG,
            ping_request_id) &&
        wait_for_identity_blocked(supervisor) &&
        send_identity_message(
            supervisor,
            AURORA_IDENTITY_SERVICE_SHUTDOWN,
            shutdown_request_id) &&
        wait_for_identity_message(
            supervisor,
            AURORA_IDENTITY_SERVICE_SHUTDOWN_ACK,
            shutdown_request_id) &&
        wait_for_terminal(supervisor);
}

bool service_supervisor_self_test(void) {
    const struct aurora_trusted_service_manifest manifest = {
        .name = "identity-supervisor-probe",
        .image = identity_service_probe_image(),
        .image_size = identity_service_probe_image_size(),
        .protected_state_scope = "identity",
        .protected_state_rights = AURORA_RIGHT_READ | AURORA_RIGHT_WRITE
    };

    struct aurora_service_supervisor supervisor;
    if (!service_supervisor_init(
            &supervisor,
            &manifest,
            AURORA_SERVICE_RESTART_ALWAYS,
            1u)) {
        return false;
    }

    struct pmm_stats baseline = pmm_get_stats();

    if (!service_supervisor_start(&supervisor) ||
        supervisor.generation != 1u ||
        supervisor.restart_count != 0u ||
        !verify_identity_instance_authority(&supervisor)) {
        return false;
    }

    aurora_process_id first_process_id = supervisor.service.process->id;
    if (!exercise_identity_request_loop(
            &supervisor,
            0x53555050494E4701ull,
            0x5355505348555401ull)) {
        return false;
    }

    if (!service_supervisor_step(&supervisor) ||
        supervisor.state != AURORA_SERVICE_SUPERVISOR_RUNNING ||
        supervisor.generation != 2u ||
        supervisor.restart_count != 1u ||
        supervisor.service.process == NULL ||
        supervisor.service.process->id == first_process_id ||
        !supervisor.has_last_result ||
        supervisor.last_result.id != first_process_id ||
        supervisor.last_result.terminal_state != AURORA_PROCESS_EXITED ||
        supervisor.last_result.exit_code != 0 ||
        !verify_identity_instance_authority(&supervisor)) {
        return false;
    }

    aurora_process_id second_process_id = supervisor.service.process->id;
    if (!exercise_identity_request_loop(
            &supervisor,
            0x53555050494E4702ull,
            0x5355505348555402ull)) {
        return false;
    }

    if (!service_supervisor_step(&supervisor) ||
        supervisor.state != AURORA_SERVICE_SUPERVISOR_EXITED ||
        supervisor.generation != 2u ||
        supervisor.restart_count != 1u ||
        !supervisor.has_last_result ||
        supervisor.last_result.id != second_process_id ||
        supervisor.last_result.terminal_state != AURORA_PROCESS_EXITED ||
        supervisor.last_result.exit_code != 0 ||
        supervisor.service.process != NULL ||
        supervisor.service.thread != 0u) {
        return false;
    }

    struct pmm_stats after = pmm_get_stats();
    return after.total_pages == baseline.total_pages &&
        after.free_pages == baseline.free_pages &&
        after.allocated_pages == baseline.allocated_pages;
}
