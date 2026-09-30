#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/pmm.h>
#include <aurora/vmm.h>

#define PTE_PRESENT   (1ull << 0)
#define PTE_WRITE     (1ull << 1)
#define PTE_USER      (1ull << 2)
#define PTE_PCD       (1ull << 4)
#define PTE_HUGE      (1ull << 7)
#define PTE_GLOBAL    (1ull << 8)
#define PTE_NX        (1ull << 63)

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ull

static uint64_t root_table;

static uint64_t *table_pointer(uint64_t physical_address) {
    return (uint64_t *)pmm_phys_to_virt(
        physical_address & PTE_ADDR_MASK
    );
}

bool vmm_init(void) {
    root_table = arch_read_cr3() & PTE_ADDR_MASK;
    return root_table != 0;
}

static uint64_t make_leaf_flags(uint64_t flags) {
    uint64_t result = PTE_PRESENT;

    if ((flags & VMM_FLAG_WRITE) != 0) {
        result |= PTE_WRITE;
    }

    if ((flags & VMM_FLAG_USER) != 0) {
        result |= PTE_USER;
    }

    if ((flags & VMM_FLAG_GLOBAL) != 0) {
        result |= PTE_GLOBAL;
    }

    if ((flags & VMM_FLAG_NO_CACHE) != 0) {
        result |= PTE_PCD;
    }

    if ((flags & VMM_FLAG_EXECUTE) == 0 &&
        arch_nx_enabled()) {
        result |= PTE_NX;
    }

    return result;
}

static uint64_t make_parent_flags(uint64_t flags) {
    uint64_t result = PTE_PRESENT | PTE_WRITE;

    if ((flags & VMM_FLAG_USER) != 0) {
        result |= PTE_USER;
    }

    return result;
}

bool vmm_map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
) {
    if ((virtual_address & 0xFFFu) != 0 ||
        (physical_address & 0xFFFu) != 0 ||
        root_table == 0) {
        return false;
    }

    unsigned indices[4] = {
        (unsigned)((virtual_address >> 39) & 0x1FFu),
        (unsigned)((virtual_address >> 30) & 0x1FFu),
        (unsigned)((virtual_address >> 21) & 0x1FFu),
        (unsigned)((virtual_address >> 12) & 0x1FFu)
    };

    uint64_t table_physical = root_table;

    for (unsigned level = 0; level < 3; ++level) {
        uint64_t *table = table_pointer(table_physical);
        uint64_t entry = table[indices[level]];

        if ((entry & PTE_PRESENT) == 0) {
            uint64_t next_table = pmm_alloc_page();

            if (next_table == 0) {
                return false;
            }

            table[indices[level]] =
                next_table | make_parent_flags(flags);

            table_physical = next_table;
        } else {
            if ((entry & PTE_HUGE) != 0) {
                return false;
            }

            if ((flags & VMM_FLAG_USER) != 0) {
                table[indices[level]] |= PTE_USER;
            }

            table_physical = entry & PTE_ADDR_MASK;
        }
    }

    uint64_t *page_table = table_pointer(table_physical);

    if ((page_table[indices[3]] & PTE_PRESENT) != 0) {
        return false;
    }

    page_table[indices[3]] =
        (physical_address & PTE_ADDR_MASK) |
        make_leaf_flags(flags);

    arch_invalidate_page(virtual_address);
    return true;
}

bool vmm_unmap_page(uint64_t virtual_address) {
    if ((virtual_address & 0xFFFu) != 0 ||
        root_table == 0) {
        return false;
    }

    unsigned indices[4] = {
        (unsigned)((virtual_address >> 39) & 0x1FFu),
        (unsigned)((virtual_address >> 30) & 0x1FFu),
        (unsigned)((virtual_address >> 21) & 0x1FFu),
        (unsigned)((virtual_address >> 12) & 0x1FFu)
    };

    uint64_t table_physical = root_table;

    for (unsigned level = 0; level < 3; ++level) {
        uint64_t *table = table_pointer(table_physical);
        uint64_t entry = table[indices[level]];

        if ((entry & PTE_PRESENT) == 0 ||
            (entry & PTE_HUGE) != 0) {
            return false;
        }

        table_physical = entry & PTE_ADDR_MASK;
    }

    uint64_t *page_table = table_pointer(table_physical);

    if ((page_table[indices[3]] & PTE_PRESENT) == 0) {
        return false;
    }

    page_table[indices[3]] = 0;
    arch_invalidate_page(virtual_address);

    return true;
}

bool vmm_translate(
    uint64_t virtual_address,
    uint64_t *out_physical_address
) {
    if (out_physical_address == NULL ||
        root_table == 0) {
        return false;
    }

    unsigned indices[4] = {
        (unsigned)((virtual_address >> 39) & 0x1FFu),
        (unsigned)((virtual_address >> 30) & 0x1FFu),
        (unsigned)((virtual_address >> 21) & 0x1FFu),
        (unsigned)((virtual_address >> 12) & 0x1FFu)
    };

    uint64_t table_physical = root_table;

    for (unsigned level = 0; level < 4; ++level) {
        uint64_t *table = table_pointer(table_physical);
        uint64_t entry = table[indices[level]];

        if ((entry & PTE_PRESENT) == 0) {
            return false;
        }

        if (level == 1 && (entry & PTE_HUGE) != 0) {
            *out_physical_address =
                (entry & 0x000FFFFFC0000000ull) |
                (virtual_address & 0x3FFFFFFFull);

            return true;
        }

        if (level == 2 && (entry & PTE_HUGE) != 0) {
            *out_physical_address =
                (entry & 0x000FFFFFFFE00000ull) |
                (virtual_address & 0x1FFFFFull);

            return true;
        }

        if (level == 3) {
            *out_physical_address =
                (entry & PTE_ADDR_MASK) |
                (virtual_address & 0xFFFull);

            return true;
        }

        table_physical = entry & PTE_ADDR_MASK;
    }

    return false;
}
