#ifndef AURORA_ACPI_H
#define AURORA_ACPI_H

#include <stdbool.h>
#include <stdint.h>

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

bool acpi_init(void);

const struct acpi_sdt_header *acpi_find_table(
    const char signature[4]
);

#endif
