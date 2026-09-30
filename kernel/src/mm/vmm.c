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
#define USER_TOP_EXCLUSIVE 0x0000800000000000ull

static struct vmm_address_space kernel_space;
static struct vmm_address_space *current_space;

static uint64_t *table_pointer(uint64_t physical_address) {
    return (uint64_t *)pmm_phys_to_virt(
        physical_address & PTE_ADDR_MASK
    );
}

static bool valid_space(
    const struct vmm_address_space *space
) {
    return space != NULL &&
        space->root_physical != 0 &&
        (space->root_physical & 0xFFFu) == 0;
}

bool vmm_init(void) {
    kernel_space.root_physical =
        arch_read_cr3() & PTE_ADDR_MASK;

    if (!valid_space(&kernel_space)) {
        return false;
    }

    current_space = &kernel_space;
    return true;
}

struct vmm_address_space *vmm_kernel_space(void) {
    return &kernel_space;
}

struct vmm_address_space *vmm_current_space(void) {
    return current_space;
}

bool vmm_address_space_create(
    struct vmm_address_space *out
) {
    if (out == NULL ||
        !valid_space(&kernel_space)) {
        return false;
    }

    uint64_t root =
        pmm_alloc_page();

    if (root == 0) {
        return false;
    }

    uint64_t *new_pml4 =
        table_pointer(root);

    uint64_t *kernel_pml4 =
        table_pointer(
            kernel_space.root_physical
        );

    /*
     * Lower-half entries belong exclusively to user space. Upper-half
     * entries point at Aurora's supervisor-only kernel/HHDM mappings.
     */
    for (uint32_t i = 256;
         i < 512;
         ++i) {
        new_pml4[i] =
            kernel_pml4[i];
    }

    out->root_physical = root;
    return true;
}

bool vmm_activate(
    struct vmm_address_space *space
) {
    if (!valid_space(space)) {
        return false;
    }

    if (current_space != space ||
        (arch_read_cr3() & PTE_ADDR_MASK) !=
            space->root_physical) {
        arch_write_cr3(
            space->root_physical
        );
    }

    current_space = space;
    return true;
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
    uint64_t result =
        PTE_PRESENT | PTE_WRITE;

    if ((flags & VMM_FLAG_USER) != 0) {
        result |= PTE_USER;
    }

    return result;
}

static bool mapping_request_valid(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
) {
    if ((virtual_address & 0xFFFu) != 0 ||
        (physical_address & 0xFFFu) != 0) {
        return false;
    }

    if ((flags & VMM_FLAG_USER) != 0 &&
        virtual_address >= USER_TOP_EXCLUSIVE) {
        return false;
    }

    return true;
}

bool vmm_map_page_in(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
) {
    if (!valid_space(space) ||
        !mapping_request_valid(
            virtual_address,
            physical_address,
            flags)) {
        return false;
    }

    unsigned indices[4] = {
        (unsigned)((virtual_address >> 39) & 0x1FFu),
        (unsigned)((virtual_address >> 30) & 0x1FFu),
        (unsigned)((virtual_address >> 21) & 0x1FFu),
        (unsigned)((virtual_address >> 12) & 0x1FFu)
    };

    uint64_t table_physical =
        space->root_physical;

    for (unsigned level = 0;
         level < 3;
         ++level) {
        uint64_t *table =
            table_pointer(
                table_physical
            );

        uint64_t entry =
            table[indices[level]];

        if ((entry & PTE_PRESENT) == 0) {
            uint64_t next_table =
                pmm_alloc_page();

            if (next_table == 0) {
                return false;
            }

            table[indices[level]] =
                next_table |
                make_parent_flags(flags);

            table_physical =
                next_table;
        } else {
            if ((entry & PTE_HUGE) != 0) {
                return false;
            }

            if ((flags & VMM_FLAG_USER) != 0) {
                table[indices[level]] |=
                    PTE_USER;
            }

            table_physical =
                entry & PTE_ADDR_MASK;
        }
    }

    uint64_t *page_table =
        table_pointer(table_physical);

    if ((page_table[indices[3]] &
         PTE_PRESENT) != 0) {
        return false;
    }

    page_table[indices[3]] =
        (physical_address &
         PTE_ADDR_MASK) |
        make_leaf_flags(flags);

    if (space == current_space) {
        arch_invalidate_page(
            virtual_address
        );
    }

    return true;
}

bool vmm_unmap_page_in(
    struct vmm_address_space *space,
    uint64_t virtual_address
) {
    if (!valid_space(space) ||
        (virtual_address & 0xFFFu) != 0) {
        return false;
    }

    unsigned indices[4] = {
        (unsigned)((virtual_address >> 39) & 0x1FFu),
        (unsigned)((virtual_address >> 30) & 0x1FFu),
        (unsigned)((virtual_address >> 21) & 0x1FFu),
        (unsigned)((virtual_address >> 12) & 0x1FFu)
    };

    uint64_t table_physical =
        space->root_physical;

    for (unsigned level = 0;
         level < 3;
         ++level) {
        uint64_t *table =
            table_pointer(
                table_physical
            );

        uint64_t entry =
            table[indices[level]];

        if ((entry & PTE_PRESENT) == 0 ||
            (entry & PTE_HUGE) != 0) {
            return false;
        }

        table_physical =
            entry & PTE_ADDR_MASK;
    }

    uint64_t *page_table =
        table_pointer(table_physical);

    if ((page_table[indices[3]] &
         PTE_PRESENT) == 0) {
        return false;
    }

    page_table[indices[3]] = 0;

    if (space == current_space) {
        arch_invalidate_page(
            virtual_address
        );
    }

    return true;
}

bool vmm_translate_in(
    const struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t *out_physical_address
) {
    if (!valid_space(space) ||
        out_physical_address == NULL) {
        return false;
    }

    unsigned indices[4] = {
        (unsigned)((virtual_address >> 39) & 0x1FFu),
        (unsigned)((virtual_address >> 30) & 0x1FFu),
        (unsigned)((virtual_address >> 21) & 0x1FFu),
        (unsigned)((virtual_address >> 12) & 0x1FFu)
    };

    uint64_t table_physical =
        space->root_physical;

    for (unsigned level = 0;
         level < 4;
         ++level) {
        uint64_t *table =
            table_pointer(
                table_physical
            );

        uint64_t entry =
            table[indices[level]];

        if ((entry & PTE_PRESENT) == 0) {
            return false;
        }

        if (level == 1 &&
            (entry & PTE_HUGE) != 0) {
            *out_physical_address =
                (entry &
                 0x000FFFFFC0000000ull) |
                (virtual_address &
                 0x3FFFFFFFull);

            return true;
        }

        if (level == 2 &&
            (entry & PTE_HUGE) != 0) {
            *out_physical_address =
                (entry &
                 0x000FFFFFFFE00000ull) |
                (virtual_address &
                 0x1FFFFFull);

            return true;
        }

        if (level == 3) {
            *out_physical_address =
                (entry & PTE_ADDR_MASK) |
                (virtual_address &
                 0xFFFull);

            return true;
        }

        table_physical =
            entry & PTE_ADDR_MASK;
    }

    return false;
}

bool vmm_map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
) {
    return vmm_map_page_in(
        current_space,
        virtual_address,
        physical_address,
        flags
    );
}

bool vmm_unmap_page(
    uint64_t virtual_address
) {
    return vmm_unmap_page_in(
        current_space,
        virtual_address
    );
}

bool vmm_translate(
    uint64_t virtual_address,
    uint64_t *out_physical_address
) {
    return vmm_translate_in(
        current_space,
        virtual_address,
        out_physical_address
    );
}
