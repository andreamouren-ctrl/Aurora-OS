#ifndef AURORA_IDENTITY_RUNTIME_STDLIB_H
#define AURORA_IDENTITY_RUNTIME_STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void free(void *pointer);
void *calloc(size_t count, size_t size);

#endif
