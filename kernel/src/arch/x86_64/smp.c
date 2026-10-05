#include <stddef.h>
#include <stdint.h>

#include <aurora/apic.h>
#include <aurora/arch.h>
#include <aurora/boot.h>
#include <aurora/clock.h>
#include <aurora/cpu_local.h>
#include <aurora/interrupts.h>
#include <aurora/gdt.h>
#include <aurora/scheduler.h>
#include <aurora/smp.h>
#include <aurora/syscall.h>
#include <aurora/vmm.h>

static struct aurora_cpu_runtime cpus[
    AURORA_MAX_SMP_CPUS
];

static uint32_t cpu_count;
static volatile uint32_t online_count;
static volatile uint32_t response_count;
static volatile uint32_t failed_count;
static volatile uint32_t scheduler_ap_count;
static volatile bool scheduler_release;

static void ap_fail(struct aurora_cpu_runtime *cpu) {
    if (cpu != NULL) {
        __atomic_store_n(
            &cpu->state,
            AURORA_CPU_FAILED,
            __ATOMIC_RELEASE
        );
    }

    __atomic_fetch_add(
        &failed_count,
        1u,
        __ATOMIC_ACQ_REL
    );

    __atomic_fetch_add(
        &response_count,
        1u,
        __ATOMIC_ACQ_REL
    );

    arch_halt();
}

static void ap_scheduler_fail(struct aurora_cpu_runtime *cpu) {
    if (cpu != NULL) {
        __atomic_store_n(
            &cpu->state,
            AURORA_CPU_FAILED,
            __ATOMIC_RELEASE
        );
    }

    __atomic_fetch_add(
        &failed_count,
        1u,
        __ATOMIC_ACQ_REL
    );

    arch_halt();
}

static void ap_entry(
    uint32_t processor_id,
    uint32_t lapic_id_value,
    void *context
) {
    struct aurora_cpu_runtime *cpu = context;

    arch_early_init();

    if (cpu == NULL ||
        !cpu_local_activate_current(cpu->logical_id) ||
        !gdt_init_ap(cpu->logical_id)) {
        ap_fail(cpu);
    }

    interrupts_load_current_cpu();
    (void)arch_enable_hardening();

    bool ok = lapic_init();

    if (ok) {
        cpu->processor_id = processor_id;
        cpu->lapic_id = lapic_id_value;

        struct aurora_cpu_local *local =
            cpu_local_at(cpu->logical_id);

        ok = local != NULL &&
            local->lapic_id == lapic_id_value;
    }

    if (ok) {
        cpu_local_set_current_space(vmm_kernel_space());
        ok = cpu_local_current_space() == vmm_kernel_space() &&
            syscall_init();
    }

    __atomic_store_n(
        &cpu->state,
        ok ? AURORA_CPU_ONLINE : AURORA_CPU_FAILED,
        __ATOMIC_RELEASE
    );

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

    if (!ok) {
        arch_halt();
    }

    /*
     * The AP is fully initialized but cannot enter scheduler ownership until
     * the BSP has initialized the heap/scheduler and assigned this CPU a
     * dedicated idle thread. Keep interrupts disabled while waiting so no
     * interrupt path can observe a half-started scheduler state.
     */
    while (!__atomic_load_n(&scheduler_release, __ATOMIC_ACQUIRE)) {
        __asm__ volatile ("pause");
    }

    if (!scheduler_start_ap()) {
        ap_scheduler_fail(cpu);
    }

    __atomic_fetch_add(
        &scheduler_ap_count,
        1u,
        __ATOMIC_ACQ_REL
    );

    for (;;) {
        arch_idle();
    }
}

bool smp_init(void) {
    uint64_t reported = boot_smp_cpu_count();

    if (reported == 0) {
        return false;
    }

    if (reported > AURORA_MAX_SMP_CPUS) {
        reported = AURORA_MAX_SMP_CPUS;
    }

    cpu_count = (uint32_t)reported;

    online_count = 0;
    response_count = 0;
    failed_count = 0;
    scheduler_ap_count = 0;
    scheduler_release = false;

    bool found_bsp = false;

    /*
     * First discover/register every CPU before any AP is released. This makes
     * LAPIC-id -> CPU-local lookup complete and deterministic on every CPU.
     */
    for (uint32_t i = 0; i < cpu_count; ++i) {
        struct aurora_boot_cpu boot_cpu;

        if (!boot_smp_cpu_at(i, &boot_cpu)) {
            cpus[i].state = AURORA_CPU_FAILED;
            __atomic_fetch_add(&failed_count, 1u, __ATOMIC_ACQ_REL);
            continue;
        }

        struct aurora_cpu_runtime *cpu = &cpus[i];
        cpu->logical_id = i;
        cpu->processor_id = boot_cpu.processor_id;
        cpu->lapic_id = boot_cpu.lapic_id;
        cpu->bootstrap = boot_cpu.bootstrap;
        cpu->state = boot_cpu.bootstrap
            ? AURORA_CPU_ONLINE
            : AURORA_CPU_OFFLINE;

        if (!cpu_local_register(i, boot_cpu.lapic_id, boot_cpu.bootstrap)) {
            cpu->state = AURORA_CPU_FAILED;
            __atomic_fetch_add(&failed_count, 1u, __ATOMIC_ACQ_REL);
            continue;
        }

        if (boot_cpu.bootstrap) {
            found_bsp = true;
            __atomic_fetch_add(&online_count, 1u, __ATOMIC_ACQ_REL);
        }
    }

    if (!found_bsp || failed_count != 0u) {
        return false;
    }

    cpu_local_enable_apic_lookup();

    if (cpu_local_current() == NULL ||
        cpu_local_current_space() != vmm_kernel_space()) {
        return false;
    }

    uint32_t started_aps = 0;

    for (uint32_t i = 0; i < cpu_count; ++i) {
        struct aurora_cpu_runtime *cpu = &cpus[i];

        if (cpu->bootstrap) {
            continue;
        }

        if (cpu->state == AURORA_CPU_FAILED) {
            return false;
        }

        cpu->state = AURORA_CPU_STARTING;

        if (!boot_smp_start_cpu(i, ap_entry, cpu)) {
            cpu->state = AURORA_CPU_FAILED;
            __atomic_fetch_add(&failed_count, 1u, __ATOMIC_ACQ_REL);
            continue;
        }

        ++started_aps;
    }

    if (started_aps == 0) {
        return failed_count == 0;
    }

    uint64_t deadline = clock_now_ns() + 1000000000ull;

    while (__atomic_load_n(&response_count, __ATOMIC_ACQUIRE) < started_aps &&
           clock_now_ns() < deadline) {
        __asm__ volatile ("pause");
    }

    if (__atomic_load_n(&response_count, __ATOMIC_ACQUIRE) != started_aps) {
        return false;
    }

    return __atomic_load_n(&failed_count, __ATOMIC_ACQUIRE) == 0;
}

void smp_release_scheduler_aps(void) {
    __atomic_store_n(&scheduler_release, true, __ATOMIC_RELEASE);
}

uint32_t smp_scheduler_owned_cpu_count(void) {
    uint32_t aps = __atomic_load_n(
        &scheduler_ap_count,
        __ATOMIC_ACQUIRE
    );

    return cpu_count == 0u ? 0u : 1u + aps;
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
