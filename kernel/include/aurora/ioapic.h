#ifndef AURORA_IOAPIC_H
#define AURORA_IOAPIC_H

#include <stdbool.h>
#include <stdint.h>

bool ioapic_init(void);

bool ioapic_route_gsi(
    uint32_t gsi,
    uint8_t vector,
    uint32_t destination_apic_id,
    bool active_low,
    bool level_triggered
);

bool ioapic_route_legacy_irq(
    uint8_t irq,
    uint8_t vector,
    uint32_t destination_apic_id
);

#endif
