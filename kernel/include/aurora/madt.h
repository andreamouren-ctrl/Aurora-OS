#ifndef AURORA_MADT_H
#define AURORA_MADT_H

#include <stdbool.h>
#include <stdint.h>

#define AURORA_MAX_CPUS 256u
#define AURORA_MAX_IOAPICS 16u
#define AURORA_MAX_IRQ_OVERRIDES 32u

struct aurora_cpu_info {
    uint32_t apic_id;
    uint32_t acpi_uid;
    bool enabled;
    bool x2apic;
};

struct aurora_ioapic_info {
    uint8_t id;
    uint32_t physical_address;
    uint32_t gsi_base;
};

struct aurora_irq_override {
    uint8_t source_irq;
    uint32_t gsi;
    uint16_t flags;
};

bool madt_init(void);

uint64_t madt_lapic_address(void);

uint32_t madt_cpu_count(void);
const struct aurora_cpu_info *madt_cpu_at(uint32_t index);

uint32_t madt_ioapic_count(void);
const struct aurora_ioapic_info *madt_ioapic_at(uint32_t index);

uint32_t madt_irq_override_count(void);
const struct aurora_irq_override *madt_irq_override_at(
    uint32_t index
);

#endif
