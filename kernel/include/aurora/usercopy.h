#ifndef AURORA_USERCOPY_H
#define AURORA_USERCOPY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct aurora_process;

/* Preflights the entire mapped user span before making either copy.
 * Invalid or permission-denied ranges do not partially modify the output.
 * This does not by itself pin mappings against concurrent address-space
 * mutation by a privileged supervisor; future multithreaded user processes
 * require stable mapping leases or equivalent VMM serialization. */
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
