#include <stddef.h>
#include <stdint.h>

#include <aurora/clock.h>
#include <aurora/hpet.h>

static enum aurora_clock_source active_source;

static uint64_t tsc_frequency_hz;
static uint64_t tsc_base;
static uint64_t tsc_base_ns;

static uint64_t hpet_base_ticks;

static void cpuid(
    uint32_t leaf,
    uint32_t subleaf,
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    __asm__ volatile (
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(subleaf)
    );

    if (eax != NULL) *eax = a;
    if (ebx != NULL) *ebx = b;
    if (ecx != NULL) *ecx = c;
    if (edx != NULL) *edx = d;
}

uint64_t clock_read_tsc(void) {
    uint32_t low;
    uint32_t high;

    /*
     * LFENCE provides sufficient ordering for the timing role used by the
     * bootstrap clock on x86-64.
     */
    __asm__ volatile (
        "lfence\n\t"
        "rdtsc"
        : "=a"(low), "=d"(high)
        :
        : "memory"
    );

    return ((uint64_t)high << 32) | low;
}

static bool invariant_tsc_supported(void) {
    uint32_t max_extended = 0;

    cpuid(
        0x80000000u,
        0,
        &max_extended,
        NULL,
        NULL,
        NULL
    );

    if (max_extended < 0x80000007u) {
        return false;
    }

    uint32_t edx = 0;

    cpuid(
        0x80000007u,
        0,
        NULL,
        NULL,
        NULL,
        &edx
    );

    return (edx & (1u << 8)) != 0;
}

static uint64_t cpuid_tsc_frequency(void) {
    uint32_t max_basic = 0;

    cpuid(
        0,
        0,
        &max_basic,
        NULL,
        NULL,
        NULL
    );

    if (max_basic >= 0x15u) {
        uint32_t denominator = 0;
        uint32_t numerator = 0;
        uint32_t crystal_hz = 0;

        cpuid(
            0x15u,
            0,
            &denominator,
            &numerator,
            &crystal_hz,
            NULL
        );

        if (denominator != 0 &&
            numerator != 0 &&
            crystal_hz != 0) {
            __uint128_t value =
                (__uint128_t)crystal_hz *
                numerator;

            return (uint64_t)(
                value / denominator
            );
        }
    }

    if (max_basic >= 0x16u) {
        uint32_t base_mhz = 0;

        cpuid(
            0x16u,
            0,
            &base_mhz,
            NULL,
            NULL,
            NULL
        );

        if (base_mhz != 0) {
            return
                (uint64_t)base_mhz *
                1000000ull;
        }
    }

    return 0;
}

static uint64_t calibrate_tsc_with_hpet(void) {
    uint64_t hpet_hz =
        hpet_frequency_hz();

    if (hpet_hz == 0) {
        return 0;
    }

    uint64_t calibration_ticks =
        hpet_hz / 50ull;

    if (calibration_ticks == 0) {
        calibration_ticks = 1;
    }

    uint64_t hpet_start =
        hpet_counter();

    uint64_t tsc_start =
        clock_read_tsc();

    while ((hpet_counter() - hpet_start) <
           calibration_ticks) {
        __asm__ volatile ("pause");
    }

    uint64_t hpet_end =
        hpet_counter();

    uint64_t tsc_end =
        clock_read_tsc();

    uint64_t elapsed_hpet =
        hpet_end - hpet_start;

    uint64_t elapsed_tsc =
        tsc_end - tsc_start;

    if (elapsed_hpet == 0 ||
        elapsed_tsc == 0) {
        return 0;
    }

    __uint128_t scaled =
        (__uint128_t)elapsed_tsc *
        hpet_hz;

    return (uint64_t)(
        scaled / elapsed_hpet
    );
}

bool clock_init(void) {
    bool have_hpet =
        hpet_init();

    bool invariant_tsc =
        invariant_tsc_supported();

    if (invariant_tsc) {
        if (have_hpet) {
            tsc_frequency_hz =
                calibrate_tsc_with_hpet();
        }

        if (tsc_frequency_hz == 0) {
            tsc_frequency_hz =
                cpuid_tsc_frequency();
        }
    }

    if (invariant_tsc &&
        tsc_frequency_hz != 0) {
        if (have_hpet) {
            hpet_base_ticks =
                hpet_counter();

            tsc_base_ns =
                hpet_ticks_to_ns(
                    hpet_base_ticks
                );
        } else {
            tsc_base_ns = 0;
        }

        tsc_base =
            clock_read_tsc();

        active_source =
            AURORA_CLOCK_TSC;

        return true;
    }

    if (have_hpet) {
        hpet_base_ticks =
            hpet_counter();

        active_source =
            AURORA_CLOCK_HPET;

        return true;
    }

    active_source =
        AURORA_CLOCK_NONE;

    return false;
}

uint64_t clock_now_ns(void) {
    if (active_source == AURORA_CLOCK_TSC) {
        uint64_t elapsed =
            clock_read_tsc() - tsc_base;

        __uint128_t ns =
            (__uint128_t)elapsed *
            1000000000ull;

        return tsc_base_ns +
            (uint64_t)(
                ns / tsc_frequency_hz
            );
    }

    if (active_source == AURORA_CLOCK_HPET) {
        uint64_t elapsed =
            hpet_counter() -
            hpet_base_ticks;

        return hpet_ticks_to_ns(elapsed);
    }

    return 0;
}

void clock_busy_wait_ns(uint64_t duration_ns) {
    uint64_t start = clock_now_ns();

    while ((clock_now_ns() - start) <
           duration_ns) {
        __asm__ volatile ("pause");
    }
}

enum aurora_clock_source clock_source(void) {
    return active_source;
}

const char *clock_source_name(void) {
    switch (active_source) {
        case AURORA_CLOCK_TSC:
            return "invariant TSC";

        case AURORA_CLOCK_HPET:
            return "HPET";

        default:
            return "none";
    }
}

uint64_t clock_tsc_frequency_hz(void) {
    return tsc_frequency_hz;
}
