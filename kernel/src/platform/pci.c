#include <stddef.h>
#include <stdint.h>

#include <aurora/pci.h>

#define PCI_CONFIG_ADDRESS_PORT 0xCF8u
#define PCI_CONFIG_DATA_PORT    0xCFCu
#define PCI_COMMAND_OFFSET      0x04u
#define PCI_COMMAND_MEMORY      (1u << 1)
#define PCI_COMMAND_BUS_MASTER  (1u << 2)
#define PCI_STATUS_CAPABILITIES  (1u << 20)
#define PCI_CAPABILITY_POINTER   0x34u
#define PCI_CAPABILITY_MIN       0x40u
#define PCI_CAPABILITY_MAX       0xFCu
#define PCI_CAPABILITY_LIMIT     48u

static void pci_out32(uint16_t port, uint32_t value) {
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static uint32_t pci_in32(uint16_t port) {
    uint32_t value;

    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static uint32_t pci_config_address(
    uint8_t bus,
    uint8_t slot,
    uint8_t function,
    uint8_t offset
) {
    return 0x80000000u |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)function << 8) |
        ((uint32_t)offset & 0xFCu);
}

uint32_t pci_config_read32(
    uint8_t bus,
    uint8_t slot,
    uint8_t function,
    uint8_t offset
) {
    pci_out32(
        PCI_CONFIG_ADDRESS_PORT,
        pci_config_address(bus, slot, function, offset)
    );
    return pci_in32(PCI_CONFIG_DATA_PORT);
}

void pci_config_write32(
    uint8_t bus,
    uint8_t slot,
    uint8_t function,
    uint8_t offset,
    uint32_t value
) {
    pci_out32(
        PCI_CONFIG_ADDRESS_PORT,
        pci_config_address(bus, slot, function, offset)
    );
    pci_out32(PCI_CONFIG_DATA_PORT, value);
}

static bool pci_read_device(
    uint8_t bus,
    uint8_t slot,
    uint8_t function,
    struct aurora_pci_device *out_device
) {
    uint32_t id = pci_config_read32(bus, slot, function, 0x00u);
    uint16_t vendor = (uint16_t)(id & 0xFFFFu);

    if (vendor == 0xFFFFu) {
        return false;
    }

    if (out_device == NULL) {
        return true;
    }

    uint32_t class_info = pci_config_read32(bus, slot, function, 0x08u);
    uint32_t header = pci_config_read32(bus, slot, function, 0x0Cu);

    out_device->bus = bus;
    out_device->slot = slot;
    out_device->function = function;
    out_device->vendor_id = vendor;
    out_device->device_id = (uint16_t)(id >> 16);
    out_device->prog_if = (uint8_t)(class_info >> 8);
    out_device->subclass = (uint8_t)(class_info >> 16);
    out_device->class_code = (uint8_t)(class_info >> 24);
    out_device->header_type = (uint8_t)(header >> 16);
    return true;
}

bool pci_find_class(
    uint8_t class_code,
    uint8_t subclass,
    uint8_t prog_if,
    struct aurora_pci_device *out_device
) {
    for (uint16_t bus = 0u; bus < 256u; ++bus) {
        for (uint8_t slot = 0u; slot < 32u; ++slot) {
            struct aurora_pci_device function0;

            if (!pci_read_device((uint8_t)bus, slot, 0u, &function0)) {
                continue;
            }

            uint8_t function_count =
                (function0.header_type & 0x80u) != 0u ? 8u : 1u;

            for (uint8_t function = 0u;
                 function < function_count;
                 ++function) {
                struct aurora_pci_device device;

                if (!pci_read_device(
                        (uint8_t)bus,
                        slot,
                        function,
                        &device)) {
                    continue;
                }

                if (device.class_code == class_code &&
                    device.subclass == subclass &&
                    device.prog_if == prog_if) {
                    if (out_device != NULL) {
                        *out_device = device;
                    }
                    return true;
                }
            }
        }
    }

    return false;
}

uint32_t pci_read_bar32(
    const struct aurora_pci_device *device,
    uint8_t bar_index
) {
    if (device == NULL || bar_index >= 6u) {
        return 0u;
    }

    return pci_config_read32(
        device->bus,
        device->slot,
        device->function,
        (uint8_t)(0x10u + bar_index * 4u)
    );
}

bool pci_read_bar64(
    const struct aurora_pci_device *device,
    uint8_t bar_index,
    uint64_t *out_address
) {
    if (device == NULL || out_address == NULL || bar_index >= 6u) {
        return false;
    }

    uint32_t low = pci_read_bar32(device, bar_index);
    if ((low & 0x1u) != 0u) {
        return false;
    }

    uint32_t type = (low >> 1) & 0x3u;
    uint64_t address = (uint64_t)(low & ~0xFu);

    if (type == 0x2u) {
        if (bar_index >= 5u) {
            return false;
        }
        uint32_t high = pci_read_bar32(device, (uint8_t)(bar_index + 1u));
        address |= (uint64_t)high << 32;
    } else if (type != 0x0u) {
        return false;
    }

    *out_address = address;
    return address != 0u;
}

bool pci_enable_memory_bus_master(
    const struct aurora_pci_device *device
) {
    if (device == NULL) {
        return false;
    }

    uint32_t command_status = pci_config_read32(
        device->bus,
        device->slot,
        device->function,
        PCI_COMMAND_OFFSET
    );

    uint32_t updated = command_status |
        PCI_COMMAND_MEMORY |
        PCI_COMMAND_BUS_MASTER;

    pci_config_write32(
        device->bus,
        device->slot,
        device->function,
        PCI_COMMAND_OFFSET,
        updated
    );

    uint32_t verify = pci_config_read32(
        device->bus,
        device->slot,
        device->function,
        PCI_COMMAND_OFFSET
    );

    return (verify & (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)) ==
        (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER);
}


static uint8_t pci_config_read8(
    const struct aurora_pci_device *device,
    uint8_t offset
) {
    uint32_t value = pci_config_read32(
        device->bus,
        device->slot,
        device->function,
        (uint8_t)(offset & 0xFCu)
    );

    return (uint8_t)(value >> ((offset & 0x3u) * 8u));
}

bool pci_find_capability(
    const struct aurora_pci_device *device,
    uint8_t capability_id,
    uint8_t *out_offset
) {
    if (out_offset != NULL) *out_offset = 0u;

    if (device == NULL ||
        out_offset == NULL ||
        capability_id == 0u) {
        return false;
    }

    uint32_t command_status = pci_config_read32(
        device->bus,
        device->slot,
        device->function,
        PCI_COMMAND_OFFSET
    );

    if ((command_status & PCI_STATUS_CAPABILITIES) == 0u) {
        return false;
    }

    uint8_t offset = (uint8_t)(
        pci_config_read32(
            device->bus,
            device->slot,
            device->function,
            PCI_CAPABILITY_POINTER
        ) & 0xFCu
    );

    uint8_t visited[64] = {0};

    for (uint32_t hop = 0u;
         hop < PCI_CAPABILITY_LIMIT && offset != 0u;
         ++hop) {
        if (offset < PCI_CAPABILITY_MIN ||
            offset > PCI_CAPABILITY_MAX ||
            (offset & 0x3u) != 0u) {
            return false;
        }

        uint8_t index = (uint8_t)(offset >> 2u);
        if (visited[index] != 0u) {
            return false;
        }
        visited[index] = 1u;

        uint8_t id = pci_config_read8(device, offset);
        uint8_t next = pci_config_read8(
            device,
            (uint8_t)(offset + 1u)
        );

        if (id == capability_id) {
            *out_offset = offset;
            return true;
        }

        if (next == 0u) break;
        offset = (uint8_t)(next & 0xFCu);
    }

    return false;
}
