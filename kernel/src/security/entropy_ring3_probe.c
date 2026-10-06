#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/entropy.h>
#include <aurora/entropy_ring3_probe.h>
#include <aurora/entropy_user_probe.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/usercopy.h>

#define ENTROPY_RING3_TEST_TIMEOUT_NS 500000000ull
#define ENTROPY_RING3_TEST_SEED_SIZE 32u

static uint64_t entropy_seed_authority;

static bool any_nonzero(const uint8_t *buffer, size_t size) {
    uint8_t combined = 0u;
    if (buffer == NULL) return false;
    for (size_t i = 0u; i < size; ++i) combined |= buffer[i];
    return combined != 0u;
}

bool entropy_ring3_self_test(void) {
    if (!entropy_ready()) return false;

    entropy_seed_authority = 0x4155524F5241454Eull;

    struct aurora_process *process = process_create_image(
        "ring3-entropy-probe",
        entropy_user_probe_image(),
        entropy_user_probe_image_size()
    );
    if (process == NULL) return false;

    aurora_cap_handle handle = cap_grant(
        &process->capabilities,
        &entropy_seed_authority,
        AURORA_CAP_ENTROPY,
        AURORA_RIGHT_READ
    );
    if (handle == AURORA_CAP_INVALID) return false;

    struct aurora_capability_view view;
    if (!cap_lookup(
            &process->capabilities,
            handle,
            AURORA_CAP_ENTROPY,
            AURORA_RIGHT_READ,
            &view) ||
        cap_lookup(
            &process->capabilities,
            handle,
            AURORA_CAP_ENTROPY,
            AURORA_RIGHT_WRITE,
            &view) ||
        cap_lookup(
            &process->capabilities,
            handle,
            AURORA_CAP_ENTROPY,
            AURORA_RIGHT_TRANSFER,
            &view)) {
        return false;
    }

    uint64_t handle_value = (uint64_t)handle;
    if (!copy_to_user(
            process,
            process->user_stack_top - 8ull,
            &handle_value,
            sizeof(handle_value))) {
        return false;
    }

    aurora_thread_id thread = scheduler_create_user_thread(
        "ring3-entropy-probe-main",
        process
    );
    if (thread == 0u) return false;

    uint64_t deadline = clock_now_ns() + ENTROPY_RING3_TEST_TIMEOUT_NS;
    while (!scheduler_thread_finished(thread) && clock_now_ns() < deadline) {
        arch_idle();
    }

    if (!scheduler_thread_finished(thread) ||
        process_state(process) != AURORA_PROCESS_EXITED ||
        process->exit_code != 0) {
        return false;
    }

    uint8_t seed[ENTROPY_RING3_TEST_SEED_SIZE] = {0};
    if (!copy_from_user(
            process,
            seed,
            process->user_stack_top - 64ull,
            sizeof(seed)) ||
        !any_nonzero(seed, sizeof(seed))) {
        return false;
    }

    for (size_t i = 0u; i < sizeof(seed); ++i) seed[i] = 0u;

    if (!scheduler_reap_thread(thread) ||
        process_live_thread_count(process) != 0u ||
        !process_reap(process, NULL) ||
        !process_release(process)) {
        return false;
    }

    return true;
}
