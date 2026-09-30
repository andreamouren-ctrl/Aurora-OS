#include <stddef.h>
#include <stdint.h>

#include <aurora/apic.h>
#include <aurora/arch.h>
#include <aurora/boot.h>
#include <aurora/clock.h>
#include <aurora/interrupts.h>
#include <aurora/smp.h>

static struct aurora_cpu_runtime cpus[
    AURORA_MAX_SMP_CPUS
];

static uint32_t cpu_count;
static volatile uint32_t online_count;
static volatile uint32_t response_count;
static volatile uint32_t failed_count;

static void ap_entry(
    uint32_t processor_id,
    uint32_t lapic_id,
    void *context
) {
    struct aurora_cpu_runtime *cpu =
        context;

    arch_early_init();

    interrupts_load_current_cpu();

    bool ok =
        lapic_init();

    if (cpu != NULL) {
        cpu->processor_id =
            processor_id;

        cpu->lapic_id =
            lapic_id;

        __atomic_store_n(
            &cpu->state,
            ok
                ? AURORA_CPU_ONLINE
                : AURORA_CPU_FAILED,
            __ATOMIC_RELEASE
        );
    }

    if (ok) {
        __atomic_fetch_add(
            &online_count,
            1u,
            __ATOMIC_ACQ_REL
        );
    } else {
        __atomic_fetch_add(
            &failed_count,
            1u,
            __ATOMIC_ACQ_REL
        );
    }

    __atomic_fetch_add(
        &response_count,
        1u,
        __ATOMIC_ACQ_REL
    );

    /*
     * AP scheduling is deliberately not enabled yet. The AP is fully
     * initialized enough to be identified and interrupted later, but remains
     * parked until Aurora has per-CPU scheduler state.
     */
    arch_halt();
}

bool smp_init(void) {
    uint64_t reported =
        boot_smp_cpu_count();

    if (reported == 0) {
        return false;
    }

    if (reported >
        AURORA_MAX_SMP_CPUS) {
        reported =
            AURORA_MAX_SMP_CPUS;
    }

    cpu_count =
        (uint32_t)reported;

    online_count = 0;
    response_count = 0;
    failed_count = 0;

    uint32_t started_aps = 0;
    bool found_bsp = false;

    for (uint32_t i = 0;
         i < cpu_count;
         ++i) {
        struct aurora_boot_cpu boot_cpu;

        if (!boot_smp_cpu_at(
                i,
                &boot_cpu)) {
            cpus[i].state =
                AURORA_CPU_FAILED;

            ++failed_count;
            continue;
        }

        struct aurora_cpu_runtime *cpu =
            &cpus[i];

        cpu->logical_id = i;
        cpu->processor_id =
            boot_cpu.processor_id;

        cpu->lapic_id =
            boot_cpu.lapic_id;

        cpu->bootstrap =
            boot_cpu.bootstrap;

        if (boot_cpu.bootstrap) {
            cpu->state =
                AURORA_CPU_ONLINE;

            found_bsp = true;
            ++online_count;
            continue;
        }

        cpu->state =
            AURORA_CPU_STARTING;

        if (!boot_smp_start_cpu(
                i,
                ap_entry,
                cpu)) {
            cpu->state =
                AURORA_CPU_FAILED;

            ++failed_count;
            continue;
        }

        ++started_aps;
    }

    if (!found_bsp) {
        return false;
    }

    if (started_aps == 0) {
        return failed_count == 0;
    }

    uint64_t deadline =
        clock_now_ns() +
        1000000000ull;

    while (__atomic_load_n(
               &response_count,
               __ATOMIC_ACQUIRE) <
               started_aps &&
           clock_now_ns() <
               deadline) {
        __asm__ volatile ("pause");
    }

    if (__atomic_load_n(
            &response_count,
            __ATOMIC_ACQUIRE) !=
        started_aps) {
        return false;
    }

    return __atomic_load_n(
               &failed_count,
               __ATOMIC_ACQUIRE) == 0;
}

uint32_t smp_cpu_count(void) {
    return cpu_count;
}

uint32_t smp_online_cpu_count(void) {
    return __atomic_load_n(
        &online_count,
        __ATOMIC_ACQUIRE
    );
}

const struct aurora_cpu_runtime *smp_cpu_at(
    uint32_t index
) {
    if (index >= cpu_count) {
        return NULL;
    }

    return &cpus[index];
}
