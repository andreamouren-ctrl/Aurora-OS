#include <stddef.h>
#include <stdint.h>

#include <aurora/heap.h>
#include <aurora/memory_object.h>
#include <aurora/pmm.h>

static struct aurora_memory_object objects[AURORA_MEMORY_OBJECT_MAX];
static aurora_spinlock object_lock = AURORA_SPINLOCK_INIT;
static uint64_t next_object_id;
static bool initialized;

static void clear_bytes(void *address, size_t length) {
    uint8_t *bytes = (uint8_t *)address;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static void clear_object(struct aurora_memory_object *object) {
    clear_bytes(object, sizeof(*object));
}

bool memory_object_system_init(void) {
    spinlock_init(&object_lock);
    for (uint32_t i = 0u; i < AURORA_MEMORY_OBJECT_MAX; ++i) {
        clear_object(&objects[i]);
    }
    next_object_id = 1u;
    initialized = true;
    return true;
}

struct aurora_memory_object *memory_object_create(
    uint32_t page_count
) {
    if (!initialized ||
        page_count == 0u ||
        page_count > AURORA_MEMORY_OBJECT_MAX_PAGES ||
        page_count > (uint32_t)(SIZE_MAX / sizeof(uint64_t))) {
        return NULL;
    }

    spinlock_lock(&object_lock);
    struct aurora_memory_object *slot = NULL;
    for (uint32_t i = 0u; i < AURORA_MEMORY_OBJECT_MAX; ++i) {
        if (!objects[i].active) {
            slot = &objects[i];
            slot->active = true;
            slot->owner_refs = 1u;
            slot->mapping_refs = 0u;
            slot->object_id = next_object_id++;
            if (next_object_id == 0u) next_object_id = 1u;
            break;
        }
    }
    spinlock_unlock(&object_lock);

    if (slot == NULL) return NULL;

    size_t list_bytes = (size_t)page_count * sizeof(uint64_t);
    uint64_t *pages = kheap_alloc(list_bytes, 16u);
    if (pages == NULL) {
        spinlock_lock(&object_lock);
        clear_object(slot);
        spinlock_unlock(&object_lock);
        return NULL;
    }

    for (uint32_t i = 0u; i < page_count; ++i) pages[i] = 0u;

    uint32_t allocated = 0u;
    for (; allocated < page_count; ++allocated) {
        uint64_t physical = pmm_alloc_page();
        if (physical == 0u) break;
        clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);
        pages[allocated] = physical;
    }

    if (allocated != page_count) {
        for (uint32_t i = 0u; i < allocated; ++i) {
            clear_bytes(pmm_phys_to_virt(pages[i]), (size_t)AURORA_PAGE_SIZE);
            pmm_free_page(pages[i]);
        }
        (void)kheap_free_sized(pages, list_bytes);
        spinlock_lock(&object_lock);
        clear_object(slot);
        spinlock_unlock(&object_lock);
        return NULL;
    }

    spinlock_lock(&object_lock);
    slot->pages = pages;
    slot->page_count = page_count;
    spinlock_unlock(&object_lock);
    return slot;
}

static bool valid_object_locked(
    const struct aurora_memory_object *object
) {
    return object >= &objects[0] &&
        object < &objects[AURORA_MEMORY_OBJECT_MAX] &&
        object->active &&
        object->pages != NULL &&
        object->page_count != 0u;
}

bool memory_object_retain_owner(
    struct aurora_memory_object *object
) {
    spinlock_lock(&object_lock);
    if (!valid_object_locked(object) ||
        object->owner_refs == UINT32_MAX) {
        spinlock_unlock(&object_lock);
        return false;
    }
    ++object->owner_refs;
    spinlock_unlock(&object_lock);
    return true;
}

static bool maybe_destroy_locked(
    struct aurora_memory_object *object,
    uint64_t **out_pages,
    uint32_t *out_page_count
) {
    if (object->owner_refs != 0u ||
        object->mapping_refs != 0u) {
        return false;
    }

    /*
     * Keep active=true while destruction is in progress. create() only reuses
     * inactive slots, so another CPU cannot recycle this object until scrub,
     * PMM release and metadata cleanup have completed.
     *
     * pages/page_count are cleared now so ordinary object validation fails
     * during teardown even though the slot itself remains reserved.
     */
    *out_pages = object->pages;
    *out_page_count = object->page_count;
    object->pages = NULL;
    object->page_count = 0u;
    return true;
}

bool memory_object_release_owner(
    struct aurora_memory_object *object
) {
    uint64_t *pages = NULL;
    uint32_t page_count = 0u;

    spinlock_lock(&object_lock);
    if (!valid_object_locked(object) ||
        object->owner_refs == 0u) {
        spinlock_unlock(&object_lock);
        return false;
    }

    --object->owner_refs;
    bool destroy = maybe_destroy_locked(object, &pages, &page_count);
    spinlock_unlock(&object_lock);

    if (!destroy) return true;

    for (uint32_t i = 0u; i < page_count; ++i) {
        clear_bytes(pmm_phys_to_virt(pages[i]), (size_t)AURORA_PAGE_SIZE);
        pmm_free_page(pages[i]);
    }

    bool freed = kheap_free_sized(
        pages,
        (size_t)page_count * sizeof(uint64_t)
    );

    spinlock_lock(&object_lock);
    clear_object(object);
    spinlock_unlock(&object_lock);
    return freed;
}

bool memory_object_mapping_acquire(
    struct aurora_memory_object *object
) {
    spinlock_lock(&object_lock);
    if (!valid_object_locked(object) ||
        object->mapping_refs == UINT32_MAX) {
        spinlock_unlock(&object_lock);
        return false;
    }
    ++object->mapping_refs;
    spinlock_unlock(&object_lock);
    return true;
}

bool memory_object_mapping_release(
    struct aurora_memory_object *object
) {
    uint64_t *pages = NULL;
    uint32_t page_count = 0u;

    spinlock_lock(&object_lock);
    if (!valid_object_locked(object) ||
        object->mapping_refs == 0u) {
        spinlock_unlock(&object_lock);
        return false;
    }

    --object->mapping_refs;
    bool destroy = maybe_destroy_locked(object, &pages, &page_count);
    spinlock_unlock(&object_lock);

    if (!destroy) return true;

    for (uint32_t i = 0u; i < page_count; ++i) {
        clear_bytes(pmm_phys_to_virt(pages[i]), (size_t)AURORA_PAGE_SIZE);
        pmm_free_page(pages[i]);
    }

    bool freed = kheap_free_sized(
        pages,
        (size_t)page_count * sizeof(uint64_t)
    );

    spinlock_lock(&object_lock);
    clear_object(object);
    spinlock_unlock(&object_lock);
    return freed;
}

bool memory_object_page_at(
    const struct aurora_memory_object *object,
    uint32_t page_index,
    uint64_t *out_physical
) {
    if (out_physical == NULL) return false;
    *out_physical = 0u;

    spinlock_lock(&object_lock);
    if (!valid_object_locked(object) ||
        page_index >= object->page_count) {
        spinlock_unlock(&object_lock);
        return false;
    }

    *out_physical = object->pages[page_index];
    spinlock_unlock(&object_lock);
    return *out_physical != 0u;
}
