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

bool vmm_init(void);

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
