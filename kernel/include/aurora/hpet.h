#ifndef AURORA_HPET_H
#define AURORA_HPET_H

#include <stdbool.h>
#include <stdint.h>

bool hpet_init(void);

uint64_t hpet_counter(void);
uint64_t hpet_frequency_hz(void);
uint64_t hpet_ticks_to_ns(uint64_t ticks);

#endif
