#ifndef AURORA_TIMER_H
#define AURORA_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/interrupts.h>

enum aurora_timer_mode {
    AURORA_TIMER_NONE = 0,
    AURORA_TIMER_TSC_DEADLINE,
    AURORA_TIMER_LAPIC_ONESHOT
};

typedef struct interrupt_frame *(
    *timer_callback_fn
)(
    struct interrupt_frame *frame
);

/* Initializes the shared timer interrupt handler and the BSP local timer. */
bool timer_init(void);

/*
 * Initializes only the current AP's Local APIC timer state. AP bootstrap
 * ticks remain on the bootstrap context until the dedicated scheduler-stack
 * handoff milestone enables full AP context switching.
 */
bool timer_init_ap(void);

bool timer_arm_ns(uint64_t delay_ns);
void timer_cancel(void);

void timer_set_callback(
    timer_callback_fn callback
);

enum aurora_timer_mode timer_mode(void);
const char *timer_mode_name(void);

/* Interrupts delivered to the current logical CPU. */
uint64_t timer_interrupt_count(void);
uint64_t timer_interrupt_count_cpu(uint32_t logical_id);

#endif
