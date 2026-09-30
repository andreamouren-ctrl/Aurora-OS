#ifndef AURORA_SPINLOCK_H
#define AURORA_SPINLOCK_H

#include <stdint.h>

typedef struct {
    volatile uint32_t value;
} aurora_spinlock;

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

static inline void spinlock_lock(
    aurora_spinlock *lock
) {
    for (;;) {
        if (__atomic_exchange_n(
                &lock->value,
                1u,
                __ATOMIC_ACQUIRE) == 0u) {
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

#endif
