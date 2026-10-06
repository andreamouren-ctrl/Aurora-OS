#ifndef AURORA_PROTECTED_STATE_SYSCALL_H
#define AURORA_PROTECTED_STATE_SYSCALL_H

#include <stdint.h>

struct aurora_process;

uint64_t protected_state_syscall_read(
    struct aurora_process *process,
    uint64_t handle,
    uint64_t user_name,
    uint64_t name_length,
    uint64_t user_buffer,
    uint64_t capacity
);

uint64_t protected_state_syscall_create_once(
    struct aurora_process *process,
    uint64_t handle,
    uint64_t user_name,
    uint64_t name_length,
    uint64_t user_data,
    uint64_t length
);

uint64_t protected_state_syscall_replace(
    struct aurora_process *process,
    uint64_t handle,
    uint64_t user_name,
    uint64_t name_length,
    uint64_t user_data,
    uint64_t length
);

#endif
