#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/cpu_local.h>
#include <aurora/pmm.h>
#include <aurora/smp.h>
#include <aurora/vmm.h>

#define PTE_PRESENT   (1ull << 0)
#define PTE_HUGE      (1ull << 7)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ull
#define USER_PML4_ENTRIES 256u
#define PAGE_TABLE_ENTRIES 512u

static uint64_t *table_pointer(uint64_t physical_address) {
    return (uint64_t *)pmm_phys_to_virt(physical_address & PTE_ADDR_MASK);
}

static bool valid_private_space(const struct vmm_address_space *space) {
    return space != NULL &&
        space != vmm_kernel_space() &&
        space->root_physical != 0u &&
        (space->root_physical & 0xFFFu) == 0u;
}

bool vmm_address_space_is_quiescent(const struct vmm_address_space *space) {
    if (!valid_private_space(space)) return false;

    for (uint32_t logical_id = 0u; logical_id < AURORA_MAX_SMP_CPUS; ++logical_id) {
        struct aurora_cpu_local *cpu = cpu_local_at(logical_id);
        if (cpu != NULL && cpu->current_space == space) return false;
    }

    return true;
}

static bool preflight_table(uint64_t table_physical, unsigned level) {
    uint64_t *table = table_pointer(table_physical);
    if (table == NULL) return false;

    for (uint32_t i = 0u; i < PAGE_TABLE_ENTRIES; ++i) {
        uint64_t entry = table[i];
        if ((entry & PTE_PRESENT) == 0u) continue;

        if (level < 3u) {
            if ((entry & PTE_HUGE) != 0u) return false;
            if (!preflight_table(entry & PTE_ADDR_MASK, level + 1u)) return false;
        }
    }

    return true;
}

static void destroy_table_pages(uint64_t table_physical, unsigned level) {
    uint64_t *table = table_pointer(table_physical);

    for (uint32_t i = 0u; i < PAGE_TABLE_ENTRIES; ++i) {
        uint64_t entry = table[i];
        if ((entry & PTE_PRESENT) == 0u) continue;

        table[i] = 0u;

        if (level < 3u) {
            uint64_t child = entry & PTE_ADDR_MASK;
            destroy_table_pages(child, level + 1u);
            pmm_free_page(child);
        }
    }
}

bool vmm_address_space_destroy(struct vmm_address_space *space) {
    if (!valid_private_space(space) || !vmm_address_space_is_quiescent(space))
        return false;

    uint64_t root_physical = space->root_physical;
    uint64_t *pml4 = table_pointer(root_physical);
    if (pml4 == NULL) return false;

    for (uint32_t i = 0u; i < USER_PML4_ENTRIES; ++i) {
        uint64_t entry = pml4[i];
        if ((entry & PTE_PRESENT) == 0u) continue;
        if ((entry & PTE_HUGE) != 0u) return false;
        if (!preflight_table(entry & PTE_ADDR_MASK, 1u)) return false;
    }

    for (uint32_t i = 0u; i < USER_PML4_ENTRIES; ++i) {
        uint64_t entry = pml4[i];
        if ((entry & PTE_PRESENT) == 0u) continue;

        uint64_t pdpt_physical = entry & PTE_ADDR_MASK;
        pml4[i] = 0u;
        destroy_table_pages(pdpt_physical, 1u);
        pmm_free_page(pdpt_physical);
    }

    space->root_physical = 0u;
    pmm_free_page(root_physical);
    return true;
}
