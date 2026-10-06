#ifndef AURORA_VMM_H
#define AURORA_VMM_H

#include <stdbool.h>
#include <stdint.h>

enum vmm_flags {
    VMM_FLAG_WRITE    = 1u << 0,
    VMM_FLAG_USER     = 1u << 1,
    VMM_FLAG_EXECUTE  = 1u << 2,
    VMM_FLAG_GLOBAL   = 1u << 3,
    VMM_FLAG_NO_CACHE = 1u << 4
};

struct vmm_address_space {
    uint64_t root_physical;
};

struct vmm_mapping_info {
    uint64_t physical_address;
    uint64_t flags;
    uint64_t page_size;

    bool accessed;
    bool dirty;
};

bool vmm_init(void);

struct vmm_address_space *vmm_kernel_space(void);
struct vmm_address_space *vmm_current_space(void);

bool vmm_address_space_create(
    struct vmm_address_space *out
);

/* True only when no online CPU currently has this address space active. */
bool vmm_address_space_is_quiescent(
    const struct vmm_address_space *space
);

/*
 * Destroys only the private user page-table hierarchy of a quiescent address
 * space. Leaf physical frames are owned by the mapping creator and are never
 * freed implicitly here. The shared kernel half is preserved.
 */
bool vmm_address_space_destroy(
    struct vmm_address_space *space
);

bool vmm_activate(
    struct vmm_address_space *space
);

bool vmm_map_page_in(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
);

bool vmm_unmap_page_in(
    struct vmm_address_space *space,
    uint64_t virtual_address
);

bool vmm_translate_in(
    const struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t *out_physical_address
);

bool vmm_query_in(
    const struct vmm_address_space *space,
    uint64_t virtual_address,
    struct vmm_mapping_info *out
);

bool vmm_map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
);

bool vmm_unmap_page(uint64_t virtual_address);

bool vmm_translate(
    uint64_t virtual_address,
    uint64_t *out_physical_address
);

#endif
