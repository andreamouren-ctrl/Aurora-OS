#include <aurora/arch.h>
#include <aurora/spinlock.h>

bool spinlock_self_test(void) {
    aurora_spinlock lock = AURORA_SPINLOCK_INIT;

    spinlock_init(&lock);
    if (!spinlock_try_lock(&lock)) return false;
    if (spinlock_try_lock(&lock)) return false;
    spinlock_unlock(&lock);
    if (!spinlock_try_lock(&lock)) return false;
    spinlock_unlock(&lock);

    bool interrupts_before = arch_interrupts_enabled();
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&lock);
    if (arch_interrupts_enabled()) {
        spinlock_unlock_irqrestore(&lock, irq);
        return false;
    }
    if (spinlock_try_lock(&lock)) {
        spinlock_unlock_irqrestore(&lock, irq);
        return false;
    }
    spinlock_unlock_irqrestore(&lock, irq);

    if (arch_interrupts_enabled() != interrupts_before) return false;
    if (!spinlock_try_lock(&lock)) return false;
    spinlock_unlock(&lock);
    return true;
}
