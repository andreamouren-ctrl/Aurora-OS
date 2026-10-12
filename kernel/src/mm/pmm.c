#include <stddef.h>
#include <stdint.h>

#include <aurora/boot.h>
#include <aurora/panic.h>
#include <aurora/pmm.h>
#include <aurora/spinlock.h>

struct free_page_node {
    uint64_t next;
};

/* Metadata lives inside each USABLE physical region, never on the PMM
 * free list. One bitmap bit per managed 4 KiB page: 1 = allocated,
 * 0 = available. No heap, dynamic allocator or identity mapping required.
 * The static descriptors themselves live in kernel BSS. */
#define PMM_MAX_REGIONS 512u
#define PMM_MAX_BOOT_ENTRIES 4096u
#define PMM_BITS_PER_METADATA_PAGE (AURORA_PAGE_SIZE * 8u)

struct pmm_region {
    uint64_t metadata_start;
    uint64_t managed_start;
    uint64_t managed_end;
    uint64_t page_count;
    uint8_t *allocated_bits;
};

static uint64_t hhdm_offset;
static uint64_t free_head;
static uint64_t total_pages;
static uint64_t free_pages;
static uint32_t managed_region_count;
static struct pmm_region managed_regions[PMM_MAX_REGIONS];
static aurora_spinlock pmm_lock = AURORA_SPINLOCK_INIT;

static uint64_t align_up(uint64_t value,uint64_t alignment) {
    return (value + (alignment - 1u)) & ~(alignment - 1u);
}
static uint64_t align_down(uint64_t value,uint64_t alignment) {
    return value & ~(alignment - 1u);
}
static bool overlap(uint64_t start_a,uint64_t end_a,
                    uint64_t start_b,uint64_t end_b) {
    return start_a < end_b && start_b < end_a;
}
static bool aligned_usable_range(const struct aurora_memory_region *region,
                                 uint64_t *out_start,uint64_t *out_end) {
    if (region == NULL || region->type != AURORA_MEMORY_USABLE ||
        region->length < AURORA_PAGE_SIZE ||
        region->base > UINT64_MAX - region->length ||
        region->base > UINT64_MAX - (AURORA_PAGE_SIZE-1u))
        return false;
    uint64_t start=align_up(region->base,AURORA_PAGE_SIZE);
    uint64_t end=align_down(region->base+region->length,AURORA_PAGE_SIZE);
    if (start==0u) start=AURORA_PAGE_SIZE;
    if (start>=end) return false;
    *out_start=start;
    *out_end=end;
    return true;
}
static struct pmm_region *region_for_page_locked(
    uint64_t physical,uint64_t *out_index
) {
    if (physical==0u || (physical & (AURORA_PAGE_SIZE-1u))!=0u)
        return NULL;
    for (uint32_t i=0u;i<managed_region_count;++i) {
        struct pmm_region *region=&managed_regions[i];
        if (physical>=region->managed_start &&
            physical<region->managed_end) {
            *out_index=(physical-region->managed_start)/AURORA_PAGE_SIZE;
            return region;
        }
    }
    return NULL;
}
static bool bitmap_get(const struct pmm_region *r,uint64_t index) {
    return (r->allocated_bits[index/8u] & (uint8_t)(1u<<(index%8u)))!=0u;
}
static void bitmap_set(struct pmm_region *r,uint64_t index,bool allocated) {
    uint8_t mask=(uint8_t)(1u<<(index%8u));
    if (allocated) r->allocated_bits[index/8u]|=mask;
    else r->allocated_bits[index/8u]&=(uint8_t)~mask;
}
void *pmm_phys_to_virt(uint64_t physical_address) {
    return (void *)(uintptr_t)(hhdm_offset+physical_address);
}
uint64_t pmm_hhdm_offset(void) { return hhdm_offset; }

/* Validate the firmware map *before* writing PMM metadata to any physical
 * pages. All USABLE intervals must be non-overlapping and disjoint from
 * reserved, ACPI, kernel, module, MMIO and framebuffer entries. */
static bool memory_map_safe(uint64_t entry_count) {
    for (uint64_t i=0u;i<entry_count;++i) {
        struct aurora_memory_region region;
        if (!boot_memory_region_at(i,&region)) return false;
        uint64_t start=0u,end=0u;
        if (!aligned_usable_range(&region,&start,&end)) {
            /* Empty/malformed usable entries cannot grant memory. */
            if (region.type==AURORA_MEMORY_USABLE &&
                region.length>=AURORA_PAGE_SIZE &&
                (region.base>UINT64_MAX-region.length))
                return false;
            continue;
        }
        for (uint64_t j=0u;j<entry_count;++j) {
            if (j==i) continue;
            struct aurora_memory_region other;
            if (!boot_memory_region_at(j,&other)) return false;
            if (other.length==0u) continue;
            if (other.base>UINT64_MAX-other.length) return false;
            if (overlap(start,end,other.base,other.base+other.length))
                return false;
        }
    }
    return true;
}

bool pmm_init(void) {
    spinlock_init(&pmm_lock);
    free_head=0u;
    total_pages=0u;
    free_pages=0u;
    managed_region_count=0u;
    for (uint32_t i=0u;i<PMM_MAX_REGIONS;++i)
        managed_regions[i]=(struct pmm_region){0};

    if (!boot_get_hhdm_offset(&hhdm_offset)) return false;
    uint64_t entry_count=boot_memory_region_count();
    if (entry_count==0u || entry_count>PMM_MAX_BOOT_ENTRIES ||
        !memory_map_safe(entry_count)) return false;

    for (uint64_t i=0u;i<entry_count;++i) {
        struct aurora_memory_region region;
        if (!boot_memory_region_at(i,&region)) return false;
        uint64_t start=0u,end=0u;
        if (!aligned_usable_range(&region,&start,&end)) continue;
        uint64_t page_count=(end-start)/AURORA_PAGE_SIZE;
        /* A metadata page covers 32,768 physical pages (128 MiB).
         * Reject tiny regions that cannot house metadata + a usable page. */
        uint64_t metadata_pages=
            (page_count+PMM_BITS_PER_METADATA_PAGE-1u)/
            PMM_BITS_PER_METADATA_PAGE;
        if (metadata_pages>=page_count) continue;
        if (managed_region_count>=PMM_MAX_REGIONS) return false;
        uint64_t managed_start=start+metadata_pages*AURORA_PAGE_SIZE;
        if (hhdm_offset>UINT64_MAX-end) return false;
        struct pmm_region *tracked=&managed_regions[managed_region_count++];
        tracked->metadata_start=start;
        tracked->managed_start=managed_start;
        tracked->managed_end=end;
        tracked->page_count=page_count-metadata_pages;
        tracked->allocated_bits=(uint8_t *)pmm_phys_to_virt(start);

        /* Zero all metadata pages before the list is exposed to allocators.
         * Metadata frames are NEVER counted as free/managed pages. */
        for (uint64_t byte=0u;byte<metadata_pages*AURORA_PAGE_SIZE;++byte)
            tracked->allocated_bits[byte]=0u;

        for (uint64_t page=managed_start;page<end;) {
            struct free_page_node *node=pmm_phys_to_virt(page);
            node->next=free_head;
            free_head=page;
            ++total_pages;
            ++free_pages;
            if (end-page<=AURORA_PAGE_SIZE) break;
            page+=AURORA_PAGE_SIZE;
        }
    }
    return free_pages!=0u;
}

uint64_t pmm_alloc_page(void) {
    aurora_spinlock_irq_state irq=spinlock_lock_irqsave(&pmm_lock);
    if (free_head==0u) {
        if (free_pages!=0u)
            kernel_panic("PMM free-list/count mismatch");
        spinlock_unlock_irqrestore(&pmm_lock,irq);
        return 0u;
    }
    uint64_t physical=free_head,index=0u;
    struct pmm_region *r=region_for_page_locked(physical,&index);
    if (r==NULL || bitmap_get(r,index) || free_pages==0u)
        kernel_panic("PMM free-list ownership corruption");
    const struct free_page_node *node=pmm_phys_to_virt(physical);
    uint64_t next=node->next;
    if (next!=0u) {
        uint64_t next_index=0u;
        struct pmm_region *next_region=
            region_for_page_locked(next,&next_index);
        if (next_region==NULL || bitmap_get(next_region,next_index))
            kernel_panic("PMM corrupted free-list next pointer");
    }
    free_head=next;
    bitmap_set(r,index,true);
    --free_pages;
    spinlock_unlock_irqrestore(&pmm_lock,irq);

    /* Scrub outside the lock; page is exclusively owned. */
    uint8_t *bytes=pmm_phys_to_virt(physical);
    for (uint64_t i=0u;i<AURORA_PAGE_SIZE;++i) bytes[i]=0u;
    return physical;
}

bool pmm_try_free_page(uint64_t physical) {
    aurora_spinlock_irq_state irq=spinlock_lock_irqsave(&pmm_lock);
    uint64_t index=0u;
    struct pmm_region *r=region_for_page_locked(physical,&index);
    if (r==NULL || !bitmap_get(r,index)) {
        spinlock_unlock_irqrestore(&pmm_lock,irq);
        return false;
    }
    if (free_pages>=total_pages)
        kernel_panic("PMM ownership/free count mismatch");
    /* Reject a double free or unowned/metadata page BEFORE modifying its
     * first bytes, its allocation bit, or any free-list counters. */
    struct free_page_node *node=pmm_phys_to_virt(physical);
    node->next=free_head;
    free_head=physical;
    bitmap_set(r,index,false);
    ++free_pages;
    spinlock_unlock_irqrestore(&pmm_lock,irq);
    return true;
}
void pmm_free_page(uint64_t physical) {
    (void)pmm_try_free_page(physical);
}
struct pmm_stats pmm_get_stats(void) {
    aurora_spinlock_irq_state irq=spinlock_lock_irqsave(&pmm_lock);
    struct pmm_stats result={
        .total_pages=total_pages,
        .free_pages=free_pages,
        .allocated_pages=total_pages-free_pages
    };
    spinlock_unlock_irqrestore(&pmm_lock,irq);
    return result;
}

/* Real early-boot physical page tests. All allocations and releases are
 * deterministic and run before AP startup; no heap allocations are used. */
bool pmm_self_test(void) {
    struct pmm_stats initial=pmm_get_stats();
    if (initial.free_pages<5u ||
        initial.total_pages!=initial.free_pages+initial.allocated_pages ||
        managed_region_count==0u)
        return false;

    uint64_t pages[4]={0u,0u,0u,0u};
    uint32_t count=0u;
    bool ok=true;
    for (uint32_t i=0u;i<4u;++i) {
        uint64_t physical=pmm_alloc_page();
        if (physical==0u || (physical&(AURORA_PAGE_SIZE-1u))!=0u) {
            ok=false;
            break;
        }
        bool distinct=true;
        for (uint32_t j=0u;j<count;++j)
            if (pages[j]==physical) distinct=false;
        if (!distinct) { ok=false; break; }
        pages[count++]=physical;
        uint8_t *bytes=pmm_phys_to_virt(physical);
        if (bytes==NULL) { ok=false; break; }
        for (uint64_t j=0u;j<AURORA_PAGE_SIZE;++j)
            if (bytes[j]!=0u) { ok=false; break; }
        bytes[0]=0xa5u;
        bytes[AURORA_PAGE_SIZE-1u]=0x5au;
        if (!ok) break;
    }
    struct pmm_stats middle=pmm_get_stats();
    if (middle.total_pages!=initial.total_pages ||
        middle.free_pages!=initial.free_pages-count ||
        middle.allocated_pages!=initial.allocated_pages+count)
        ok=false;
    if (count!=0u) {
        pmm_free_page(0u);
        pmm_free_page(pages[0]+1u);
        /* Reject a clearly foreign high physical address and a metadata
         * page reserved by the PMM itself, while pages[] remain owned. */
        if (pmm_try_free_page(UINT64_MAX & ~(AURORA_PAGE_SIZE-1u)) ||
            pmm_try_free_page(managed_regions[0].metadata_start))
            ok=false;
        struct pmm_stats invalid=pmm_get_stats();
        if (invalid.free_pages!=middle.free_pages ||
            invalid.allocated_pages!=middle.allocated_pages)
            ok=false;
    }
    for (uint32_t i=count;i>0u;--i)
        if (!pmm_try_free_page(pages[i-1u])) ok=false;

    /* A double free must fail WITHOUT altering free-list state or counters. */
    if (count!=0u && pmm_try_free_page(pages[0])) ok=false;
    struct pmm_stats restored=pmm_get_stats();
    if (restored.total_pages!=initial.total_pages ||
        restored.free_pages!=initial.free_pages ||
        restored.allocated_pages!=initial.allocated_pages)
        ok=false;
    if (!ok || count!=4u) return false;

    uint64_t reused=pmm_alloc_page();
    if (reused==0u) return false;
    bool reused_ok=reused==pages[0];
    const uint8_t *bytes=pmm_phys_to_virt(reused);
    if (bytes==NULL) reused_ok=false;
    if (bytes!=NULL)
        for (uint64_t i=0u;i<AURORA_PAGE_SIZE;++i)
            if (bytes[i]!=0u) { reused_ok=false; break; }
    if (!pmm_try_free_page(reused)) reused_ok=false;
    struct pmm_stats final_stats=pmm_get_stats();
    return reused_ok &&
        final_stats.total_pages==initial.total_pages &&
        final_stats.free_pages==initial.free_pages &&
        final_stats.allocated_pages==initial.allocated_pages;
}
