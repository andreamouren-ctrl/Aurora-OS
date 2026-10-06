#ifndef AURORA_HEAP_H
#define AURORA_HEAP_H

#include <stdbool.h>
#include <stddef.h>

bool kheap_init(void);

void *kheap_alloc(
    size_t size,
    size_t alignment
);

/*
 * Returns an exact previously allocated virtual range to the kernel heap
 * reuse list. Backing pages remain mapped: this bounds repeated allocation
 * growth without pretending that the bootstrap heap is a general allocator.
 */
bool kheap_free_sized(
    void *address,
    size_t size
);

#endif
