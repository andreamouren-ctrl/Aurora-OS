#include <stddef.h>
#include <stdint.h>

#include <aurora/heap.h>
#include <aurora/pmm.h>
#include <aurora/vmm.h>

#define KHEAP_BASE  0xFFFFFFFFC0000000ull
#define KHEAP_LIMIT 0xFFFFFFFFF0000000ull

static uint64_t heap_cursor;
static uint64_t mapped_end;

static bool is_power_of_two(size_t value) {
    return value != 0 &&
        (value & (value - 1u)) == 0;
}

static uint64_t align_up_u64(
    uint64_t value,
    uint64_t alignment
) {
    return (value + (alignment - 1u)) &
        ~(alignment - 1u);
}

bool kheap_init(void) {
    heap_cursor = KHEAP_BASE;
    mapped_end = KHEAP_BASE;
    return true;
}

static bool ensure_mapped(uint64_t end) {
    uint64_t target =
        align_up_u64(end, AURORA_PAGE_SIZE);

    while (mapped_end < target) {
        uint64_t page = pmm_alloc_page();

        if (page == 0) {
            return false;
        }

        if (!vmm_map_page(
                mapped_end,
                page,
                VMM_FLAG_WRITE)) {
            pmm_free_page(page);
            return false;
        }

        mapped_end += AURORA_PAGE_SIZE;
    }

    return true;
}

void *kheap_alloc(
    size_t size,
    size_t alignment
) {
    if (size == 0 || !is_power_of_two(alignment)) {
        return NULL;
    }

    uint64_t start =
        align_up_u64(heap_cursor, (uint64_t)alignment);

    if (start < heap_cursor) {
        return NULL;
    }

    uint64_t end = start + (uint64_t)size;

    if (end < start || end > KHEAP_LIMIT) {
        return NULL;
    }

    if (!ensure_mapped(end)) {
        return NULL;
    }

    heap_cursor = end;
    return (void *)(uintptr_t)start;
}
