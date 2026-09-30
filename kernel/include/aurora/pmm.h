#ifndef AURORA_PMM_H
#define AURORA_PMM_H

#include <stdbool.h>
#include <stdint.h>

#define AURORA_PAGE_SIZE 4096ull

struct pmm_stats {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t allocated_pages;
};

bool pmm_init(void);

uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t physical_address);

void *pmm_phys_to_virt(uint64_t physical_address);
uint64_t pmm_hhdm_offset(void);

struct pmm_stats pmm_get_stats(void);

#endif
