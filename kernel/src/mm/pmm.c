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

        uint64_t start = align_up(region.base, AURORA_PAGE_SIZE);
        end = align_down(end, AURORA_PAGE_SIZE);
        if (start < region.base || start >= end) continue;

        for (uint64_t page = start; page < end; page += AURORA_PAGE_SIZE)
            add_free_page_init(page);
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
