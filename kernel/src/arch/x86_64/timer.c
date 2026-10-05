#include <stddef.h>
#include <stdint.h>

#include <aurora/apic.h>
#include <aurora/clock.h>
#include <aurora/cpu_local.h>
#include <aurora/interrupts.h>
#include <aurora/smp.h>
#include <aurora/timer.h>

#define LAPIC_DIVIDE_BY_16 0x3u
#define LAPIC_MIN_ONESHOT_NS 1000000ull
#define AP_BOOTSTRAP_PREEMPTION_NS 4000000ull

/* The callback is installed once by the scheduler and then read-only. */
static timer_callback_fn callback_fn;

static struct interrupt_frame *timer_interrupt(
    struct interrupt_frame *frame
) {
    struct aurora_cpu_local *cpu = cpu_local_current();

    if (cpu != NULL) {
        __atomic_fetch_add(
            &cpu->timer_interrupts,
            1ull,
            __ATOMIC_RELAXED
        );
    }

    lapic_eoi();

    /*
     * APs are timer-capable before they have performed the dedicated idle
     * stack handoff. During that narrow bootstrap phase the interrupt proves
     * local preemption delivery, but it must not enter the global context-
     * switch callback: scheduler_current_index already names the AP idle
     * thread while the processor is still physically executing on Limine's
     * AP bootstrap stack. Switching from that mismatched frame corrupts the
     * scheduler context and can reset the machine.
     *
     * Keep the AP quantum periodic here until the next SMP milestone performs
     * a real scheduler-stack handoff. The BSP continues through the normal
     * callback path unchanged.
     */
    if (cpu != NULL && cpu->bootstrap_cpu == 0u) {
        (void)timer_arm_ns(AP_BOOTSTRAP_PREEMPTION_NS);
        return frame;
    }

    if (callback_fn != NULL) {
        struct interrupt_frame *next =
            callback_fn(frame);

        if (next != NULL) {
            return next;
        }
    }

    return frame;
}

static bool calibrate_lapic_oneshot(
    struct aurora_cpu_local *cpu
) {
    if (cpu == NULL) {
        return false;
    }

    lapic_timer_configure_oneshot(
        AURORA_VECTOR_TIMER,
        true,
        LAPIC_DIVIDE_BY_16
    );

    lapic_timer_set_initial_count(
        0xFFFFFFFFu
    );

    uint64_t start_ns =
        clock_now_ns();

    clock_busy_wait_ns(
        10000000ull
    );

    uint64_t end_ns =
        clock_now_ns();

    uint32_t current =
        lapic_timer_current_count();

    uint64_t elapsed_ns =
        end_ns - start_ns;

    uint64_t elapsed_ticks =
        0xFFFFFFFFull -
        (uint64_t)current;

    if (elapsed_ns == 0 ||
        elapsed_ticks == 0) {
        return false;
    }

    __uint128_t scaled =
        (__uint128_t)elapsed_ticks *
        1000000000ull;

    cpu->timer_lapic_hz =
        (uint64_t)(
            scaled / elapsed_ns
        );

    if (cpu->timer_lapic_hz == 0) {
        return false;
    }

    lapic_timer_set_initial_count(0);

    lapic_timer_configure_oneshot(
        AURORA_VECTOR_TIMER,
        false,
        LAPIC_DIVIDE_BY_16
    );

    return true;
}

static bool timer_init_current_cpu(void) {
    struct aurora_cpu_local *cpu = cpu_local_current();

    if (cpu == NULL) {
        return false;
    }

    cpu->timer_mode = AURORA_TIMER_NONE;
    cpu->timer_lapic_hz = 0;
    __atomic_store_n(
        &cpu->timer_interrupts,
        0ull,
        __ATOMIC_RELEASE
    );

    if (lapic_timer_tsc_deadline_supported() &&
        clock_tsc_frequency_hz() != 0) {
        lapic_timer_configure_tsc_deadline(
            AURORA_VECTOR_TIMER
        );

        lapic_timer_set_tsc_deadline(0);

        cpu->timer_mode =
            AURORA_TIMER_TSC_DEADLINE;

        return true;
    }

    if (!calibrate_lapic_oneshot(cpu)) {
        cpu->timer_mode =
            AURORA_TIMER_NONE;

        return false;
    }

    cpu->timer_mode =
        AURORA_TIMER_LAPIC_ONESHOT;

    return true;
}

bool timer_init(void) {
    if (!interrupt_register_handler(
            AURORA_VECTOR_TIMER,
            timer_interrupt)) {
        return false;
    }

    callback_fn = NULL;

    if (!timer_init_current_cpu()) {
        return false;
    }

    /*
     * smp_init() has already brought APs online with IF clear. Prepare their
     * Local APIC timers now, after the shared timer vector is installed and
     * before heap/scheduler startup. smp_prepare_ap_timers() serializes that
     * calibration one AP at a time.
     */
    return smp_prepare_ap_timers();
}

bool timer_init_ap(void) {
    struct aurora_cpu_local *cpu = cpu_local_current();

    if (cpu == NULL) {
        return false;
    }

    /*
     * AP timer setup is staged before scheduler release. scheduler_start_ap()
     * still calls this API defensively; once the current CPU is prepared the
     * second call must not recalibrate or disturb its already-programmed LVT.
     */
    if (cpu->timer_mode != AURORA_TIMER_NONE) {
        return true;
    }

    return timer_init_current_cpu();
}

bool timer_arm_ns(uint64_t delay_ns) {
    struct aurora_cpu_local *cpu = cpu_local_current();

    if (cpu == NULL) {
        return false;
    }

    if (delay_ns == 0) {
        delay_ns = 1;
    }

    if (cpu->timer_mode ==
        AURORA_TIMER_TSC_DEADLINE) {
        uint64_t tsc_hz =
            clock_tsc_frequency_hz();

        if (tsc_hz == 0) {
            return false;
        }

        __uint128_t scaled =
            (__uint128_t)delay_ns *
            tsc_hz;

        uint64_t cycles =
            (uint64_t)(
                scaled / 1000000000ull
            );

        if (cycles == 0) {
            cycles = 1;
        }

        lapic_timer_set_tsc_deadline(
            clock_read_tsc() + cycles
        );

        return true;
    }

    if (cpu->timer_mode ==
        AURORA_TIMER_LAPIC_ONESHOT) {
        if (delay_ns < LAPIC_MIN_ONESHOT_NS) {
            delay_ns = LAPIC_MIN_ONESHOT_NS;
        }

        __uint128_t scaled =
            (__uint128_t)delay_ns *
            cpu->timer_lapic_hz;

        uint64_t ticks =
            (uint64_t)(
                scaled / 1000000000ull
            );

        if (ticks == 0) {
            ticks = 1;
        }

        if (ticks > 0xFFFFFFFFull) {
            ticks = 0xFFFFFFFFull;
        }

        lapic_timer_set_initial_count(
            (uint32_t)ticks
        );

        return true;
    }

    return false;
}

void timer_cancel(void) {
    struct aurora_cpu_local *cpu = cpu_local_current();

    if (cpu == NULL) {
        return;
    }

    if (cpu->timer_mode ==
        AURORA_TIMER_TSC_DEADLINE) {
        lapic_timer_set_tsc_deadline(0);
    } else if (
        cpu->timer_mode ==
        AURORA_TIMER_LAPIC_ONESHOT) {
        lapic_timer_set_initial_count(0);
    }
}

void timer_set_callback(
    timer_callback_fn callback
) {
    callback_fn = callback;
}

enum aurora_timer_mode timer_mode(void) {
    struct aurora_cpu_local *cpu = cpu_local_current();

    if (cpu == NULL) {
        return AURORA_TIMER_NONE;
    }

    return (enum aurora_timer_mode)cpu->timer_mode;
}

const char *timer_mode_name(void) {
    switch (timer_mode()) {
        case AURORA_TIMER_TSC_DEADLINE:
            return "TSC deadline";

        case AURORA_TIMER_LAPIC_ONESHOT:
            return "Local APIC one-shot";

        default:
            return "none";
    }
}

uint64_t timer_interrupt_count(void) {
    struct aurora_cpu_local *cpu = cpu_local_current();
    return cpu != NULL
        ? __atomic_load_n(&cpu->timer_interrupts, __ATOMIC_ACQUIRE)
        : 0u;
}

uint64_t timer_interrupt_count_cpu(uint32_t logical_id) {
    struct aurora_cpu_local *cpu = cpu_local_at(logical_id);
    return cpu != NULL
        ? __atomic_load_n(&cpu->timer_interrupts, __ATOMIC_ACQUIRE)
        : 0u;
}
