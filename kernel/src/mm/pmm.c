#include <stddef.h>
#include <stdint.h>

#include <aurora/boot.h>
#include <aurora/pmm.h>
#include <aurora/spinlock.h>

struct free_page_node {
    uint64_t next;
};

static uint64_t hhdm_offset;
static uint64_t free_head;
static uint64_t total_pages;
static uint64_t free_pages;
static aurora_spinlock pmm_lock = AURORA_SPINLOCK_INIT;

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + (alignment - 1u)) & ~(alignment - 1u);
}

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1u);
}

void *pmm_phys_to_virt(uint64_t physical_address) {
    return (void *)(uintptr_t)(hhdm_offset + physical_address);
}

uint64_t pmm_hhdm_offset(void) {
    return hhdm_offset;
}

/* Initialization-only helper. pmm_init() runs before secondary CPUs are online. */
static void add_free_page_init(uint64_t physical_address) {
    if (physical_address == 0u) return;

    struct free_page_node *node = pmm_phys_to_virt(physical_address);
    node->next = free_head;
    free_head = physical_address;
    ++total_pages;
    ++free_pages;
}

bool pmm_init(void) {
    spinlock_init(&pmm_lock);
    free_head = 0u;
    total_pages = 0u;
    free_pages = 0u;

    if (!boot_get_hhdm_offset(&hhdm_offset)) return false;

    uint64_t region_count = boot_memory_region_count();
    if (region_count == 0u) return false;

    for (uint64_t i = 0u; i < region_count; ++i) {
        struct aurora_memory_region region;
        if (!boot_memory_region_at(i, &region) ||
            region.type != AURORA_MEMORY_USABLE ||
            region.length < AURORA_PAGE_SIZE) continue;

        uint64_t end = region.base + region.length;
        if (end < region.base) continue;

        /* Round the base without overflowing UINT64_MAX; a malformed
         * firmware map must not wrap the physical free-list to address 0. */
        if (region.base > UINT64_MAX - (AURORA_PAGE_SIZE - 1u))
            continue;
        uint64_t start = align_up(region.base, AURORA_PAGE_SIZE);
        end = align_down(end, AURORA_PAGE_SIZE);
        if (start < region.base || start >= end) continue;

        /* Do not let a final page near UINT64_MAX wrap the loop counter. */
        for (uint64_t page = start; page < end;) {
            add_free_page_init(page);
            if (end - page <= AURORA_PAGE_SIZE) break;
            page += AURORA_PAGE_SIZE;
        }
    }

    return free_pages != 0u;
}

uint64_t pmm_alloc_page(void) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&pmm_lock);
    if (free_head == 0u) {
        spinlock_unlock_irqrestore(&pmm_lock, irq);
        return 0u;
    }

    uint64_t physical_address = free_head;
    struct free_page_node *node = pmm_phys_to_virt(physical_address);
    free_head = node->next;
    --free_pages;
    spinlock_unlock_irqrestore(&pmm_lock, irq);

    /* Zero outside the lock: the page is already exclusively owned. */
    unsigned char *page = pmm_phys_to_virt(physical_address);
    for (uint64_t i = 0u; i < AURORA_PAGE_SIZE; ++i) page[i] = 0u;
    return physical_address;
}

void pmm_free_page(uint64_t physical_address) {
    if (physical_address == 0u ||
        (physical_address & (AURORA_PAGE_SIZE - 1u)) != 0u) return;

    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&pmm_lock);
    struct free_page_node *node = pmm_phys_to_virt(physical_address);
    node->next = free_head;
    free_head = physical_address;
    ++free_pages;
    spinlock_unlock_irqrestore(&pmm_lock, irq);
}

struct pmm_stats pmm_get_stats(void) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&pmm_lock);
    struct pmm_stats result = {
        .total_pages = total_pages,
        .free_pages = free_pages,
        .allocated_pages = total_pages - free_pages
    };
    spinlock_unlock_irqrestore(&pmm_lock, irq);
    return result;
}

/* Boot-validation-only caller, before AP startup and independent of heap.
 * Exercise distinct allocation, zero-on-allocation, invalid free rejection,
 * precise accounting and reuse scrubbing without changing PMM policy. */
bool pmm_self_test(void) {
    struct pmm_stats initial = pmm_get_stats();
    if (initial.free_pages < 5u ||
        initial.total_pages != initial.free_pages + initial.allocated_pages)
        return false;

    uint64_t pages[4] = {0u,0u,0u,0u};
    uint32_t count = 0u;
    bool ok = true;
    for (uint32_t i = 0u; i < 4u; ++i) {
        uint64_t physical = pmm_alloc_page();
        if (physical == 0u ||
            (physical & (AURORA_PAGE_SIZE - 1u)) != 0u) {
            ok = false;
            break;
        }
        bool distinct = true;
        for (uint32_t j = 0u; j < count; ++j)
            if (pages[j] == physical) distinct = false;
        if (!distinct) { ok = false; break; }
        pages[count++] = physical;
        uint8_t *bytes = (uint8_t *)pmm_phys_to_virt(physical);
        if (bytes == NULL) { ok = false; break; }
        for (uint64_t j = 0u; j < AURORA_PAGE_SIZE; ++j)
            if (bytes[j] != 0u) { ok = false; break; }
        bytes[0] = 0xa5u;
        bytes[AURORA_PAGE_SIZE-1u] = 0x5au;
        if (!ok) break;
    }
    struct pmm_stats middle = pmm_get_stats();
    if (middle.total_pages != initial.total_pages ||
        middle.free_pages != initial.free_pages-count ||
        middle.allocated_pages != initial.allocated_pages+count)
        ok = false;
    /* Zero and misaligned frees are invalid; they must not inflate stats
     * or corrupt the free list. Only call this with a known owned page. */
    if (count != 0u) {
        pmm_free_page(0u);
        pmm_free_page(pages[0]+1u);
        struct pmm_stats invalid = pmm_get_stats();
        if (invalid.free_pages != middle.free_pages ||
            invalid.allocated_pages != middle.allocated_pages)
            ok = false;
    }
    for (uint32_t i = count; i > 0u; --i)
        pmm_free_page(pages[i-1u]);

    struct pmm_stats restored = pmm_get_stats();
    if (restored.total_pages != initial.total_pages ||
        restored.free_pages != initial.free_pages ||
        restored.allocated_pages != initial.allocated_pages)
        ok = false;
    if (!ok || count != 4u) return false;

    /* Last returned page is first on the free list. Its prior 0xa5/0x5a
     * sentinel must be cleared by pmm_alloc_page() before re-exposure. */
    uint64_t reused = pmm_alloc_page();
    if (reused == 0u) return false;
    bool reused_ok = reused == pages[0];
    const uint8_t *bytes = (const uint8_t *)pmm_phys_to_virt(reused);
    if (bytes == NULL) reused_ok = false;
    if (bytes != NULL)
        for (uint64_t i = 0u; i < AURORA_PAGE_SIZE; ++i)
            if (bytes[i] != 0u) { reused_ok = false; break; }
    pmm_free_page(reused);
    struct pmm_stats final_stats = pmm_get_stats();
    return reused_ok &&
        final_stats.total_pages == initial.total_pages &&
        final_stats.free_pages == initial.free_pages &&
        final_stats.allocated_pages == initial.allocated_pages;
}
