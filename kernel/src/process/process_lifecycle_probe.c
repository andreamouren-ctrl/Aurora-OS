#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/clock.h>
#include <aurora/pmm.h>
#include <aurora/process.h>
#include <aurora/process_lifecycle_probe.h>
#include <aurora/scheduler.h>
#include <aurora/user_probe.h>
#include <aurora/vmm.h>

#define PROCESS_LIFECYCLE_REUSE_CYCLES 80u
#define PROCESS_LIFECYCLE_TIMEOUT_NS   500000000ull
#define PROCESS_ANON_TEST_BYTES        9000u

static bool page_is_zero(uint64_t physical) {
    const uint8_t *bytes = (const uint8_t *)pmm_phys_to_virt(physical);
    if (bytes == NULL) return false;
    for (size_t i = 0u; i < (size_t)AURORA_PAGE_SIZE; ++i) {
        if (bytes[i] != 0u) return false;
    }
    return true;
}

static bool exercise_anonymous_memory(struct aurora_process *process) {
    if (process == NULL) return false;

    uint64_t first = 0u;
    if (!process_map_anonymous(process, PROCESS_ANON_TEST_BYTES, &first) ||
        first < AURORA_USER_ANON_BASE ||
        process_anonymous_page_count(process) != 3u) {
        return false;
    }

    for (uint32_t page = 0u; page < 3u; ++page) {
        uint64_t physical = 0u;
        if (!vmm_translate_in(
                &process->address_space,
                first + (uint64_t)page * AURORA_PAGE_SIZE,
                &physical) ||
            !page_is_zero(physical)) {
            return false;
        }
    }

    uint64_t first_physical = 0u;
    if (!vmm_translate_in(&process->address_space, first, &first_physical)) {
        return false;
    }
    ((uint8_t *)pmm_phys_to_virt(first_physical))[0] = 0xA5u;

    if (process_unmap_anonymous(process, first + AURORA_PAGE_SIZE) ||
        !process_unmap_anonymous(process, first) ||
        process_anonymous_page_count(process) != 0u) {
        return false;
    }

    uint64_t reused = 0u;
    if (!process_map_anonymous(process, PROCESS_ANON_TEST_BYTES, &reused) ||
        reused != first ||
        process_anonymous_page_count(process) != 3u) {
        return false;
    }

    uint64_t reused_physical = 0u;
    if (!vmm_translate_in(&process->address_space, reused, &reused_physical) ||
        !page_is_zero(reused_physical)) {
        return false;
    }

    uint64_t rejected = 0u;
    size_t over_quota =
        ((size_t)AURORA_PROCESS_ANON_MAX_PAGES + 1u) * (size_t)AURORA_PAGE_SIZE;
    if (process_map_anonymous(process, over_quota, &rejected) || rejected != 0u) {
        return false;
    }

    /* Leave the second mapping live: process_reap() must reclaim it. */
    return true;
}

static bool run_one_lifecycle(void) {
    struct aurora_process *process = process_create_image(
        "lifecycle-probe",
        user_probe_image(),
        user_probe_image_size()
    );
    if (process == NULL || !exercise_anonymous_memory(process)) return false;

    aurora_thread_id thread = scheduler_create_user_thread(
        "lifecycle-probe-main",
        process
    );
    if (thread == 0u) return false;

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
        process_anonymous_page_count(process) != 0u) {
        return false;
    }

    return process_release(process);
}

bool process_lifecycle_self_test(void) {
    /* Warm the reusable heap/page-table ranges before taking the PMM baseline. */
    if (!run_one_lifecycle()) return false;

    struct pmm_stats baseline = pmm_get_stats();

    for (uint32_t cycle = 0u; cycle < PROCESS_LIFECYCLE_REUSE_CYCLES; ++cycle) {
        if (!run_one_lifecycle()) return false;
    }

    struct pmm_stats after = pmm_get_stats();
    return after.total_pages == baseline.total_pages &&
        after.free_pages == baseline.free_pages &&
        after.allocated_pages == baseline.allocated_pages;
}
