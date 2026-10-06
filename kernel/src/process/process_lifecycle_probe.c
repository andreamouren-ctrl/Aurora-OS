#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/clock.h>
#include <aurora/pmm.h>
#include <aurora/process.h>
#include <aurora/process_lifecycle_probe.h>
#include <aurora/scheduler.h>
#include <aurora/user_probe.h>

#define PROCESS_LIFECYCLE_REUSE_CYCLES 96u
#define PROCESS_LIFECYCLE_TIMEOUT_NS   500000000ull

static bool run_one_lifecycle(void) {
    struct aurora_process *process = process_create_image(
        "lifecycle-probe",
        user_probe_image(),
        user_probe_image_size()
    );
    if (process == NULL) return false;

    aurora_thread_id thread = scheduler_create_user_thread(
        "lifecycle-probe-main",
        process
    );
    if (thread == 0u || process_live_thread_count(process) != 1u) return false;

    uint64_t deadline = clock_now_ns() + PROCESS_LIFECYCLE_TIMEOUT_NS;
    while ((process_bootstrap_signal(process) != AURORA_USER_PROBE_MAGIC ||
            !scheduler_thread_finished(thread)) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }

    if (process_bootstrap_signal(process) != AURORA_USER_PROBE_MAGIC ||
        !scheduler_thread_finished(thread) ||
        process_state(process) != AURORA_PROCESS_EXITED ||
        process->exit_code != 0) {
        return false;
    }

    if (!scheduler_reap_thread(thread) ||
        process_live_thread_count(process) != 0u) {
        return false;
    }

    struct aurora_process_result result;
    if (!process_reap(process, &result) ||
        result.terminal_state != AURORA_PROCESS_EXITED ||
        result.exit_code != 0 ||
        result.fault_vector != 0u ||
        process_state(process) != AURORA_PROCESS_REAPED) {
        return false;
    }

    return process_release(process);
}

bool process_lifecycle_self_test(void) {
    /* Warm the reusable heap ranges before taking the PMM baseline. */
    if (!run_one_lifecycle()) return false;

    struct pmm_stats baseline = pmm_get_stats();

    for (uint32_t cycle = 0u; cycle < PROCESS_LIFECYCLE_REUSE_CYCLES; ++cycle) {
        if (!run_one_lifecycle()) return false;

        struct pmm_stats current = pmm_get_stats();
        if (current.total_pages != baseline.total_pages ||
            current.free_pages != baseline.free_pages ||
            current.allocated_pages != baseline.allocated_pages) {
            return false;
        }
    }

    return true;
}
