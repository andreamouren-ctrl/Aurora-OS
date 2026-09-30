#include <stddef.h>
#include <stdint.h>

#include <aurora/ioapic.h>
#include <aurora/madt.h>
#include <aurora/vmm.h>

#define IOAPIC_MMIO_BASE 0xFFFFFFFFB0100000ull
#define IOAPIC_MMIO_STRIDE 0x1000ull

#define IOAPIC_REGSEL 0x00u
#define IOAPIC_WINDOW 0x10u

#define IOAPIC_REG_VERSION 0x01u
#define IOAPIC_REG_REDIRECTION_BASE 0x10u

struct ioapic_controller {
    volatile uint8_t *base;
    uint32_t gsi_base;
    uint32_t redirection_count;
    uint8_t id;
};

static struct ioapic_controller controllers[
    AURORA_MAX_IOAPICS
];

static uint32_t controller_count;

static uint32_t ioapic_read(
    struct ioapic_controller *controller,
    uint8_t reg
) {
    volatile uint32_t *select =
        (volatile uint32_t *)(
            controller->base + IOAPIC_REGSEL
        );

    volatile uint32_t *window =
        (volatile uint32_t *)(
            controller->base + IOAPIC_WINDOW
        );

    *select = reg;
    return *window;
}

static void ioapic_write(
    struct ioapic_controller *controller,
    uint8_t reg,
    uint32_t value
) {
    volatile uint32_t *select =
        (volatile uint32_t *)(
            controller->base + IOAPIC_REGSEL
        );

    volatile uint32_t *window =
        (volatile uint32_t *)(
            controller->base + IOAPIC_WINDOW
        );

    *select = reg;
    *window = value;
}

static bool ensure_mapping(
    uint64_t virtual_address,
    uint64_t physical_address
) {
    if (vmm_map_page(
            virtual_address,
            physical_address,
            VMM_FLAG_WRITE |
            VMM_FLAG_NO_CACHE)) {
        return true;
    }

    uint64_t existing = 0;

    return vmm_translate(
               virtual_address,
               &existing) &&
        (existing & ~0xFFFull) ==
            (physical_address & ~0xFFFull);
}

static struct ioapic_controller *controller_for_gsi(
    uint32_t gsi
) {
    for (uint32_t i = 0;
         i < controller_count;
         ++i) {
        struct ioapic_controller *controller =
            &controllers[i];

        uint64_t end =
            (uint64_t)controller->gsi_base +
            controller->redirection_count;

        if (gsi >= controller->gsi_base &&
            (uint64_t)gsi < end) {
            return controller;
        }
    }

    return NULL;
}

bool ioapic_init(void) {
    controller_count = 0;

    uint32_t count =
        madt_ioapic_count();

    if (count == 0) {
        return false;
    }

    if (count > AURORA_MAX_IOAPICS) {
        count = AURORA_MAX_IOAPICS;
    }

    for (uint32_t i = 0; i < count; ++i) {
        const struct aurora_ioapic_info *info =
            madt_ioapic_at(i);

        if (info == NULL ||
            info->physical_address == 0) {
            return false;
        }

        uint64_t physical =
            (uint64_t)info->physical_address;

        uint64_t physical_page =
            physical & ~0xFFFull;

        uint64_t offset =
            physical & 0xFFFull;

        uint64_t virtual_page =
            IOAPIC_MMIO_BASE +
            (uint64_t)i * IOAPIC_MMIO_STRIDE;

        if (!ensure_mapping(
                virtual_page,
                physical_page)) {
            return false;
        }

        struct ioapic_controller *controller =
            &controllers[controller_count];

        controller->base =
            (volatile uint8_t *)(uintptr_t)(
                virtual_page + offset
            );

        controller->gsi_base =
            info->gsi_base;

        controller->id =
            info->id;

        uint32_t version =
            ioapic_read(
                controller,
                IOAPIC_REG_VERSION
            );

        controller->redirection_count =
            ((version >> 16) & 0xFFu) + 1u;

        /*
         * Aurora starts with every external interrupt masked. Device
         * drivers explicitly route only the IRQs they own.
         */
        for (uint32_t entry = 0;
             entry <
                 controller->redirection_count;
             ++entry) {
            uint8_t low_reg =
                (uint8_t)(
                    IOAPIC_REG_REDIRECTION_BASE +
                    entry * 2u
                );

            uint32_t low =
                ioapic_read(
                    controller,
                    low_reg
                );

            low |= (1u << 16);

            ioapic_write(
                controller,
                low_reg,
                low
            );
        }

        ++controller_count;
    }

    return controller_count != 0;
}

bool ioapic_route_gsi(
    uint32_t gsi,
    uint8_t vector,
    uint32_t destination_apic_id,
    bool active_low,
    bool level_triggered
) {
    if (vector < 32 ||
        destination_apic_id > 0xFFu) {
        return false;
    }

    struct ioapic_controller *controller =
        controller_for_gsi(gsi);

    if (controller == NULL) {
        return false;
    }

    uint32_t index =
        gsi - controller->gsi_base;

    uint8_t low_reg =
        (uint8_t)(
            IOAPIC_REG_REDIRECTION_BASE +
            index * 2u
        );

    uint8_t high_reg =
        (uint8_t)(low_reg + 1u);

    uint32_t low =
        vector;

    if (active_low) {
        low |= (1u << 13);
    }

    if (level_triggered) {
        low |= (1u << 15);
    }

    uint32_t high =
        destination_apic_id << 24;

    /*
     * High dword first, then low dword which removes the mask and makes
     * the route live.
     */
    ioapic_write(
        controller,
        high_reg,
        high
    );

    ioapic_write(
        controller,
        low_reg,
        low
    );

    return true;
}

bool ioapic_route_legacy_irq(
    uint8_t irq,
    uint8_t vector,
    uint32_t destination_apic_id
) {
    uint32_t gsi = irq;
    bool active_low = false;
    bool level_triggered = false;

    uint32_t override_count =
        madt_irq_override_count();

    for (uint32_t i = 0;
         i < override_count;
         ++i) {
        const struct aurora_irq_override *override =
            madt_irq_override_at(i);

        if (override == NULL ||
            override->source_irq != irq) {
            continue;
        }

        gsi = override->gsi;

        uint16_t polarity =
            override->flags & 0x3u;

        uint16_t trigger =
            (override->flags >> 2) & 0x3u;

        if (polarity == 0x3u) {
            active_low = true;
        }

        if (trigger == 0x3u) {
            level_triggered = true;
        }

        break;
    }

    return ioapic_route_gsi(
        gsi,
        vector,
        destination_apic_id,
        active_low,
        level_triggered
    );
}
