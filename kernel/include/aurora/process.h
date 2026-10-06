#ifndef AURORA_PROCESS_H
#define AURORA_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/spinlock.h>
#include <aurora/vmm.h>

#define AURORA_USER_IMAGE_BASE 0x0000000000400000ull
#define AURORA_USER_ANON_BASE  0x0000000100000000ull
#define AURORA_USER_ANON_LIMIT 0x0000000108000000ull
#define AURORA_USER_STACK_TOP  0x00007FFFFFF00000ull
#define AURORA_USER_STACK_PAGES 8u

#define AURORA_PROCESS_ANON_MAPPING_SLOTS 32u
#define AURORA_PROCESS_ANON_MAX_PAGES 32768u

typedef uint32_t aurora_process_id;

enum aurora_process_state {
    AURORA_PROCESS_RUNNING = 0,
    AURORA_PROCESS_EXITED,
    AURORA_PROCESS_FAULTED,
    AURORA_PROCESS_REAPED
};

struct aurora_process_result {
    aurora_process_id id;
    enum aurora_process_state terminal_state;
    int64_t exit_code;
    uint64_t fault_vector;
};

struct aurora_process_anon_mapping {
    uint64_t base;
    uint32_t page_count;
    bool active;
};

struct aurora_process {
    aurora_process_id id;
    char name[32];

    struct vmm_address_space address_space;
    struct aurora_cap_table capabilities;

    uint64_t entry_point;
    uint64_t user_stack_top;
    size_t image_page_count;
    uint32_t stack_page_count;
    volatile uint32_t live_threads;

    aurora_spinlock memory_lock;
    struct aurora_process_anon_mapping anon_mappings[
        AURORA_PROCESS_ANON_MAPPING_SLOTS
    ];
    uint32_t anon_page_count;

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

/*
 * Map zero-filled, non-executable anonymous memory owned by this process.
 * Aurora chooses the virtual address. The mapping is quota-bounded and is
 * reclaimed automatically if the process exits without explicitly freeing it.
 */
bool process_map_anonymous(
    struct aurora_process *process,
    size_t size,
    uint64_t *out_address
);

/* Free one complete anonymous mapping identified by its returned base. */
bool process_unmap_anonymous(
    struct aurora_process *process,
    uint64_t address
);

uint32_t process_anonymous_page_count(
    const struct aurora_process *process
);

/* Internal lifecycle helpers used before and during terminal process reap. */
bool process_anonymous_preflight(
    const struct aurora_process *process
);

void process_anonymous_reap(
    struct aurora_process *process
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

bool process_thread_attach(
    struct aurora_process *process
);

bool process_thread_detach(
    struct aurora_process *process
);

uint32_t process_live_thread_count(
    const struct aurora_process *process
);

/*
 * Reclaims a terminal process's owned user frames plus private page tables.
 * It is valid only after all scheduler thread references have been detached.
 */
bool process_reap(
    struct aurora_process *process,
    struct aurora_process_result *out_result
);

/* Frees the small process object after successful process_reap(). */
bool process_release(
    struct aurora_process *process
);

#endif
