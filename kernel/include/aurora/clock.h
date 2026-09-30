#ifndef AURORA_CLOCK_H
#define AURORA_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

enum aurora_clock_source {
    AURORA_CLOCK_NONE = 0,
    AURORA_CLOCK_HPET,
    AURORA_CLOCK_TSC
};

bool clock_init(void);

uint64_t clock_now_ns(void);
void clock_busy_wait_ns(uint64_t duration_ns);

enum aurora_clock_source clock_source(void);
const char *clock_source_name(void);

uint64_t clock_tsc_frequency_hz(void);
uint64_t clock_read_tsc(void);

#endif
