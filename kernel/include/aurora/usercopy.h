#ifndef AURORA_USERCOPY_H
#define AURORA_USERCOPY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct aurora_process;

bool copy_from_user(
    struct aurora_process *process,
    void *kernel_destination,
    uint64_t user_source,
    size_t length
);

bool copy_to_user(
    struct aurora_process *process,
    uint64_t user_destination,
    const void *kernel_source,
    size_t length
);

#endif
