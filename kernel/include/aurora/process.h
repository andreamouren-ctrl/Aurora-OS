#ifndef AURORA_PROCESS_H
#define AURORA_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/vmm.h>

#define AURORA_USER_IMAGE_BASE 0x0000000000400000ull
#define AURORA_USER_STACK_TOP  0x00007FFFFFF00000ull
#define AURORA_USER_STACK_PAGES 8u

typedef uint32_t aurora_process_id;

enum aurora_process_state {
    AURORA_PROCESS_RUNNING = 0,
    AURORA_PROCESS_EXITED,
    AURORA_PROCESS_FAULTED
};

struct aurora_process {
    aurora_process_id id;
    char name[32];

    struct vmm_address_space address_space;
    struct aurora_cap_table capabilities;

    uint64_t entry_point;
    uint64_t user_stack_top;

    volatile enum aurora_process_state state;
    volatile int64_t exit_code;
    volatile uint64_t fault_vector;

    volatile uint64_t bootstrap_signal;
};

struct aurora_process *process_create_image(
    const char *name,
    const uint8_t *image,
    size_t image_size
);

void process_set_bootstrap_signal(
    struct aurora_process *process,
    uint64_t value
);

uint64_t process_bootstrap_signal(
    const struct aurora_process *process
);

void process_mark_exited(
    struct aurora_process *process,
    int64_t exit_code
);

void process_mark_faulted(
    struct aurora_process *process,
    uint64_t vector
);

enum aurora_process_state process_state(
    const struct aurora_process *process
);

#endif
