#ifndef AURORA_SCHEDULER_H
#define AURORA_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/interrupts.h>

struct aurora_process;

typedef uint32_t aurora_thread_id;

typedef void (*kernel_thread_entry)(
    void *argument
);

bool scheduler_init(void);

/*
 * Prepares CPU-local idle ownership for an AP. scheduler_start_ap() currently
 * initializes AP-local timer/preemption delivery while the AP remains on its
 * bootstrap stack; the explicit idle-stack context handoff is the next SMP
 * milestone.
 */
bool scheduler_prepare_ap(uint32_t logical_id);
bool scheduler_start_ap(void);

aurora_thread_id scheduler_create_kernel_thread(
    const char *name,
    kernel_thread_entry entry,
    void *argument
);

aurora_thread_id scheduler_create_user_thread(
    const char *name,
    struct aurora_process *process
);

bool scheduler_start(void);

bool scheduler_thread_finished(
    aurora_thread_id id
);

aurora_thread_id scheduler_current_thread_id(void);

struct aurora_process *scheduler_current_process(void);

struct interrupt_frame *scheduler_terminate_current(void);

uint64_t scheduler_context_switch_count(void);

#endif
