#ifndef AURORA_HEAP_H
#define AURORA_HEAP_H

#include <stdbool.h>
#include <stddef.h>

bool kheap_init(void);

void *kheap_alloc(
    size_t size,
    size_t alignment
);

#endif
