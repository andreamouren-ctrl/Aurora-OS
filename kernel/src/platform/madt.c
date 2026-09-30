#include <stddef.h>
#include <stdint.h>

#include <aurora/acpi.h>
#include <aurora/madt.h>

struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t entries[];
} __attribute__((packed));

struct madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct madt_local_apic {
    uint8_t type;
    uint8_t length;
    uint8_t acpi_processor_uid;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct madt_io_apic {
    uint8_t type;
    uint8_t length;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;
    uint32_t gsi_base;
} __attribute__((packed));

struct madt_interrupt_override {
    uint8_t type;
    uint8_t length;
    uint8_t bus;
    uint8_t source_irq;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

struct madt_lapic_override {
    uint8_t type;
    uint8_t length;
    uint16_t reserved;
    uint64_t local_apic_address;
} __attribute__((packed));

struct madt_local_x2apic {
    uint8_t type;
    uint8_t length;
    uint16_t reserved;
    uint32_t x2apic_id;
    uint32_t flags;
    uint32_t acpi_uid;
} __attribute__((packed));

static struct aurora_cpu_info cpus[AURORA_MAX_CPUS];
static struct aurora_ioapic_info ioapics[AURORA_MAX_IOAPICS];
static struct aurora_irq_override overrides[AURORA_MAX_IRQ_OVERRIDES];

static uint32_t cpu_count;
static uint32_t ioapic_count;
static uint32_t override_count;
static uint64_t lapic_address;

static void add_cpu(
    uint32_t apic_id,
    uint32_t acpi_uid,
    uint32_t flags,
    bool x2apic
) {
    bool enabled = (flags & 0x3u) != 0;

    if (!enabled ||
        cpu_count >= AURORA_MAX_CPUS) {
        return;
    }

    cpus[cpu_count++] = (struct aurora_cpu_info) {
        .apic_id = apic_id,
        .acpi_uid = acpi_uid,
        .enabled = true,
        .x2apic = x2apic
    };
}

bool madt_init(void) {
    const struct acpi_sdt_header *header =
        acpi_find_table("APIC");

    if (header == NULL ||
        header->length < sizeof(struct acpi_madt)) {
        return false;
    }

    const struct acpi_madt *madt =
        (const struct acpi_madt *)header;

    cpu_count = 0;
    ioapic_count = 0;
    override_count = 0;
    lapic_address = madt->local_apic_address;

    const uint8_t *cursor = madt->entries;
    const uint8_t *end =
        (const uint8_t *)madt + madt->header.length;

    while (cursor + sizeof(struct madt_entry_header) <= end) {
        const struct madt_entry_header *entry =
            (const struct madt_entry_header *)cursor;

        if (entry->length < sizeof(*entry) ||
            cursor + entry->length > end) {
            return false;
        }

        switch (entry->type) {
            case 0: {
                if (entry->length >= sizeof(struct madt_local_apic)) {
                    const struct madt_local_apic *item =
                        (const struct madt_local_apic *)cursor;

                    add_cpu(
                        item->apic_id,
                        item->acpi_processor_uid,
                        item->flags,
                        false
                    );
                }
                break;
            }

            case 1: {
                if (entry->length >= sizeof(struct madt_io_apic) &&
                    ioapic_count < AURORA_MAX_IOAPICS) {
                    const struct madt_io_apic *item =
                        (const struct madt_io_apic *)cursor;

                    ioapics[ioapic_count++] =
                        (struct aurora_ioapic_info) {
                            .id = item->io_apic_id,
                            .physical_address =
                                item->io_apic_address,
                            .gsi_base = item->gsi_base
                        };
                }
                break;
            }

            case 2: {
                if (entry->length >=
                        sizeof(struct madt_interrupt_override) &&
                    override_count <
                        AURORA_MAX_IRQ_OVERRIDES) {
                    const struct madt_interrupt_override *item =
                        (const struct madt_interrupt_override *)cursor;

                    if (item->bus == 0) {
                        overrides[override_count++] =
                            (struct aurora_irq_override) {
                                .source_irq = item->source_irq,
                                .gsi = item->gsi,
                                .flags = item->flags
                            };
                    }
                }
                break;
            }

            case 5: {
                if (entry->length >= sizeof(struct madt_lapic_override)) {
                    const struct madt_lapic_override *item =
                        (const struct madt_lapic_override *)cursor;

                    lapic_address = item->local_apic_address;
                }
                break;
            }

            case 9: {
                if (entry->length >= sizeof(struct madt_local_x2apic)) {
                    const struct madt_local_x2apic *item =
                        (const struct madt_local_x2apic *)cursor;

                    add_cpu(
                        item->x2apic_id,
                        item->acpi_uid,
                        item->flags,
                        true
                    );
                }
                break;
            }

            default:
                break;
        }

        cursor += entry->length;
    }

    return lapic_address != 0 &&
        cpu_count != 0;
}

uint64_t madt_lapic_address(void) {
    return lapic_address;
}

uint32_t madt_cpu_count(void) {
    return cpu_count;
}

const struct aurora_cpu_info *madt_cpu_at(uint32_t index) {
    if (index >= cpu_count) {
        return NULL;
    }

    return &cpus[index];
}

uint32_t madt_ioapic_count(void) {
    return ioapic_count;
}

const struct aurora_ioapic_info *madt_ioapic_at(uint32_t index) {
    if (index >= ioapic_count) {
        return NULL;
    }

    return &ioapics[index];
}

uint32_t madt_irq_override_count(void) {
    return override_count;
}

const struct aurora_irq_override *madt_irq_override_at(
    uint32_t index
) {
    if (index >= override_count) {
        return NULL;
    }

    return &overrides[index];
}
