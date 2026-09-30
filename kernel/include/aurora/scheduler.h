#ifndef AURORA_SCHEDULER_H
#define AURORA_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t aurora_thread_id;

typedef void (*kernel_thread_entry)(
    void *argument
);

bool scheduler_init(void);

aurora_thread_id scheduler_create_kernel_thread(
    const char *name,
    kernel_thread_entry entry,
    void *argument
);

bool scheduler_start(void);

bool scheduler_thread_finished(
    aurora_thread_id id
);

aurora_thread_id scheduler_current_thread_id(void);

uint64_t scheduler_context_switch_count(void);

#endif
