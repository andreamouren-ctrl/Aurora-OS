#ifndef AURORA_MEMORY_OBJECT_H
#define AURORA_MEMORY_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/spinlock.h>

#define AURORA_MEMORY_OBJECT_MAX 64u
#define AURORA_MEMORY_OBJECT_MAX_PAGES 32768u

struct aurora_memory_object {
    uint64_t object_id;
    uint64_t *pages;
    uint32_t page_count;
    uint32_t mapping_refs;
    uint32_t owner_refs;
    bool active;
};

bool memory_object_system_init(void);

struct aurora_memory_object *memory_object_create(
    uint32_t page_count
);

bool memory_object_retain_owner(
    struct aurora_memory_object *object
);

bool memory_object_release_owner(
    struct aurora_memory_object *object
);

bool memory_object_mapping_acquire(
    struct aurora_memory_object *object
);

bool memory_object_mapping_release(
    struct aurora_memory_object *object
);

bool memory_object_page_at(
    const struct aurora_memory_object *object,
    uint32_t page_index,
    uint64_t *out_physical
);

#endif
