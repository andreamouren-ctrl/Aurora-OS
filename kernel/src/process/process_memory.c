#include <stddef.h>
#include <stdint.h>

#include <aurora/pmm.h>
#include <aurora/process.h>

static void clear_bytes(void *buffer, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    if (buffer == NULL) return;
    while (size != 0u) {
        *bytes++ = 0u;
        --size;
    }
}

static bool pages_for_size(size_t size, uint32_t *out_pages) {
    if (out_pages == NULL || size == 0u ||
        size > (size_t)AURORA_PROCESS_ANON_MAX_PAGES * (size_t)AURORA_PAGE_SIZE) {
        return false;
    }

    size_t pages = (size + (size_t)AURORA_PAGE_SIZE - 1u) /
        (size_t)AURORA_PAGE_SIZE;
    if (pages == 0u || pages > AURORA_PROCESS_ANON_MAX_PAGES) return false;

    *out_pages = (uint32_t)pages;
    return true;
}

static uint64_t mapping_end(const struct aurora_process_anon_mapping *mapping) {
    return mapping->base + (uint64_t)mapping->page_count * AURORA_PAGE_SIZE;
}

static bool find_free_slot(
    struct aurora_process *process,
    uint32_t *out_slot
) {
    if (process == NULL || out_slot == NULL) return false;
    for (uint32_t i = 0u; i < AURORA_PROCESS_ANON_MAPPING_SLOTS; ++i) {
        if (!process->anon_mappings[i].active) {
            *out_slot = i;
            return true;
        }
    }
    return false;
}

static bool find_first_fit(
    const struct aurora_process *process,
    uint32_t page_count,
    uint64_t *out_base
) {
    if (process == NULL || out_base == NULL || page_count == 0u) return false;

    uint64_t bytes = (uint64_t)page_count * AURORA_PAGE_SIZE;
    uint64_t candidate = AURORA_USER_ANON_BASE;

    while (candidate <= AURORA_USER_ANON_LIMIT &&
           bytes <= AURORA_USER_ANON_LIMIT - candidate) {
        bool conflict = false;
        uint64_t next_candidate = candidate;

        for (uint32_t i = 0u; i < AURORA_PROCESS_ANON_MAPPING_SLOTS; ++i) {
            const struct aurora_process_anon_mapping *mapping =
                &process->anon_mappings[i];
            if (!mapping->active) continue;

            uint64_t end = mapping_end(mapping);
            uint64_t requested_end = candidate + bytes;
            if (candidate < end && requested_end > mapping->base) {
                conflict = true;
                if (end > next_candidate) next_candidate = end;
            }
        }

        if (!conflict) {
            *out_base = candidate;
            return true;
        }

        if (next_candidate <= candidate) return false;
        candidate = next_candidate;
    }

    return false;
}

static void rollback_mapping(
    struct aurora_process *process,
    uint64_t base,
    uint32_t mapped_pages
) {
    for (uint32_t i = 0u; i < mapped_pages; ++i) {
        uint64_t virtual_address = base + (uint64_t)i * AURORA_PAGE_SIZE;
        uint64_t physical = 0u;
        if (!vmm_translate_in(&process->address_space, virtual_address, &physical)) {
            continue;
        }
        (void)vmm_unmap_page_in(&process->address_space, virtual_address);
        clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);
        pmm_free_page(physical);
    }
}

bool process_map_anonymous(
    struct aurora_process *process,
    size_t size,
    uint64_t *out_address
) {
    uint32_t pages = 0u;
    uint32_t slot = 0u;
    uint64_t base = 0u;

    if (out_address != NULL) *out_address = 0u;
    if (process == NULL || out_address == NULL ||
        process_state(process) != AURORA_PROCESS_RUNNING ||
        !pages_for_size(size, &pages)) {
        return false;
    }

    spinlock_lock(&process->memory_lock);

    if (pages > AURORA_PROCESS_ANON_MAX_PAGES - process->anon_page_count ||
        !find_free_slot(process, &slot) ||
        !find_first_fit(process, pages, &base)) {
        spinlock_unlock(&process->memory_lock);
        return false;
    }

    uint32_t mapped = 0u;
    for (; mapped < pages; ++mapped) {
        uint64_t physical = pmm_alloc_page();
        if (physical == 0u) break;

        clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);
        uint64_t virtual_address = base + (uint64_t)mapped * AURORA_PAGE_SIZE;
        if (!vmm_map_page_in(
                &process->address_space,
                virtual_address,
                physical,
                VMM_FLAG_USER | VMM_FLAG_WRITE)) {
            clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);
            pmm_free_page(physical);
            break;
        }
    }

    if (mapped != pages) {
        rollback_mapping(process, base, mapped);
        spinlock_unlock(&process->memory_lock);
        return false;
    }

    process->anon_mappings[slot].base = base;
    process->anon_mappings[slot].page_count = pages;
    process->anon_mappings[slot].active = true;
    process->anon_page_count += pages;
    *out_address = base;

    spinlock_unlock(&process->memory_lock);
    return true;
}

bool process_unmap_anonymous(
    struct aurora_process *process,
    uint64_t address
) {
    if (process == NULL || address == 0u ||
        process_state(process) != AURORA_PROCESS_RUNNING) {
        return false;
    }

    spinlock_lock(&process->memory_lock);

    struct aurora_process_anon_mapping *mapping = NULL;
    for (uint32_t i = 0u; i < AURORA_PROCESS_ANON_MAPPING_SLOTS; ++i) {
        if (process->anon_mappings[i].active &&
            process->anon_mappings[i].base == address) {
            mapping = &process->anon_mappings[i];
            break;
        }
    }

    if (mapping == NULL) {
        spinlock_unlock(&process->memory_lock);
        return false;
    }

    for (uint32_t i = 0u; i < mapping->page_count; ++i) {
        uint64_t physical = 0u;
        uint64_t virtual_address = mapping->base + (uint64_t)i * AURORA_PAGE_SIZE;
        if (!vmm_translate_in(&process->address_space, virtual_address, &physical)) {
            spinlock_unlock(&process->memory_lock);
            return false;
        }
    }

    uint32_t released_pages = mapping->page_count;
    for (uint32_t i = 0u; i < released_pages; ++i) {
        uint64_t physical = 0u;
        uint64_t virtual_address = mapping->base + (uint64_t)i * AURORA_PAGE_SIZE;
        (void)vmm_translate_in(&process->address_space, virtual_address, &physical);
        if (!vmm_unmap_page_in(&process->address_space, virtual_address)) {
            spinlock_unlock(&process->memory_lock);
            return false;
        }
        clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);
        pmm_free_page(physical);
    }

    mapping->base = 0u;
    mapping->page_count = 0u;
    mapping->active = false;
    process->anon_page_count -= released_pages;

    spinlock_unlock(&process->memory_lock);
    return true;
}

uint32_t process_anonymous_page_count(const struct aurora_process *process) {
    if (process == NULL) return 0u;
    return __atomic_load_n(&process->anon_page_count, __ATOMIC_ACQUIRE);
}

bool process_anonymous_preflight(const struct aurora_process *process) {
    if (process == NULL) return false;

    uint32_t counted = 0u;
    for (uint32_t slot = 0u; slot < AURORA_PROCESS_ANON_MAPPING_SLOTS; ++slot) {
        const struct aurora_process_anon_mapping *mapping =
            &process->anon_mappings[slot];
        if (!mapping->active) continue;

        if (mapping->base < AURORA_USER_ANON_BASE ||
            mapping->page_count == 0u ||
            mapping->page_count > AURORA_PROCESS_ANON_MAX_PAGES ||
            mapping_end(mapping) > AURORA_USER_ANON_LIMIT) {
            return false;
        }

        for (uint32_t page = 0u; page < mapping->page_count; ++page) {
            uint64_t physical = 0u;
            uint64_t virtual_address = mapping->base +
                (uint64_t)page * AURORA_PAGE_SIZE;
            if (!vmm_translate_in(
                    &process->address_space,
                    virtual_address,
                    &physical) ||
                (physical & (AURORA_PAGE_SIZE - 1u)) != 0u) {
                return false;
            }
        }

        counted += mapping->page_count;
        if (counted > AURORA_PROCESS_ANON_MAX_PAGES) return false;
    }

    return counted == process->anon_page_count;
}

void process_anonymous_reap(struct aurora_process *process) {
    if (process == NULL) return;

    for (uint32_t slot = 0u; slot < AURORA_PROCESS_ANON_MAPPING_SLOTS; ++slot) {
        struct aurora_process_anon_mapping *mapping =
            &process->anon_mappings[slot];
        if (!mapping->active) continue;

        for (uint32_t page = 0u; page < mapping->page_count; ++page) {
            uint64_t physical = 0u;
            uint64_t virtual_address = mapping->base +
                (uint64_t)page * AURORA_PAGE_SIZE;
            if (vmm_translate_in(&process->address_space, virtual_address, &physical)) {
                clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);
                pmm_free_page(physical);
            }
        }

        mapping->base = 0u;
        mapping->page_count = 0u;
        mapping->active = false;
    }

    process->anon_page_count = 0u;
}
