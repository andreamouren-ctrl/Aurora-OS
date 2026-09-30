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

#endif
