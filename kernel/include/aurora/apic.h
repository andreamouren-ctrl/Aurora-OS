#ifndef AURORA_APIC_H
#define AURORA_APIC_H

#include <stdbool.h>
#include <stdint.h>

enum lapic_mode {
    LAPIC_MODE_NONE = 0,
    LAPIC_MODE_XAPIC,
    LAPIC_MODE_X2APIC
};

bool lapic_init(void);

enum lapic_mode lapic_current_mode(void);
uint32_t lapic_id(void);

void lapic_eoi(void);

bool lapic_timer_tsc_deadline_supported(void);

void lapic_timer_configure_tsc_deadline(
    uint8_t vector
);

void lapic_timer_set_tsc_deadline(
    uint64_t deadline
);

void lapic_timer_configure_oneshot(
    uint8_t vector,
    bool masked,
    uint32_t divide_configuration
);

void lapic_timer_set_initial_count(
    uint32_t count
);

uint32_t lapic_timer_current_count(void);

#endif
