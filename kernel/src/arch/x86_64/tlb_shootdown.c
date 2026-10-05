#include <stddef.h>
#include <stdint.h>

#include <aurora/apic.h>
#include <aurora/arch.h>
#include <aurora/clock.h>
#include <aurora/cpu_local.h>
#include <aurora/interrupts.h>
#include <aurora/log.h>
#include <aurora/smp.h>
#include <aurora/spinlock.h>
#include <aurora/timer.h>
#include <aurora/tlb_shootdown.h>
#include <aurora/vmm.h>

#define TLB_SHOOTDOWN_TIMEOUT_NS 250000000ull

static aurora_spinlock shootdown_lock = AURORA_SPINLOCK_INIT;
static volatile uint64_t shootdown_address;
static volatile uint32_t expected_acks;
static volatile uint32_t received_acks;
static volatile uint32_t last_remote_acks;
static volatile bool initialized;
static bool first_remote_success_logged;

static struct interrupt_frame *tlb_shootdown_interrupt(
    struct interrupt_frame *frame
) {
    uint64_t address = __atomic_load_n(
        &shootdown_address,
        __ATOMIC_ACQUIRE
    );

    arch_invalidate_page(address);
    lapic_eoi();

    __atomic_fetch_add(
        &received_acks,
        1u,
        __ATOMIC_RELEASE
    );

    return frame;
}

bool tlb_shootdown_init(void) {
    if (__atomic_load_n(&initialized, __ATOMIC_ACQUIRE)) {
        return true;
    }

    spinlock_init(&shootdown_lock);
    __atomic_store_n(&shootdown_address, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&expected_acks, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&received_acks, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&last_remote_acks, 0u, __ATOMIC_RELEASE);
    first_remote_success_logged = false;

    if (!interrupt_register_handler(
            AURORA_VECTOR_TLB_SHOOTDOWN,
            tlb_shootdown_interrupt)) {
        return false;
    }

    __atomic_store_n(&initialized, true, __ATOMIC_RELEASE);
    return true;
}

static bool cpu_needs_shootdown(
    const struct aurora_cpu_runtime *runtime,
    const struct aurora_cpu_local *local,
    const struct aurora_cpu_local *current,
    const struct vmm_address_space *space
) {
    if (runtime == NULL || local == NULL || current == NULL || space == NULL) {
        return false;
    }

    /*
     * A remote CPU is eligible only after it has taken at least one local
     * scheduler timer interrupt. At that point it is unquestionably off the
     * Limine bootstrap stack, IF is live, and a fixed IPI can be acknowledged.
     */
    if (runtime->state != AURORA_CPU_ONLINE ||
        local->logical_id == current->logical_id ||
        timer_interrupt_count_cpu(local->logical_id) == 0u) {
        return false;
    }

    if (space == vmm_kernel_space()) {
        return true;
    }

    /* vmm_lock serializes address-space activation against this snapshot. */
    return local->current_space == space;
}

bool tlb_shootdown_page(
    struct vmm_address_space *space,
    uint64_t virtual_address
) {
    if (space == NULL ||
        (virtual_address & 0xFFFu) != 0u) {
        return false;
    }

    struct aurora_cpu_local *current = cpu_local_current();
    if (current == NULL) return false;

    /*
     * Local invalidation is always needed for shared kernel mappings. For a
     * user address space it is needed only if that space is active locally.
     */
    if (space == vmm_kernel_space() ||
        cpu_local_current_space() == space) {
        arch_invalidate_page(virtual_address);
    }

    /* Early boot has no remotely schedulable CPUs, so local INVLPG is enough. */
    if (!__atomic_load_n(&initialized, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&last_remote_acks, 0u, __ATOMIC_RELEASE);
        return true;
    }

    aurora_spinlock_irq_state irq =
        spinlock_lock_irqsave(&shootdown_lock);

    __atomic_store_n(
        &shootdown_address,
        virtual_address,
        __ATOMIC_RELEASE
    );
    __atomic_store_n(&received_acks, 0u, __ATOMIC_RELEASE);

    uint32_t targets = 0u;
    for (uint32_t i = 0u; i < smp_cpu_count(); ++i) {
        const struct aurora_cpu_runtime *runtime = smp_cpu_at(i);
        const struct aurora_cpu_local *local = cpu_local_at(i);
        if (cpu_needs_shootdown(runtime, local, current, space)) {
            ++targets;
        }
    }

    __atomic_store_n(&expected_acks, targets, __ATOMIC_RELEASE);

    bool sent = true;
    for (uint32_t i = 0u; i < smp_cpu_count(); ++i) {
        const struct aurora_cpu_runtime *runtime = smp_cpu_at(i);
        const struct aurora_cpu_local *local = cpu_local_at(i);
        if (!cpu_needs_shootdown(runtime, local, current, space)) {
            continue;
        }

        if (!lapic_send_fixed_ipi(
                runtime->lapic_id,
                AURORA_VECTOR_TLB_SHOOTDOWN)) {
            sent = false;
            break;
        }
    }

    bool complete = sent;
    if (complete && targets != 0u) {
        uint64_t deadline = clock_now_ns() + TLB_SHOOTDOWN_TIMEOUT_NS;
        while (__atomic_load_n(&received_acks, __ATOMIC_ACQUIRE) < targets &&
               clock_now_ns() < deadline) {
            __asm__ volatile ("pause");
        }

        complete =
            __atomic_load_n(&received_acks, __ATOMIC_ACQUIRE) == targets;
    }

    uint32_t acknowledgements = __atomic_load_n(
        &received_acks,
        __ATOMIC_ACQUIRE
    );
    __atomic_store_n(
        &last_remote_acks,
        acknowledgements,
        __ATOMIC_RELEASE
    );

    if (complete && targets != 0u && !first_remote_success_logged) {
        first_remote_success_logged = true;
        log_write("[vmm] SMP TLB shootdown remote ACKs: ");
        log_u64(acknowledgements);
        log_line("");
    }

    spinlock_unlock_irqrestore(&shootdown_lock, irq);
    return complete;
}

uint32_t tlb_shootdown_last_remote_ack_count(void) {
    return __atomic_load_n(
        &last_remote_acks,
        __ATOMIC_ACQUIRE
    );
}
