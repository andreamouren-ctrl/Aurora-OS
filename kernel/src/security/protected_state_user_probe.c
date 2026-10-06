#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/process.h>
#include <aurora/protected_state.h>
#include <aurora/protected_state_user_probe.h>
#include <aurora/scheduler.h>
#include <aurora/user_protected_state_probe.h>
#include <aurora/usercopy.h>
#include <aurora/vfs.h>

#define PROBE_HANDLE_OFFSET   8ull
#define PROBE_RECORD_OFFSET  64ull
#define PROBE_MISSING_OFFSET 80ull
#define PROBE_PAYLOAD_OFFSET 112ull
#define PROBE_READBACK_OFFSET 128ull
#define PROBE_READBACK_SIZE 32u

static struct aurora_protected_state_namespace ring3_probe_state;
static struct aurora_cap_table ring3_probe_kernel_caps;

static const char probe_record_name[] = "ring3-record";
static const char probe_missing_name[] = "missing";
static const uint8_t probe_payload[] = {
    'A', 'U', 'R', 'O', 'R', 'A', '-', 'P',
    'S', '-', 'R', 'I', 'N', 'G', '3', '!'
};

static bool bytes_equal(const uint8_t *left, const uint8_t *right, size_t length) {
    for (size_t i = 0u; i < length; ++i) {
        if (left[i] != right[i]) return false;
    }
    return true;
}

static bool clean_probe_record(aurora_cap_handle kernel_handle) {
    uint8_t scratch[PROBE_READBACK_SIZE];
    size_t length = 0u;
    enum aurora_protected_state_read_result read_result =
        protected_state_read_record(
            &ring3_probe_kernel_caps,
            kernel_handle,
            &ring3_probe_state,
            probe_record_name,
            scratch,
            sizeof(scratch),
            &length);

    if (read_result == AURORA_PROTECTED_STATE_READ_NOT_FOUND) return true;
    if (read_result != AURORA_PROTECTED_STATE_READ_OK) return false;
    return protected_state_remove(
        &ring3_probe_kernel_caps,
        kernel_handle,
        &ring3_probe_state,
        probe_record_name);
}

bool protected_state_ring3_self_test(void) {
    if (!protected_state_namespace_init(&ring3_probe_state, "ring3-bridge") ||
        !protected_state_prepare(&ring3_probe_state)) {
        return false;
    }

    cap_table_init(&ring3_probe_kernel_caps);
    aurora_cap_handle kernel_handle = protected_state_grant(
        &ring3_probe_kernel_caps,
        &ring3_probe_state,
        AURORA_RIGHT_READ | AURORA_RIGHT_WRITE | AURORA_RIGHT_CONTROL);
    if (kernel_handle == AURORA_CAP_INVALID || !clean_probe_record(kernel_handle)) {
        return false;
    }

    struct aurora_process *process = process_create_image(
        "ring3-protected-state-probe",
        user_protected_state_probe_image(),
        user_protected_state_probe_image_size());
    if (process == NULL) return false;

    aurora_cap_handle user_handle = protected_state_grant(
        &process->capabilities,
        &ring3_probe_state,
        AURORA_RIGHT_READ | AURORA_RIGHT_WRITE);
    if (user_handle == AURORA_CAP_INVALID) return false;

    uint64_t handle_value = (uint64_t)user_handle;
    uint8_t readback_zero[PROBE_READBACK_SIZE] = {0};
    uint64_t stack_top = process->user_stack_top;

    if (!copy_to_user(
            process,
            stack_top - PROBE_HANDLE_OFFSET,
            &handle_value,
            sizeof(handle_value)) ||
        !copy_to_user(
            process,
            stack_top - PROBE_RECORD_OFFSET,
            probe_record_name,
            sizeof(probe_record_name) - 1u) ||
        !copy_to_user(
            process,
            stack_top - PROBE_MISSING_OFFSET,
            probe_missing_name,
            sizeof(probe_missing_name) - 1u) ||
        !copy_to_user(
            process,
            stack_top - PROBE_PAYLOAD_OFFSET,
            probe_payload,
            sizeof(probe_payload)) ||
        !copy_to_user(
            process,
            stack_top - PROBE_READBACK_OFFSET,
            readback_zero,
            sizeof(readback_zero))) {
        return false;
    }

    aurora_thread_id thread = scheduler_create_user_thread(
        "ring3-protected-state-probe-main",
        process);
    if (thread == 0u) return false;

    uint64_t deadline = clock_now_ns() + 2000000000ull;
    while ((process_bootstrap_signal(process) !=
                AURORA_USER_PROTECTED_STATE_PROBE_MAGIC ||
            !scheduler_thread_finished(thread)) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }

    if (process_bootstrap_signal(process) !=
            AURORA_USER_PROTECTED_STATE_PROBE_MAGIC ||
        !scheduler_thread_finished(thread) ||
        process_state(process) != AURORA_PROCESS_EXITED) {
        (void)clean_probe_record(kernel_handle);
        return false;
    }

    uint8_t readback[sizeof(probe_payload)];
    if (!copy_from_user(
            process,
            readback,
            stack_top - PROBE_READBACK_OFFSET,
            sizeof(readback)) ||
        !bytes_equal(readback, probe_payload, sizeof(readback))) {
        (void)clean_probe_record(kernel_handle);
        return false;
    }

    if (!clean_probe_record(kernel_handle)) return false;

    if (!cap_revoke(&process->capabilities, user_handle) ||
        !cap_revoke(&ring3_probe_kernel_caps, kernel_handle)) {
        return false;
    }

    if (!vfs_remove(ring3_probe_state.root) || !vfs_sync("/system")) {
        return false;
    }

    return true;
}
