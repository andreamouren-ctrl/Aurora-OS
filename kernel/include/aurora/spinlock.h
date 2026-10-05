#ifndef AURORA_SPINLOCK_H
#define AURORA_SPINLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/arch.h>

typedef struct {
    volatile uint32_t value;
} aurora_spinlock;

typedef struct {
    uint64_t interrupt_state;
} aurora_spinlock_irq_state;

#define AURORA_SPINLOCK_INIT { 0u }

static inline void spinlock_init(
    aurora_spinlock *lock
) {
    __atomic_store_n(
        &lock->value,
        0u,
        __ATOMIC_RELAXED
    );
}

static inline bool spinlock_try_lock(
    aurora_spinlock *lock
) {
    uint32_t expected = 0u;
    return __atomic_compare_exchange_n(
        &lock->value,
        &expected,
        1u,
        false,
        __ATOMIC_ACQUIRE,
        __ATOMIC_RELAXED
    );
}

static inline void spinlock_lock(
    aurora_spinlock *lock
) {
    for (;;) {
        if (spinlock_try_lock(lock)) {
            return;
        }

        while (__atomic_load_n(
                   &lock->value,
                   __ATOMIC_RELAXED) != 0u) {
#if defined(__x86_64__)
            __asm__ volatile ("pause");
#endif
        }
    }
}

static inline void spinlock_unlock(
    aurora_spinlock *lock
) {
    __atomic_store_n(
        &lock->value,
        0u,
        __ATOMIC_RELEASE
    );
}

static inline aurora_spinlock_irq_state spinlock_lock_irqsave(
    aurora_spinlock *lock
) {
    aurora_spinlock_irq_state state = {
        .interrupt_state = arch_irq_save()
    };
    spinlock_lock(lock);
    return state;
}

static inline void spinlock_unlock_irqrestore(
    aurora_spinlock *lock,
    aurora_spinlock_irq_state state
) {
    spinlock_unlock(lock);
    arch_irq_restore(state.interrupt_state);
}

bool spinlock_self_test(void);

#endif
