#include <stddef.h>
#include <stdint.h>

#include <aurora/acpi.h>
#include <aurora/boot.h>
#include <aurora/pmm.h>

struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;

    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

static const struct acpi_rsdp *rsdp;
static const struct acpi_sdt_header *root_table;
static bool root_is_xsdt;

static bool bytes_equal(
    const char *left,
    const char *right,
    size_t length
) {
    for (size_t i = 0; i < length; ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }

    return true;
}

static bool checksum_ok(
    const void *address,
    size_t length
) {
    const uint8_t *bytes = address;
    uint8_t sum = 0;

    for (size_t i = 0; i < length; ++i) {
        sum = (uint8_t)(sum + bytes[i]);
    }

    return sum == 0;
}

static const void *phys_to_ptr(uint64_t physical) {
    if (physical == 0) {
        return NULL;
    }

    return pmm_phys_to_virt(physical);
}

static bool sdt_valid(
    const struct acpi_sdt_header *table
) {
    if (table == NULL ||
        table->length < sizeof(*table)) {
        return false;
    }

    return checksum_ok(table, table->length);
}

bool acpi_init(void) {
    void *address = NULL;

    if (!boot_get_rsdp(&address) ||
        address == NULL) {
        return false;
    }

    rsdp = address;

    if (!bytes_equal(rsdp->signature, "RSD PTR ", 8) ||
        !checksum_ok(rsdp, 20)) {
        return false;
    }

    uint64_t root_physical = rsdp->rsdt_address;
    root_is_xsdt = false;

    if (rsdp->revision >= 2) {
        if (rsdp->length < sizeof(struct acpi_rsdp) ||
            !checksum_ok(rsdp, rsdp->length)) {
            return false;
        }

        if (rsdp->xsdt_address != 0) {
            root_physical = rsdp->xsdt_address;
            root_is_xsdt = true;
        }
    }

    root_table = phys_to_ptr(root_physical);

    if (!sdt_valid(root_table)) {
        root_table = NULL;
        return false;
    }

    if (root_is_xsdt) {
        if (!bytes_equal(root_table->signature, "XSDT", 4)) {
            root_table = NULL;
            return false;
        }
    } else if (!bytes_equal(root_table->signature, "RSDT", 4)) {
        root_table = NULL;
        return false;
    }

    return true;
}

const struct acpi_sdt_header *acpi_find_table(
    const char signature[4]
) {
    if (root_table == NULL ||
        signature == NULL) {
        return NULL;
    }

    size_t entry_size =
        root_is_xsdt ? sizeof(uint64_t) : sizeof(uint32_t);

    size_t payload_size =
        root_table->length - sizeof(*root_table);

    size_t entry_count =
        payload_size / entry_size;

    const uint8_t *entries =
        (const uint8_t *)root_table + sizeof(*root_table);

    for (size_t i = 0; i < entry_count; ++i) {
        uint64_t physical;

        if (root_is_xsdt) {
            const uint64_t *entry =
                (const uint64_t *)(entries + i * entry_size);
            physical = *entry;
        } else {
            const uint32_t *entry =
                (const uint32_t *)(entries + i * entry_size);
            physical = *entry;
        }

        const struct acpi_sdt_header *candidate =
            phys_to_ptr(physical);

        if (!sdt_valid(candidate)) {
            continue;
        }

        if (bytes_equal(
                candidate->signature,
                signature,
                4)) {
            return candidate;
        }
    }

    return NULL;
}
