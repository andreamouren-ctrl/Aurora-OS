#include <stddef.h>
#include <stdint.h>

#include <aurora/pmm.h>
#include <aurora/process.h>
#include <aurora/vmm.h>

static void clear_bytes(void *address, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)address;
    if (address == NULL) return;
    while (length != 0u) {
        *bytes++ = 0u;
        --length;
    }
}

static uint64_t range_bytes(uint32_t page_count) {
    return (uint64_t)page_count * AURORA_PAGE_SIZE;
}

static bool ranges_overlap(
    uint64_t left_base,
    uint64_t left_size,
    uint64_t right_base,
    uint64_t right_size
) {
    if (left_size == 0u || right_size == 0u) return false;
    return left_base < right_base + right_size &&
        right_base < left_base + left_size;
}

static bool reserve_range(
    struct aurora_process *process,
    uint32_t page_count,
    uint32_t *out_slot,
    uint64_t *out_base
) {
    if (process == NULL || out_slot == NULL || out_base == NULL ||
        page_count == 0u || page_count > AURORA_PROCESS_ANON_MAX_RANGE_PAGES) {
        return false;
    }

    spinlock_lock(&process->anonymous_lock);

    if (process->anonymous_page_count >
        AURORA_PROCESS_ANON_MAX_TOTAL_PAGES - page_count) {
        spinlock_unlock(&process->anonymous_lock);
        return false;
    }

    uint32_t free_slot = AURORA_PROCESS_ANON_MAX_RANGES;
    for (uint32_t i = 0u; i < AURORA_PROCESS_ANON_MAX_RANGES; ++i) {
        if (process->anonymous_ranges[i].state == AURORA_PROCESS_ANON_FREE) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == AURORA_PROCESS_ANON_MAX_RANGES) {
        spinlock_unlock(&process->anonymous_lock);
        return false;
    }

    uint64_t bytes = range_bytes(page_count);
    uint64_t candidate = AURORA_USER_ANON_BASE;

    for (;;) {
        if (candidate >= AURORA_USER_ANON_LIMIT ||
            bytes > AURORA_USER_ANON_LIMIT - candidate) {
            spinlock_unlock(&process->anonymous_lock);
            return false;
        }

        bool moved = false;
        for (uint32_t i = 0u; i < AURORA_PROCESS_ANON_MAX_RANGES; ++i) {
            const struct aurora_process_anon_range *range =
                &process->anonymous_ranges[i];
            if (range->state == AURORA_PROCESS_ANON_FREE ||
                range->page_count == 0u) {
                continue;
            }

            uint64_t existing_size = range_bytes(range->page_count);
            if (ranges_overlap(
                    candidate,
                    bytes,
                    range->base,
                    existing_size)) {
                candidate = range->base + existing_size;
                moved = true;
                break;
            }
        }

        if (!moved) break;
    }

    struct aurora_process_anon_range *reserved =
        &process->anonymous_ranges[free_slot];
    reserved->base = candidate;
    reserved->page_count = page_count;
    reserved->state = AURORA_PROCESS_ANON_RESERVED;
    process->anonymous_page_count += page_count;

    *out_slot = free_slot;
    *out_base = candidate;
    spinlock_unlock(&process->anonymous_lock);
    return true;
}

static void release_reservation(
    struct aurora_process *process,
    uint32_t slot,
    uint32_t page_count
) {
    if (process == NULL || slot >= AURORA_PROCESS_ANON_MAX_RANGES) return;

    spinlock_lock(&process->anonymous_lock);
    struct aurora_process_anon_range *range = &process->anonymous_ranges[slot];
    if (range->state == AURORA_PROCESS_ANON_RESERVED &&
        range->page_count == page_count) {
        range->base = 0u;
        range->page_count = 0u;
        range->state = AURORA_PROCESS_ANON_FREE;
        if (process->anonymous_page_count >= page_count)
            process->anonymous_page_count -= page_count;
    }
    spinlock_unlock(&process->anonymous_lock);
}

static void rollback_mapped_pages(
    struct aurora_process *process,
    uint64_t base,
    uint32_t mapped_pages
) {
    for (uint32_t page = 0u; page < mapped_pages; ++page) {
        uint64_t address = base + (uint64_t)page * AURORA_PAGE_SIZE;
        uint64_t physical = 0u;
        if (!vmm_translate_in(&process->address_space, address, &physical))
            continue;

        clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);
        if (vmm_unmap_page_in(&process->address_space, address))
            pmm_free_page(physical);
    }
}

bool process_user_memory_allocate(
    struct aurora_process *process,
    uint64_t size,
    uint64_t *out_address
) {
    if (out_address != NULL) *out_address = 0u;
    if (process == NULL || out_address == NULL || size == 0u ||
        process_state(process) != AURORA_PROCESS_RUNNING) {
        return false;
    }

    uint64_t maximum =
        (uint64_t)AURORA_PROCESS_ANON_MAX_RANGE_PAGES * AURORA_PAGE_SIZE;
    if (size > maximum) return false;

    uint64_t rounded = (size + AURORA_PAGE_SIZE - 1u) & ~(AURORA_PAGE_SIZE - 1u);
    if (rounded < size) return false;

    uint32_t page_count = (uint32_t)(rounded / AURORA_PAGE_SIZE);
    uint32_t slot = 0u;
    uint64_t base = 0u;
    if (!reserve_range(process, page_count, &slot, &base)) return false;

    uint32_t mapped = 0u;
    for (; mapped < page_count; ++mapped) {
        uint64_t physical = pmm_alloc_page();
        if (physical == 0u) break;

        clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);
        uint64_t address = base + (uint64_t)mapped * AURORA_PAGE_SIZE;
        if (!vmm_map_page_in(
                &process->address_space,
                address,
                physical,
                VMM_FLAG_USER | VMM_FLAG_WRITE)) {
            clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);
            pmm_free_page(physical);
            break;
        }
    }

    if (mapped != page_count) {
        rollback_mapped_pages(process, base, mapped);
        release_reservation(process, slot, page_count);
        return false;
    }

    spinlock_lock(&process->anonymous_lock);
    if (process->anonymous_ranges[slot].state != AURORA_PROCESS_ANON_RESERVED ||
        process->anonymous_ranges[slot].base != base ||
        process->anonymous_ranges[slot].page_count != page_count) {
        spinlock_unlock(&process->anonymous_lock);
        rollback_mapped_pages(process, base, page_count);
        release_reservation(process, slot, page_count);
        return false;
    }
    process->anonymous_ranges[slot].state = AURORA_PROCESS_ANON_ACTIVE;
    spinlock_unlock(&process->anonymous_lock);

    *out_address = base;
    return true;
}

bool process_user_memory_free(
    struct aurora_process *process,
    uint64_t address
) {
    if (process == NULL || address < AURORA_USER_ANON_BASE ||
        address >= AURORA_USER_ANON_LIMIT ||
        (address & (AURORA_PAGE_SIZE - 1u)) != 0u ||
        process_state(process) != AURORA_PROCESS_RUNNING) {
        return false;
    }

    spinlock_lock(&process->anonymous_lock);
    uint32_t slot = AURORA_PROCESS_ANON_MAX_RANGES;
    uint32_t page_count = 0u;

    for (uint32_t i = 0u; i < AURORA_PROCESS_ANON_MAX_RANGES; ++i) {
        struct aurora_process_anon_range *range = &process->anonymous_ranges[i];
        if (range->state == AURORA_PROCESS_ANON_ACTIVE && range->base == address) {
            slot = i;
            page_count = range->page_count;
            range->state = AURORA_PROCESS_ANON_RESERVED;
            break;
        }
    }
    spinlock_unlock(&process->anonymous_lock);

    if (slot == AURORA_PROCESS_ANON_MAX_RANGES || page_count == 0u) return false;

    for (uint32_t page = 0u; page < page_count; ++page) {
        uint64_t physical = 0u;
        uint64_t virtual_address = address + (uint64_t)page * AURORA_PAGE_SIZE;
        if (!vmm_translate_in(
                &process->address_space,
                virtual_address,
                &physical) ||
            (physical & (AURORA_PAGE_SIZE - 1u)) != 0u) {
            spinlock_lock(&process->anonymous_lock);
            process->anonymous_ranges[slot].state = AURORA_PROCESS_ANON_ACTIVE;
            spinlock_unlock(&process->anonymous_lock);
            return false;
        }
    }

    for (uint32_t page = 0u; page < page_count; ++page) {
        uint64_t physical = 0u;
        uint64_t virtual_address = address + (uint64_t)page * AURORA_PAGE_SIZE;
        if (!vmm_translate_in(&process->address_space, virtual_address, &physical))
            return false;

        clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);
        if (!vmm_unmap_page_in(&process->address_space, virtual_address))
            return false;
        pmm_free_page(physical);
    }

    spinlock_lock(&process->anonymous_lock);
    struct aurora_process_anon_range *range = &process->anonymous_ranges[slot];
    range->base = 0u;
    range->page_count = 0u;
    range->state = AURORA_PROCESS_ANON_FREE;
    if (process->anonymous_page_count >= page_count)
        process->anonymous_page_count -= page_count;
    spinlock_unlock(&process->anonymous_lock);
    return true;
}

bool process_user_memory_preflight(
    const struct aurora_process *process
) {
    if (process == NULL) return false;

    uint32_t counted_pages = 0u;
    for (uint32_t i = 0u; i < AURORA_PROCESS_ANON_MAX_RANGES; ++i) {
        const struct aurora_process_anon_range *range =
            &process->anonymous_ranges[i];
        if (range->state == AURORA_PROCESS_ANON_FREE) continue;
        if (range->state != AURORA_PROCESS_ANON_ACTIVE ||
            range->page_count == 0u ||
            range->base < AURORA_USER_ANON_BASE ||
            range->base >= AURORA_USER_ANON_LIMIT ||
            range_bytes(range->page_count) > AURORA_USER_ANON_LIMIT - range->base) {
            return false;
        }

        counted_pages += range->page_count;
        for (uint32_t page = 0u; page < range->page_count; ++page) {
            uint64_t physical = 0u;
            uint64_t virtual_address =
                range->base + (uint64_t)page * AURORA_PAGE_SIZE;
            if (!vmm_translate_in(
                    &process->address_space,
                    virtual_address,
                    &physical) ||
                (physical & (AURORA_PAGE_SIZE - 1u)) != 0u) {
                return false;
            }
        }
    }

    return counted_pages == process->anonymous_page_count;
}

void process_user_memory_reap(struct aurora_process *process) {
    if (process == NULL) return;

    for (uint32_t i = 0u; i < AURORA_PROCESS_ANON_MAX_RANGES; ++i) {
        struct aurora_process_anon_range *range = &process->anonymous_ranges[i];
        if (range->state != AURORA_PROCESS_ANON_ACTIVE) continue;

        for (uint32_t page = 0u; page < range->page_count; ++page) {
            uint64_t physical = 0u;
            uint64_t virtual_address =
                range->base + (uint64_t)page * AURORA_PAGE_SIZE;
            if (vmm_translate_in(&process->address_space, virtual_address, &physical)) {
                clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);
                pmm_free_page(physical);
            }
        }

        range->base = 0u;
        range->page_count = 0u;
        range->state = AURORA_PROCESS_ANON_FREE;
    }

    process->anonymous_page_count = 0u;
}
