#include <stddef.h>
#include <stdint.h>

#include <aurora/heap.h>
#include <aurora/pmm.h>
#include <aurora/spinlock.h>
#include <aurora/vmm.h>

#define KHEAP_BASE  0xFFFFFFFFC0000000ull
#define KHEAP_LIMIT 0xFFFFFFFFF0000000ull
#define KHEAP_MAX_FREE_RANGES 256u

struct kheap_free_range {
    uint64_t start;
    uint64_t end;
};

static uint64_t heap_cursor;
static uint64_t mapped_end;
static struct kheap_free_range free_ranges[KHEAP_MAX_FREE_RANGES];
static uint32_t free_range_count;
static aurora_spinlock heap_lock = AURORA_SPINLOCK_INIT;

static bool is_power_of_two(size_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

static uint64_t align_up_u64(uint64_t value, uint64_t alignment) {
    return (value + (alignment - 1u)) & ~(alignment - 1u);
}

bool kheap_init(void) {
    spinlock_init(&heap_lock);
    heap_cursor = KHEAP_BASE;
    mapped_end = KHEAP_BASE;
    free_range_count = 0u;
    return true;
}

/* heap_lock is held by the caller. */
static bool ensure_mapped_locked(uint64_t end) {
    uint64_t target = align_up_u64(end, AURORA_PAGE_SIZE);

    while (mapped_end < target) {
        uint64_t page = pmm_alloc_page();
        if (page == 0u) return false;

        if (!vmm_map_page(mapped_end, page, VMM_FLAG_WRITE)) {
            pmm_free_page(page);
            return false;
        }

        mapped_end += AURORA_PAGE_SIZE;
    }

    return true;
}

static void remove_free_range_locked(uint32_t index) {
    if (index >= free_range_count) return;
    for (uint32_t i = index + 1u; i < free_range_count; ++i)
        free_ranges[i - 1u] = free_ranges[i];
    --free_range_count;
}

static bool insert_free_range_locked(uint64_t start, uint64_t end) {
    if (start >= end || free_range_count >= KHEAP_MAX_FREE_RANGES) return false;

    uint32_t index = 0u;
    while (index < free_range_count && free_ranges[index].start < start) ++index;

    for (uint32_t i = free_range_count; i > index; --i)
        free_ranges[i] = free_ranges[i - 1u];

    free_ranges[index].start = start;
    free_ranges[index].end = end;
    ++free_range_count;

    if (index > 0u && free_ranges[index - 1u].end >= free_ranges[index].start) {
        if (free_ranges[index].end > free_ranges[index - 1u].end)
            free_ranges[index - 1u].end = free_ranges[index].end;
        remove_free_range_locked(index);
        --index;
    }

    while (index + 1u < free_range_count &&
           free_ranges[index].end >= free_ranges[index + 1u].start) {
        if (free_ranges[index + 1u].end > free_ranges[index].end)
            free_ranges[index].end = free_ranges[index + 1u].end;
        remove_free_range_locked(index + 1u);
    }

    return true;
}

static void *alloc_from_free_locked(size_t size, size_t alignment) {
    for (uint32_t i = 0u; i < free_range_count; ++i) {
        uint64_t start = align_up_u64(free_ranges[i].start, (uint64_t)alignment);
        if (start < free_ranges[i].start) continue;

        uint64_t end = start + (uint64_t)size;
        if (end < start || end > free_ranges[i].end) continue;

        uint64_t original_start = free_ranges[i].start;
        uint64_t original_end = free_ranges[i].end;

        if (start == original_start && end == original_end) {
            remove_free_range_locked(i);
        } else if (start == original_start) {
            free_ranges[i].start = end;
        } else if (end == original_end) {
            free_ranges[i].end = start;
        } else {
            if (free_range_count >= KHEAP_MAX_FREE_RANGES) continue;
            free_ranges[i].end = start;
            for (uint32_t j = free_range_count; j > i + 1u; --j)
                free_ranges[j] = free_ranges[j - 1u];
            free_ranges[i + 1u].start = end;
            free_ranges[i + 1u].end = original_end;
            ++free_range_count;
        }

        return (void *)(uintptr_t)start;
    }

    return NULL;
}

void *kheap_alloc(size_t size, size_t alignment) {
    if (size == 0u || !is_power_of_two(alignment)) return NULL;

    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&heap_lock);

    void *reused = alloc_from_free_locked(size, alignment);
    if (reused != NULL) {
        spinlock_unlock_irqrestore(&heap_lock, irq);
        return reused;
    }

    uint64_t start = align_up_u64(heap_cursor, (uint64_t)alignment);
    if (start < heap_cursor) {
        spinlock_unlock_irqrestore(&heap_lock, irq);
        return NULL;
    }

    uint64_t end = start + (uint64_t)size;
    if (end < start || end > KHEAP_LIMIT || !ensure_mapped_locked(end)) {
        spinlock_unlock_irqrestore(&heap_lock, irq);
        return NULL;
    }

    heap_cursor = end;
    spinlock_unlock_irqrestore(&heap_lock, irq);
    return (void *)(uintptr_t)start;
}

bool kheap_free_sized(void *address, size_t size) {
    if (address == NULL || size == 0u) return false;

    uint64_t start = (uint64_t)(uintptr_t)address;
    uint64_t end = start + (uint64_t)size;
    if (end < start || start < KHEAP_BASE || end > KHEAP_LIMIT) return false;

    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&heap_lock);

    if (end > heap_cursor) {
        spinlock_unlock_irqrestore(&heap_lock, irq);
        return false;
    }

    for (uint32_t i = 0u; i < free_range_count; ++i) {
        if (!(end <= free_ranges[i].start || start >= free_ranges[i].end)) {
            spinlock_unlock_irqrestore(&heap_lock, irq);
            return false;
        }
    }

    bool ok = insert_free_range_locked(start, end);
    spinlock_unlock_irqrestore(&heap_lock, irq);
    return ok;
}
