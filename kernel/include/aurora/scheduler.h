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

/* Prepare a dedicated idle thread pinned to one application processor. */
bool scheduler_prepare_ap(uint32_t logical_id);

/*
 * Marks the current AP's idle thread running, prepares its address space and
 * local timer, and returns the initial idle interrupt frame. The caller must
 * count the AP as scheduler-owned and then enter that frame with
 * interrupt_enter_frame(); returning to the firmware/bootstrap stack is not a
 * valid scheduler state.
 */
struct interrupt_frame *scheduler_start_ap(void);

aurora_thread_id scheduler_create_kernel_thread(
    const char *name,
    kernel_thread_entry entry,
    void *argument
);

aurora_thread_id scheduler_create_user_thread(
    const char *name,
    struct aurora_process *process
);

/*
 * Returns the scheduler's soft placement target. This is not a hard affinity:
 * another CPU may execute the thread when stealing work is necessary.
 */
bool scheduler_thread_preferred_cpu(
    aurora_thread_id id,
    uint32_t *out_logical_id
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
