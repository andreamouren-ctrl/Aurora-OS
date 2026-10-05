#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/pmm.h>
#include <aurora/spinlock.h>
#include <aurora/vmm.h>

#define PTE_PRESENT   (1ull << 0)
#define PTE_WRITE     (1ull << 1)
#define PTE_USER      (1ull << 2)
#define PTE_PCD       (1ull << 4)
#define PTE_ACCESSED  (1ull << 5)
#define PTE_DIRTY     (1ull << 6)
#define PTE_HUGE      (1ull << 7)
#define PTE_GLOBAL    (1ull << 8)
#define PTE_NX        (1ull << 63)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ull
#define USER_TOP_EXCLUSIVE 0x0000800000000000ull

static struct vmm_address_space kernel_space;
/* BSP-only until the dedicated per-CPU execution-state milestone. */
static struct vmm_address_space *current_space;
static aurora_spinlock vmm_lock = AURORA_SPINLOCK_INIT;

static uint64_t *table_pointer(uint64_t physical_address) {
    return (uint64_t *)pmm_phys_to_virt(physical_address & PTE_ADDR_MASK);
}

static bool valid_space(const struct vmm_address_space *space) {
    return space != NULL && space->root_physical != 0u &&
        (space->root_physical & 0xFFFu) == 0u;
}

bool vmm_init(void) {
    spinlock_init(&vmm_lock);
    kernel_space.root_physical = arch_read_cr3() & PTE_ADDR_MASK;
    if (!valid_space(&kernel_space)) return false;
    current_space = &kernel_space;
    return true;
}

struct vmm_address_space *vmm_kernel_space(void) {
    return &kernel_space;
}

struct vmm_address_space *vmm_current_space(void) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&vmm_lock);
    struct vmm_address_space *space = current_space;
    spinlock_unlock_irqrestore(&vmm_lock, irq);
    return space;
}

bool vmm_address_space_create(struct vmm_address_space *out) {
    if (out == NULL) return false;
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&vmm_lock);
    if (!valid_space(&kernel_space)) {
        spinlock_unlock_irqrestore(&vmm_lock, irq);
        return false;
    }

    uint64_t root = pmm_alloc_page();
    if (root == 0u) {
        spinlock_unlock_irqrestore(&vmm_lock, irq);
        return false;
    }

    uint64_t *new_pml4 = table_pointer(root);
    uint64_t *kernel_pml4 = table_pointer(kernel_space.root_physical);
    for (uint32_t i = 256u; i < 512u; ++i) new_pml4[i] = kernel_pml4[i];
    out->root_physical = root;
    spinlock_unlock_irqrestore(&vmm_lock, irq);
    return true;
}

bool vmm_activate(struct vmm_address_space *space) {
    if (!valid_space(space)) return false;
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&vmm_lock);
    if (current_space != space ||
        (arch_read_cr3() & PTE_ADDR_MASK) != space->root_physical)
        arch_write_cr3(space->root_physical);
    current_space = space;
    spinlock_unlock_irqrestore(&vmm_lock, irq);
    return true;
}

static uint64_t make_leaf_flags(uint64_t flags) {
    uint64_t result = PTE_PRESENT;
    if ((flags & VMM_FLAG_WRITE) != 0u) result |= PTE_WRITE;
    if ((flags & VMM_FLAG_USER) != 0u) result |= PTE_USER;
    if ((flags & VMM_FLAG_GLOBAL) != 0u) result |= PTE_GLOBAL;
    if ((flags & VMM_FLAG_NO_CACHE) != 0u) result |= PTE_PCD;
    if ((flags & VMM_FLAG_EXECUTE) == 0u && arch_nx_enabled()) result |= PTE_NX;
    return result;
}

static uint64_t make_parent_flags(uint64_t flags) {
    uint64_t result = PTE_PRESENT | PTE_WRITE;
    if ((flags & VMM_FLAG_USER) != 0u) result |= PTE_USER;
    return result;
}

static bool mapping_request_valid(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
) {
    if ((virtual_address & 0xFFFu) != 0u || (physical_address & 0xFFFu) != 0u)
        return false;
    return (flags & VMM_FLAG_USER) == 0u || virtual_address < USER_TOP_EXCLUSIVE;
}

/* vmm_lock must be held by all helpers suffixed with _locked. */
static bool map_page_locked(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
) {
    if (!valid_space(space) ||
        !mapping_request_valid(virtual_address, physical_address, flags)) return false;

    unsigned indices[4] = {
        (unsigned)((virtual_address >> 39) & 0x1FFu),
        (unsigned)((virtual_address >> 30) & 0x1FFu),
        (unsigned)((virtual_address >> 21) & 0x1FFu),
        (unsigned)((virtual_address >> 12) & 0x1FFu)
    };
    uint64_t table_physical = space->root_physical;

    for (unsigned level = 0u; level < 3u; ++level) {
        uint64_t *table = table_pointer(table_physical);
        uint64_t entry = table[indices[level]];
        if ((entry & PTE_PRESENT) == 0u) {
            uint64_t next_table = pmm_alloc_page();
            if (next_table == 0u) return false;
            table[indices[level]] = next_table | make_parent_flags(flags);
            table_physical = next_table;
        } else {
            if ((entry & PTE_HUGE) != 0u) return false;
            if ((flags & VMM_FLAG_USER) != 0u) table[indices[level]] |= PTE_USER;
            table_physical = entry & PTE_ADDR_MASK;
        }
    }

    uint64_t *page_table = table_pointer(table_physical);
    if ((page_table[indices[3]] & PTE_PRESENT) != 0u) return false;
    page_table[indices[3]] = (physical_address & PTE_ADDR_MASK) | make_leaf_flags(flags);
    if (space == current_space) arch_invalidate_page(virtual_address);
    return true;
}

bool vmm_map_page_in(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&vmm_lock);
    bool result = map_page_locked(space, virtual_address, physical_address, flags);
    spinlock_unlock_irqrestore(&vmm_lock, irq);
    return result;
}

static bool unmap_page_locked(
    struct vmm_address_space *space,
    uint64_t virtual_address
) {
    if (!valid_space(space) || (virtual_address & 0xFFFu) != 0u) return false;
    unsigned indices[4] = {
        (unsigned)((virtual_address >> 39) & 0x1FFu),
        (unsigned)((virtual_address >> 30) & 0x1FFu),
        (unsigned)((virtual_address >> 21) & 0x1FFu),
        (unsigned)((virtual_address >> 12) & 0x1FFu)
    };
    uint64_t table_physical = space->root_physical;
    for (unsigned level = 0u; level < 3u; ++level) {
        uint64_t *table = table_pointer(table_physical);
        uint64_t entry = table[indices[level]];
        if ((entry & PTE_PRESENT) == 0u || (entry & PTE_HUGE) != 0u) return false;
        table_physical = entry & PTE_ADDR_MASK;
    }
    uint64_t *page_table = table_pointer(table_physical);
    if ((page_table[indices[3]] & PTE_PRESENT) == 0u) return false;
    page_table[indices[3]] = 0u;
    if (space == current_space) arch_invalidate_page(virtual_address);
    return true;
}

bool vmm_unmap_page_in(struct vmm_address_space *space, uint64_t virtual_address) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&vmm_lock);
    bool result = unmap_page_locked(space, virtual_address);
    spinlock_unlock_irqrestore(&vmm_lock, irq);
    return result;
}

static bool translate_locked(
    const struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t *out_physical_address
) {
    if (!valid_space(space) || out_physical_address == NULL) return false;
    unsigned indices[4] = {
        (unsigned)((virtual_address >> 39) & 0x1FFu),
        (unsigned)((virtual_address >> 30) & 0x1FFu),
        (unsigned)((virtual_address >> 21) & 0x1FFu),
        (unsigned)((virtual_address >> 12) & 0x1FFu)
    };
    uint64_t table_physical = space->root_physical;
    for (unsigned level = 0u; level < 4u; ++level) {
        uint64_t *table = table_pointer(table_physical);
        uint64_t entry = table[indices[level]];
        if ((entry & PTE_PRESENT) == 0u) return false;
        if (level == 1u && (entry & PTE_HUGE) != 0u) {
            *out_physical_address = (entry & 0x000FFFFFC0000000ull) |
                (virtual_address & 0x3FFFFFFFull);
            return true;
        }
        if (level == 2u && (entry & PTE_HUGE) != 0u) {
            *out_physical_address = (entry & 0x000FFFFFFFE00000ull) |
                (virtual_address & 0x1FFFFFull);
            return true;
        }
        if (level == 3u) {
            *out_physical_address = (entry & PTE_ADDR_MASK) |
                (virtual_address & 0xFFFull);
            return true;
        }
        table_physical = entry & PTE_ADDR_MASK;
    }
    return false;
}

bool vmm_translate_in(
    const struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t *out_physical_address
) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&vmm_lock);
    bool result = translate_locked(space, virtual_address, out_physical_address);
    spinlock_unlock_irqrestore(&vmm_lock, irq);
    return result;
}

bool vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&vmm_lock);
    bool result = map_page_locked(current_space, virtual_address, physical_address, flags);
    spinlock_unlock_irqrestore(&vmm_lock, irq);
    return result;
}

bool vmm_unmap_page(uint64_t virtual_address) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&vmm_lock);
    bool result = unmap_page_locked(current_space, virtual_address);
    spinlock_unlock_irqrestore(&vmm_lock, irq);
    return result;
}

bool vmm_translate(uint64_t virtual_address, uint64_t *out_physical_address) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&vmm_lock);
    bool result = translate_locked(current_space, virtual_address, out_physical_address);
    spinlock_unlock_irqrestore(&vmm_lock, irq);
    return result;
}

static uint64_t decode_mapping_flags(uint64_t entry) {
    uint64_t flags = 0u;
    if ((entry & PTE_WRITE) != 0u) flags |= VMM_FLAG_WRITE;
    if ((entry & PTE_USER) != 0u) flags |= VMM_FLAG_USER;
    if ((entry & PTE_GLOBAL) != 0u) flags |= VMM_FLAG_GLOBAL;
    if ((entry & PTE_PCD) != 0u) flags |= VMM_FLAG_NO_CACHE;
    if (!arch_nx_enabled() || (entry & PTE_NX) == 0u) flags |= VMM_FLAG_EXECUTE;
    return flags;
}

static bool query_locked(
    const struct vmm_address_space *space,
    uint64_t virtual_address,
    struct vmm_mapping_info *out
) {
    if (!valid_space(space) || out == NULL) return false;
    unsigned indices[4] = {
        (unsigned)((virtual_address >> 39) & 0x1FFu),
        (unsigned)((virtual_address >> 30) & 0x1FFu),
        (unsigned)((virtual_address >> 21) & 0x1FFu),
        (unsigned)((virtual_address >> 12) & 0x1FFu)
    };
    uint64_t table_physical = space->root_physical;
    for (unsigned level = 0u; level < 4u; ++level) {
        uint64_t *table = table_pointer(table_physical);
        uint64_t entry = table[indices[level]];
        if ((entry & PTE_PRESENT) == 0u) return false;
        if (level == 1u && (entry & PTE_HUGE) != 0u) {
            out->physical_address = (entry & 0x000FFFFFC0000000ull) |
                (virtual_address & 0x3FFFFFFFull);
            out->page_size = 0x40000000ull;
            out->flags = decode_mapping_flags(entry);
            out->accessed = (entry & PTE_ACCESSED) != 0u;
            out->dirty = (entry & PTE_DIRTY) != 0u;
            return true;
        }
        if (level == 2u && (entry & PTE_HUGE) != 0u) {
            out->physical_address = (entry & 0x000FFFFFFFE00000ull) |
                (virtual_address & 0x1FFFFFull);
            out->page_size = 0x200000ull;
            out->flags = decode_mapping_flags(entry);
            out->accessed = (entry & PTE_ACCESSED) != 0u;
            out->dirty = (entry & PTE_DIRTY) != 0u;
            return true;
        }
        if (level == 3u) {
            out->physical_address = (entry & PTE_ADDR_MASK) |
                (virtual_address & 0xFFFull);
            out->page_size = AURORA_PAGE_SIZE;
            out->flags = decode_mapping_flags(entry);
            out->accessed = (entry & PTE_ACCESSED) != 0u;
            out->dirty = (entry & PTE_DIRTY) != 0u;
            return true;
        }
        table_physical = entry & PTE_ADDR_MASK;
    }
    return false;
}

bool vmm_query_in(
    const struct vmm_address_space *space,
    uint64_t virtual_address,
    struct vmm_mapping_info *out
) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&vmm_lock);
    bool result = query_locked(space, virtual_address, out);
    spinlock_unlock_irqrestore(&vmm_lock, irq);
    return result;
}
