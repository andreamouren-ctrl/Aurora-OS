#ifndef AURORA_TLB_SHOOTDOWN_H
#define AURORA_TLB_SHOOTDOWN_H

#include <stdbool.h>
#include <stdint.h>

struct vmm_address_space;

/* Install the dedicated fixed-IPI invalidation handler. */
bool tlb_shootdown_init(void);

/*
 * Invalidate one virtual page on every scheduler-ready CPU whose TLB may
 * contain an entry for the supplied address space. Kernel-space mappings are
 * shared through every address space, so they target all ready CPUs.
 *
 * The operation is synchronous: success means every remote target has
 * acknowledged invalidation. Callers must fail-stop after publishing a PTE if
 * this function returns false; continuing with a stale remote TLB is unsafe.
 */
bool tlb_shootdown_page(
    struct vmm_address_space *space,
    uint64_t virtual_address
);

uint32_t tlb_shootdown_last_remote_ack_count(void);

#endif
