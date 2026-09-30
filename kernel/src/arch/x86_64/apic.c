#include <stddef.h>
#include <stdint.h>

#include <aurora/apic.h>
#include <aurora/arch.h>
#include <aurora/interrupts.h>
#include <aurora/madt.h>
#include <aurora/spinlock.h>
#include <aurora/vmm.h>

#define IA32_APIC_BASE_MSR      0x1Bu
#define IA32_TSC_DEADLINE_MSR   0x6E0u

#define APIC_BASE_ENABLE        (1ull << 11)
#define APIC_BASE_X2APIC        (1ull << 10)

#define LAPIC_REG_ID            0x020u
#define LAPIC_REG_TPR           0x080u
#define LAPIC_REG_EOI           0x0B0u
#define LAPIC_REG_SVR           0x0F0u
#define LAPIC_REG_LVT_TIMER     0x320u
#define LAPIC_REG_INITIAL_COUNT 0x380u
#define LAPIC_REG_CURRENT_COUNT 0x390u
#define LAPIC_REG_DIVIDE        0x3E0u

#define X2APIC_MSR_BASE         0x800u

#define LAPIC_MMIO_VIRTUAL      0xFFFFFFFFB0000000ull

static aurora_spinlock lapic_init_lock =
    AURORA_SPINLOCK_INIT;

static enum lapic_mode current_mode;
static volatile uint8_t *lapic_mmio;

static void cpuid(
    uint32_t leaf,
    uint32_t subleaf,
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    __asm__ volatile (
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(subleaf)
    );

    if (eax != NULL) *eax = a;
    if (ebx != NULL) *ebx = b;
    if (ecx != NULL) *ecx = c;
    if (edx != NULL) *edx = d;
}

static uint64_t rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile (
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr)
    );

    return ((uint64_t)high << 32) | low;
}

static void wrmsr(
    uint32_t msr,
    uint64_t value
) {
    __asm__ volatile (
        "wrmsr"
        :
        : "c"(msr),
          "a"((uint32_t)value),
          "d"((uint32_t)(value >> 32))
    );
}

static uint32_t x2apic_msr_for_offset(uint32_t offset) {
    return X2APIC_MSR_BASE + (offset >> 4);
}

static uint32_t lapic_read(uint32_t offset) {
    if (current_mode == LAPIC_MODE_X2APIC) {
        return (uint32_t)rdmsr(
            x2apic_msr_for_offset(offset)
        );
    }

    if (current_mode == LAPIC_MODE_XAPIC &&
        lapic_mmio != NULL) {
        volatile uint32_t *reg =
            (volatile uint32_t *)(lapic_mmio + offset);

        return *reg;
    }

    return 0;
}

static void lapic_write(
    uint32_t offset,
    uint32_t value
) {
    if (current_mode == LAPIC_MODE_X2APIC) {
        wrmsr(
            x2apic_msr_for_offset(offset),
            value
        );
        return;
    }

    if (current_mode == LAPIC_MODE_XAPIC &&
        lapic_mmio != NULL) {
        volatile uint32_t *reg =
            (volatile uint32_t *)(lapic_mmio + offset);

        *reg = value;
        (void)lapic_read(LAPIC_REG_ID);
    }
}

static bool lapic_prepare_shared(
    bool local_x2apic
) {
    bool ok = true;

    spinlock_lock(
        &lapic_init_lock
    );

    if (current_mode ==
        LAPIC_MODE_NONE) {
        if (local_x2apic) {
            current_mode =
                LAPIC_MODE_X2APIC;
        } else {
            uint64_t physical =
                madt_lapic_address() &
                ~0xFFFull;

            if (physical == 0) {
                ok = false;
            } else {
                if (!vmm_map_page(
                        LAPIC_MMIO_VIRTUAL,
                        physical,
                        VMM_FLAG_WRITE |
                        VMM_FLAG_NO_CACHE)) {
                    uint64_t existing = 0;

                    if (!vmm_translate(
                            LAPIC_MMIO_VIRTUAL,
                            &existing) ||
                        (existing & ~0xFFFull) !=
                            physical) {
                        ok = false;
                    }
                }

                if (ok) {
                    lapic_mmio =
                        (volatile uint8_t *)
                        (uintptr_t)
                            LAPIC_MMIO_VIRTUAL;

                    current_mode =
                        LAPIC_MODE_XAPIC;
                }
            }
        }
    } else {
        /*
         * All logical CPUs must agree on the APIC access mode. Mixing xAPIC
         * and x2APIC while using one shared kernel APIC backend would make
         * register accesses ambiguous and unsafe.
         */
        enum lapic_mode local_mode =
            local_x2apic
                ? LAPIC_MODE_X2APIC
                : LAPIC_MODE_XAPIC;

        ok =
            current_mode ==
            local_mode;
    }

    spinlock_unlock(
        &lapic_init_lock
    );

    return ok;
}

bool lapic_init(void) {
    uint32_t feature_ecx = 0;
    uint32_t feature_edx = 0;

    cpuid(
        1,
        0,
        NULL,
        NULL,
        &feature_ecx,
        &feature_edx
    );

    if ((feature_edx & (1u << 9)) == 0) {
        return false;
    }

    /*
     * Aurora does not use the legacy 8259 PIC as its primary controller.
     * Mask both chips before IF is ever enabled.
     */
    arch_out8(0x21u, 0xFFu);
    arch_out8(0xA1u, 0xFFu);

    /*
     * IA32_APIC_BASE is per logical CPU. Every CPU must make sure its own
     * local APIC is enabled, even though the shared access backend below is
     * initialized only once.
     */
    uint64_t apic_base =
        rdmsr(IA32_APIC_BASE_MSR);

    if ((apic_base & APIC_BASE_ENABLE) == 0) {
        apic_base |= APIC_BASE_ENABLE;
        wrmsr(
            IA32_APIC_BASE_MSR,
            apic_base
        );

        apic_base =
            rdmsr(
                IA32_APIC_BASE_MSR
            );
    }

    bool x2apic_supported =
        (feature_ecx & (1u << 21)) != 0;

    bool local_x2apic =
        x2apic_supported &&
        (apic_base &
         APIC_BASE_X2APIC) != 0;

    if (!lapic_prepare_shared(
            local_x2apic)) {
        return false;
    }

    lapic_write(LAPIC_REG_TPR, 0);

    uint32_t svr =
        lapic_read(LAPIC_REG_SVR);

    svr &= ~0xFFu;
    svr |= AURORA_VECTOR_SPURIOUS;
    svr |= (1u << 8);

    lapic_write(
        LAPIC_REG_SVR,
        svr
    );

    return true;
}

enum lapic_mode lapic_current_mode(void) {
    return current_mode;
}

uint32_t lapic_id(void) {
    uint32_t value = lapic_read(LAPIC_REG_ID);

    if (current_mode == LAPIC_MODE_X2APIC) {
        return value;
    }

    return value >> 24;
}

void lapic_eoi(void) {
    lapic_write(LAPIC_REG_EOI, 0);
}

bool lapic_timer_tsc_deadline_supported(void) {
    uint32_t ecx = 0;

    cpuid(
        1,
        0,
        NULL,
        NULL,
        &ecx,
        NULL
    );

    return (ecx & (1u << 24)) != 0;
}

void lapic_timer_configure_tsc_deadline(
    uint8_t vector
) {
    uint32_t value =
        (uint32_t)vector |
        (2u << 17);

    lapic_write(
        LAPIC_REG_LVT_TIMER,
        value
    );
}

void lapic_timer_set_tsc_deadline(
    uint64_t deadline
) {
    wrmsr(
        IA32_TSC_DEADLINE_MSR,
        deadline
    );
}

void lapic_timer_configure_oneshot(
    uint8_t vector,
    bool masked,
    uint32_t divide_configuration
) {
    lapic_write(
        LAPIC_REG_DIVIDE,
        divide_configuration & 0x0Bu
    );

    uint32_t value =
        (uint32_t)vector;

    if (masked) {
        value |= (1u << 16);
    }

    lapic_write(
        LAPIC_REG_LVT_TIMER,
        value
    );
}

void lapic_timer_set_initial_count(
    uint32_t count
) {
    lapic_write(
        LAPIC_REG_INITIAL_COUNT,
        count
    );
}

uint32_t lapic_timer_current_count(void) {
    return lapic_read(
        LAPIC_REG_CURRENT_COUNT
    );
}
