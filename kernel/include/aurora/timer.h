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

bool timer_init(void);

bool timer_arm_ns(uint64_t delay_ns);
void timer_cancel(void);

void timer_set_callback(
    timer_callback_fn callback
);

enum aurora_timer_mode timer_mode(void);
const char *timer_mode_name(void);

uint64_t timer_interrupt_count(void);

#endif
