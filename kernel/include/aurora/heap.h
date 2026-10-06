#ifndef AURORA_HEAP_H
#define AURORA_HEAP_H

#include <stdbool.h>
#include <stddef.h>

bool kheap_init(void);

/*
 * Allocates a kernel-heap range whose backing bytes are zeroed before first
 * publication. Reused ranges were scrubbed before entering the free list.
 */
void *kheap_alloc(
    size_t size,
    size_t alignment
);

/*
 * Returns an exact previously allocated virtual range to the kernel heap
 * reuse list. The range is scrubbed after validation and before publication
 * for reuse. Backing pages remain mapped: this bounds repeated allocation
 * growth without pretending that the bootstrap heap is a general allocator.
 */
bool kheap_free_sized(
    void *address,
    size_t size
);

#endif
