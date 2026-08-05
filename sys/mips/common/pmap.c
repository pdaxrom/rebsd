/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/*
 * Shared 32-bit MIPS pmap.
 *
 * The software page tables use ordinary 4K vm_page objects.  Hardware TLB
 * entries map pairs of 4K pages and are populated on demand.  ASID zero and
 * the wired TLB entries remain available to the bootstrap mappings until
 * vmspace replaces them.
 */

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include <vm/pmap.h>
#include <mips/common/exception_diagnostics.h>

#define PMAP_DIRECTORY_ENTRIES  1024u
#define PMAP_TABLE_ENTRIES      1024u
#define PMAP_DIRECTORY_SHIFT    22u
#define PMAP_TABLE_SHIFT        VM_PAGE_SHIFT
#define PMAP_INDEX_MASK         0x3ffu
#define PMAP_USER_END           0x80000000u
#define PMAP_TLB_PAIR_MASK      0xffffe000u
#define PMAP_ASID_MASK          0xffu
#define PMAP_ASID_FIRST         1u
#define PMAP_ASID_LAST          255u
#define PMAP_PV_BUCKETS         256u
#ifdef CI20
#define PMAP_DIRECT_MAX_ADDRESS 0x1fffffffu
#endif

#define PMAP_PTE_PRESENT        0x001u
#define PMAP_PTE_READ           0x002u
#define PMAP_PTE_WRITE          0x004u
#define PMAP_PTE_EXECUTE        0x008u
#define PMAP_PTE_REFERENCED     0x010u
#define PMAP_PTE_MODIFIED       0x020u
#define PMAP_PTE_UNCACHED       0x040u
#define PMAP_PTE_DEVICE         0x080u
#define PMAP_PTE_FLAGS          0xfffu
#define PMAP_PTE_PADDR          0xfffff000u

#define PMAP_TLB_VALID          0x00000002u
#define PMAP_TLB_DIRTY          0x00000004u
#define PMAP_TLB_CACHE_SHIFT    3u
#define PMAP_TLB_CACHE_UNCACHED 2u
#define PMAP_TLB_CACHE_CACHED   3u

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
#define PMAP_MAX_MAPS           (NPROC + 4)
#define pmap_zero(p, n)         bzero((caddr_t)(p), (unsigned)(n))
#else
#define PMAP_MAX_MAPS           32
#define pmap_zero(p, n)         memset((p), 0, (n))
#endif

/* Low-level MIPS operations supplied by pmap_machdep.c or the host test. */
extern unsigned pmap_md_tlb_entries(void);
extern unsigned pmap_md_wired_entries(void);
extern int pmap_md_cache_init(void);
extern void pmap_md_cache_activate(void);
extern void pmap_md_activate(unsigned);
extern void pmap_md_tlb_update(unsigned, unsigned, unsigned);
extern int pmap_md_tlb_invalidate(unsigned);
extern void pmap_md_tlb_flush(void);
#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
extern int pmap_md_tlb_lookup(unsigned, unsigned *, unsigned *, unsigned *,
    unsigned *, unsigned *);
#endif
extern void *pmap_md_direct_map(vm_paddr_t, vm_size_t, enum pmap_cache);
extern int pmap_md_page_sync(vm_paddr_t, unsigned);
extern int pmap_md_range_sync(vm_paddr_t, vm_size_t, unsigned);
extern vm_paddr_t pmap_md_cache_alias_mask(void);

struct pmap {
    uint32_t       *pm_directory;
    vm_paddr_t      pm_directory_paddr;
    uint32_t        pm_generation;
    unsigned        pm_asid;
    unsigned        pm_in_use;
};

struct pmap_pv_page;

struct pmap_pv_entry {
    struct pmap_pv_entry *pve_hash_next;
    struct pmap_pv_entry *pve_free_next;
    struct pmap_pv_page  *pve_page;
    struct pmap          *pve_pmap;
    vm_vaddr_t            pve_vaddr;
    vm_paddr_t            pve_paddr;
};

struct pmap_pv_page {
    struct pmap_pv_page *pvp_next;
    struct vm_page      *pvp_backing;
    unsigned             pvp_entries;
    unsigned             pvp_free;
};

static struct pmap pmap_maps[PMAP_MAX_MAPS];
static struct vm_page_allocator *pmap_allocator;
static struct pmap *pmap_active;
static struct pmap_pv_entry *pmap_pv_hash[PMAP_PV_BUCKETS];
static struct pmap_pv_entry *pmap_pv_free;
static struct pmap_pv_page *pmap_pv_pages;
static struct pmap_stats pmap_statistics;
static struct pmap_tlb_diagnostics pmap_tlb_diagnostics;
static uint32_t pmap_asid_generation;
static unsigned pmap_next_asid;
static unsigned pmap_initialized;

/* Read directly by the low-level TLB refill vector. */
uint32_t *mips_pmap_fast_directory;
volatile unsigned mips_pmap_fast_refills;
volatile unsigned mips_pmap_fast_scratch;
volatile unsigned mips_pmap_fast_last_hash;
volatile unsigned mips_pmap_fast_repeat;
volatile unsigned
    mips_pmap_fast_diagnostics[MIPS_REFILL_DIAG_WORDS]
    __attribute__((aligned(8)));

static unsigned
pmap_fast_diagnostic(unsigned offset)
{
    return mips_pmap_fast_diagnostics[offset / sizeof(unsigned)];
}

static void
pmap_stat_increment(vm_pfn_t *value)
{
    if (*value != VM_PFN_MAX)
        ++*value;
}

static vm_pfn_t
pmap_total_tlb_refills(void)
{
    vm_pfn_t slow;
    unsigned fast;

    slow = pmap_statistics.pms_tlb_refills;
    fast = mips_pmap_fast_refills;
    if (fast > VM_PFN_MAX - slow)
        return VM_PFN_MAX;
    return slow + fast;
}

static int
pmap_valid(const struct pmap *pmap)
{
    return pmap != 0 && pmap >= &pmap_maps[0] &&
        pmap < &pmap_maps[PMAP_MAX_MAPS] && pmap->pm_in_use != 0;
}

static unsigned
pmap_pv_bucket(vm_paddr_t paddr)
{
    return (unsigned)((paddr >> VM_PAGE_SHIFT) & (PMAP_PV_BUCKETS - 1));
}

static struct pmap_pv_entry *
pmap_pv_entries(struct pmap_pv_page *page)
{
    return (struct pmap_pv_entry *)((unsigned char *)page +
        sizeof(*page));
}

static int
pmap_pv_grow(void)
{
    struct vm_page_request request;
    struct pmap_pv_entry *entries;
    struct pmap_pv_page *page;
    struct vm_page *backing;
    void *mapping;
    unsigned count;
    unsigned index;
    int error;

    vm_page_request_init(&request);
    request.vpr_state = VM_PAGE_WIRED;
#ifdef CI20
    /*
     * Reverse-map slabs are accessed through KSEG0 just like page-table
     * pages, so they must remain inside the direct-mapped physical window.
     */
    request.vpr_max_address = PMAP_DIRECT_MAX_ADDRESS;
#endif
    error = vm_page_alloc(pmap_allocator, &request, &backing);
    if (error != 0)
        return error;
    mapping = pmap_md_direct_map(backing->vmp_paddr, VM_PAGE_SIZE,
        PMAP_CACHE_CACHED);
    if (mapping == 0) {
        (void)vm_page_counter_dec(pmap_allocator, backing,
            VM_PAGE_COUNTER_WIRE);
        (void)vm_page_free(pmap_allocator, backing, 1);
        return EFAULT;
    }
    count = (VM_PAGE_SIZE - sizeof(*page)) / sizeof(*entries);
    if (count == 0) {
        (void)vm_page_counter_dec(pmap_allocator, backing,
            VM_PAGE_COUNTER_WIRE);
        (void)vm_page_free(pmap_allocator, backing, 1);
        return ENOSPC;
    }
    pmap_zero(mapping, VM_PAGE_SIZE);
    page = (struct pmap_pv_page *)mapping;
    page->pvp_backing = backing;
    page->pvp_entries = count;
    page->pvp_free = count;
    page->pvp_next = pmap_pv_pages;
    pmap_pv_pages = page;
    entries = pmap_pv_entries(page);
    for (index = 0; index < count; ++index) {
        entries[index].pve_page = page;
        entries[index].pve_free_next = pmap_pv_free;
        pmap_pv_free = &entries[index];
    }
    return 0;
}

static int
pmap_pv_reserve(void)
{
    return pmap_pv_free != 0 ? 0 : pmap_pv_grow();
}

static struct pmap_pv_entry *
pmap_pv_find(struct pmap *pmap, vm_vaddr_t vaddr, vm_paddr_t paddr)
{
    struct pmap_pv_entry *entry;

    for (entry = pmap_pv_hash[pmap_pv_bucket(paddr)]; entry != 0;
        entry = entry->pve_hash_next) {
        if (entry->pve_pmap == pmap && entry->pve_vaddr == vaddr &&
            entry->pve_paddr == paddr)
            return entry;
    }
    return 0;
}

static int
pmap_pv_insert(struct pmap *pmap, vm_vaddr_t vaddr, vm_paddr_t paddr)
{
    struct pmap_pv_entry *entry;
    unsigned bucket;
    int error;

    if (pmap_pv_find(pmap, vaddr, paddr) != 0)
        return EEXIST;
    error = pmap_pv_reserve();
    if (error != 0)
        return error;
    entry = pmap_pv_free;
    pmap_pv_free = entry->pve_free_next;
    --entry->pve_page->pvp_free;
    entry->pve_free_next = 0;
    entry->pve_pmap = pmap;
    entry->pve_vaddr = vaddr;
    entry->pve_paddr = paddr;
    bucket = pmap_pv_bucket(paddr);
    entry->pve_hash_next = pmap_pv_hash[bucket];
    pmap_pv_hash[bucket] = entry;
    return 0;
}

static int
pmap_pv_release_page(struct pmap_pv_page *page)
{
    struct pmap_pv_entry **free_link;
    struct pmap_pv_entry *removed_entries;
    struct pmap_pv_entry *entry;
    struct pmap_pv_page **page_link;
    struct pmap_pv_page *page_next;
    struct vm_page *backing;
    unsigned removed;
    int error;

    if (page->pvp_free != page->pvp_entries)
        return EBUSY;
    removed = 0;
    removed_entries = 0;
    free_link = &pmap_pv_free;
    while (*free_link != 0) {
        if ((*free_link)->pve_page != page) {
            free_link = &(*free_link)->pve_free_next;
            continue;
        }
        entry = *free_link;
        *free_link = entry->pve_free_next;
        entry->pve_free_next = removed_entries;
        removed_entries = entry;
        ++removed;
    }
    if (removed != page->pvp_entries) {
        while (removed_entries != 0) {
            entry = removed_entries;
            removed_entries = entry->pve_free_next;
            entry->pve_free_next = pmap_pv_free;
            pmap_pv_free = entry;
        }
        return EFAULT;
    }
    for (page_link = &pmap_pv_pages; *page_link != 0;
        page_link = &(*page_link)->pvp_next) {
        if (*page_link == page)
            break;
    }
    if (*page_link == 0) {
        while (removed_entries != 0) {
            entry = removed_entries;
            removed_entries = entry->pve_free_next;
            entry->pve_free_next = pmap_pv_free;
            pmap_pv_free = entry;
        }
        return EFAULT;
    }
    page_next = page->pvp_next;
    backing = page->pvp_backing;
    *page_link = page_next;
    error = vm_page_counter_dec(pmap_allocator, backing,
        VM_PAGE_COUNTER_WIRE);
    if (error != 0) {
        page->pvp_next = pmap_pv_pages;
        pmap_pv_pages = page;
        while (removed_entries != 0) {
            entry = removed_entries;
            removed_entries = entry->pve_free_next;
            entry->pve_free_next = pmap_pv_free;
            pmap_pv_free = entry;
        }
        return error;
    }
    error = vm_page_free(pmap_allocator, backing, 1);
    /*
     * The poison callback may already have overwritten the backing page
     * even when it reports an error.  The slab has therefore been detached
     * permanently and neither page nor its entries may be accessed here.
     */
    return error;
}

static int
pmap_pv_remove(struct pmap *pmap, vm_vaddr_t vaddr, vm_paddr_t paddr)
{
    struct pmap_pv_entry **link;
    struct pmap_pv_entry *entry;
    struct pmap_pv_page *page;
    unsigned bucket;

    bucket = pmap_pv_bucket(paddr);
    for (link = &pmap_pv_hash[bucket]; *link != 0;
        link = &(*link)->pve_hash_next) {
        entry = *link;
        if (entry->pve_pmap == pmap && entry->pve_vaddr == vaddr &&
            entry->pve_paddr == paddr)
            break;
    }
    if (*link == 0)
        return ENOENT;
    entry = *link;
    page = entry->pve_page;
    *link = entry->pve_hash_next;
    entry->pve_hash_next = 0;
    entry->pve_pmap = 0;
    entry->pve_vaddr = 0;
    entry->pve_paddr = 0;
    entry->pve_free_next = pmap_pv_free;
    pmap_pv_free = entry;
    ++page->pvp_free;
    return 0;
}

static int
pmap_pv_release_empty_pages(unsigned keep)
{
    struct pmap_pv_page *page;
    struct pmap_pv_page *next;
    unsigned retained;
    int error;

    /*
     * Keep one empty slab as the global reserve.  Releasing every last
     * entry would make a map/unmap workload allocate, zero, and free a
     * wired page on each cycle.
     */
    retained = 0;
    for (page = pmap_pv_pages; page != 0; page = next) {
        next = page->pvp_next;
        if (page->pvp_free != page->pvp_entries)
            continue;
        if (retained < keep) {
            ++retained;
            continue;
        }
        error = pmap_pv_release_page(page);
        if (error != 0)
            return error;
    }
    return 0;
}

static int
pmap_protection_valid(vm_prot_t protection, int allow_none)
{
    if ((protection & ~VM_PROT_ALL) != 0)
        return 0;
    return allow_none || protection != VM_PROT_NONE;
}

static uint32_t
pmap_protection_bits(vm_prot_t protection)
{
    uint32_t bits;

    bits = 0;
    if ((protection & VM_PROT_READ) != 0)
        bits |= PMAP_PTE_READ;
    if ((protection & VM_PROT_WRITE) != 0)
        bits |= PMAP_PTE_WRITE;
    if ((protection & VM_PROT_EXECUTE) != 0)
        bits |= PMAP_PTE_EXECUTE;
    return bits;
}

static unsigned
pmap_directory_index(vm_vaddr_t vaddr)
{
    return (vaddr >> PMAP_DIRECTORY_SHIFT) & PMAP_INDEX_MASK;
}

static unsigned
pmap_table_index(vm_vaddr_t vaddr)
{
    return (vaddr >> PMAP_TABLE_SHIFT) & PMAP_INDEX_MASK;
}

static int
pmap_alloc_table_page(vm_paddr_t *paddr, uint32_t **address)
{
    struct vm_page_request request;
    struct vm_page *page;
    void *mapping;
    int error;

    vm_page_request_init(&request);
    request.vpr_state = VM_PAGE_WIRED;
#ifdef CI20
    /*
     * The refill vector accesses page-table pages through KSEG0, so these
     * pages must remain in the directly mapped low physical window.
     */
    request.vpr_max_address = PMAP_DIRECT_MAX_ADDRESS;
#endif
    error = vm_page_alloc(pmap_allocator, &request, &page);
    if (error != 0)
        return error;
    mapping = pmap_md_direct_map(page->vmp_paddr, VM_PAGE_SIZE,
        PMAP_CACHE_CACHED);
    if (mapping == 0) {
        (void)vm_page_counter_dec(pmap_allocator, page,
            VM_PAGE_COUNTER_WIRE);
        (void)vm_page_free(pmap_allocator, page, 1);
        return EFAULT;
    }
    pmap_zero(mapping, VM_PAGE_SIZE);
    *paddr = page->vmp_paddr;
    *address = (uint32_t *)mapping;
    return 0;
}

static int
pmap_free_table_page(vm_paddr_t paddr)
{
    struct vm_page *page;
    int error;

    page = vm_page_lookup(pmap_allocator, paddr);
    if (page == 0)
        return ENOENT;
    error = vm_page_counter_dec(pmap_allocator, page,
        VM_PAGE_COUNTER_WIRE);
    if (error != 0)
        return error;
    error = vm_page_free(pmap_allocator, page, 1);
    if (error != 0) {
        (void)vm_page_counter_inc(pmap_allocator, page,
            VM_PAGE_COUNTER_WIRE);
        return error;
    }
    return 0;
}

static uint32_t *
pmap_table(struct pmap *pmap, unsigned directory_index)
{
    vm_paddr_t paddr;

    paddr = pmap->pm_directory[directory_index];
    if ((paddr & PMAP_PTE_PRESENT) == 0)
        return 0;
    paddr &= PMAP_PTE_PADDR;
    return (uint32_t *)pmap_md_direct_map(paddr, VM_PAGE_SIZE,
        PMAP_CACHE_CACHED);
}

static uint32_t *
pmap_lookup_pte(struct pmap *pmap, vm_vaddr_t vaddr)
{
    uint32_t *table;

    table = pmap_table(pmap, pmap_directory_index(vaddr));
    if (table == 0)
        return 0;
    return &table[pmap_table_index(vaddr)];
}

static int
pmap_get_pte(struct pmap *pmap, vm_vaddr_t vaddr, int create,
    uint32_t **result)
{
    vm_paddr_t table_paddr;
    uint32_t *table;
    unsigned index;
    int error;

    index = pmap_directory_index(vaddr);
    table = pmap_table(pmap, index);
    if (table == 0 && create) {
        error = pmap_alloc_table_page(&table_paddr, &table);
        if (error != 0)
            return error;
        pmap->pm_directory[index] = table_paddr | PMAP_PTE_PRESENT;
    }
    if (table == 0)
        return ENOENT;
    *result = &table[pmap_table_index(vaddr)];
    return 0;
}

static int
pmap_table_empty(const uint32_t *table)
{
    unsigned i;

    for (i = 0; i < PMAP_TABLE_ENTRIES; ++i) {
        if ((table[i] & PMAP_PTE_PRESENT) != 0)
            return 0;
    }
    return 1;
}

static void
pmap_invalidate(struct pmap *pmap, vm_vaddr_t vaddr)
{
    unsigned entryhi;

    if (pmap->pm_asid == 0 ||
        pmap->pm_generation != pmap_asid_generation)
        return;
    entryhi = (vaddr & PMAP_TLB_PAIR_MASK) | pmap->pm_asid;
    if (pmap_md_tlb_invalidate(entryhi) != 0)
        pmap_stat_increment(&pmap_statistics.pms_targeted_invalidations);
}

static unsigned
pmap_tlb_entrylo(uint32_t pte)
{
    unsigned cache;
    unsigned entrylo;

    if ((pte & PMAP_PTE_PRESENT) == 0)
        return 0;
    cache = (pte & PMAP_PTE_UNCACHED) != 0 ?
        PMAP_TLB_CACHE_UNCACHED : PMAP_TLB_CACHE_CACHED;
    entrylo = ((pte & PMAP_PTE_PADDR) >> 6) |
        (cache << PMAP_TLB_CACHE_SHIFT) | PMAP_TLB_VALID;
    if ((pte & PMAP_PTE_WRITE) != 0 &&
        (pte & PMAP_PTE_MODIFIED) != 0)
        entrylo |= PMAP_TLB_DIRTY;
    return entrylo;
}

static int
pmap_remove_pte(struct pmap *pmap, vm_vaddr_t vaddr, uint32_t *pte)
{
    struct vm_page *page;
    uint32_t old;
    int error;

    old = *pte;
    if ((old & PMAP_PTE_PRESENT) == 0)
        return 0;
    if ((old & PMAP_PTE_DEVICE) != 0) {
        *pte = 0;
        --pmap_statistics.pms_mappings;
        --pmap_statistics.pms_resident_pages;
        pmap_invalidate(pmap, vaddr);
        return 0;
    }
    page = vm_page_lookup(pmap_allocator, old & PMAP_PTE_PADDR);
    if (page == 0)
        return EFAULT;
    if (pmap_pv_find(pmap, vaddr, page->vmp_paddr) == 0)
        return EFAULT;
    /*
     * A user mapping may be the last cached alias of this page.  Write it
     * back before dropping the hold so a later direct-map user cannot see
     * stale data after the page is recycled.
     */
    error = pmap_md_page_sync(page->vmp_paddr, PMAP_SYNC_DATA);
    if (error != 0)
        return error;
    if ((old & PMAP_PTE_MODIFIED) != 0) {
        error = vm_page_counter_dec(pmap_allocator, page,
            VM_PAGE_COUNTER_DIRTY);
        if (error != 0)
            return error;
    }
    if ((old & PMAP_PTE_REFERENCED) != 0) {
        error = vm_page_counter_dec(pmap_allocator, page,
            VM_PAGE_COUNTER_REFERENCE);
        if (error != 0) {
            if ((old & PMAP_PTE_MODIFIED) != 0)
                (void)vm_page_counter_inc(pmap_allocator, page,
                    VM_PAGE_COUNTER_DIRTY);
            return error;
        }
    }
    error = vm_page_counter_dec(pmap_allocator, page,
        VM_PAGE_COUNTER_HOLD);
    if (error != 0) {
        if ((old & PMAP_PTE_REFERENCED) != 0)
            (void)vm_page_counter_inc(pmap_allocator, page,
                VM_PAGE_COUNTER_REFERENCE);
        if ((old & PMAP_PTE_MODIFIED) != 0)
            (void)vm_page_counter_inc(pmap_allocator, page,
                VM_PAGE_COUNTER_DIRTY);
        return error;
    }
    error = pmap_pv_remove(pmap, vaddr, page->vmp_paddr);
    if (error != 0) {
        (void)vm_page_counter_inc(pmap_allocator, page,
            VM_PAGE_COUNTER_HOLD);
        if ((old & PMAP_PTE_REFERENCED) != 0)
            (void)vm_page_counter_inc(pmap_allocator, page,
                VM_PAGE_COUNTER_REFERENCE);
        if ((old & PMAP_PTE_MODIFIED) != 0)
            (void)vm_page_counter_inc(pmap_allocator, page,
                VM_PAGE_COUNTER_DIRTY);
        return error;
    }
    *pte = 0;
    --pmap_statistics.pms_mappings;
    --pmap_statistics.pms_resident_pages;
    pmap_invalidate(pmap, vaddr);
    return 0;
}

int
pmap_system_init(struct vm_page_allocator *allocator)
{
    int error;

    if (allocator == 0 || allocator->vpa_initialized == 0)
        return EINVAL;
    if (pmap_md_tlb_entries() == 0 ||
        pmap_md_wired_entries() >= pmap_md_tlb_entries())
        return ENOSPC;
    pmap_zero(pmap_maps, sizeof(pmap_maps));
    pmap_zero(pmap_pv_hash, sizeof(pmap_pv_hash));
    pmap_zero(&pmap_statistics, sizeof(pmap_statistics));
    pmap_zero(&pmap_tlb_diagnostics, sizeof(pmap_tlb_diagnostics));
    pmap_allocator = allocator;
    pmap_active = 0;
    pmap_pv_free = 0;
    pmap_pv_pages = 0;
    mips_pmap_fast_directory = 0;
    mips_pmap_fast_refills = 0;
    mips_pmap_fast_scratch = 0;
    mips_pmap_fast_last_hash = 0;
    mips_pmap_fast_repeat = 0;
    pmap_zero((void *)mips_pmap_fast_diagnostics,
        sizeof(mips_pmap_fast_diagnostics));
    pmap_asid_generation = 1;
    pmap_next_asid = PMAP_ASID_FIRST;
    error = pmap_md_cache_init();
    if (error != 0)
        return error;
    pmap_initialized = 1;
    pmap_md_activate(0);
    pmap_md_tlb_flush();
    pmap_stat_increment(&pmap_statistics.pms_full_flushes);
    return 0;
}

int
pmap_create(struct pmap **result)
{
    struct pmap *pmap;
    vm_paddr_t directory_paddr;
    uint32_t *directory;
    unsigned i;
    int error;

    if (!pmap_initialized || result == 0)
        return EINVAL;
    *result = 0;
    pmap = 0;
    for (i = 0; i < PMAP_MAX_MAPS; ++i) {
        if (pmap_maps[i].pm_in_use == 0) {
            pmap = &pmap_maps[i];
            break;
        }
    }
    if (pmap == 0)
        return ENOSPC;
    error = pmap_alloc_table_page(&directory_paddr, &directory);
    if (error != 0)
        return error;
    pmap_zero(pmap, sizeof(*pmap));
    pmap->pm_directory = directory;
    pmap->pm_directory_paddr = directory_paddr;
    pmap->pm_in_use = 1;
    *result = pmap;
    return 0;
}

int
pmap_destroy(struct pmap *pmap)
{
    uint32_t *table;
    vm_paddr_t table_paddr;
    vm_vaddr_t vaddr;
    unsigned directory_index;
    unsigned table_index;
    int error;

    if (!pmap_valid(pmap))
        return EINVAL;
    if (pmap_active == pmap)
        pmap_deactivate(pmap);
    for (directory_index = 0;
        directory_index < PMAP_DIRECTORY_ENTRIES; ++directory_index) {
        table_paddr = pmap->pm_directory[directory_index];
        if ((table_paddr & PMAP_PTE_PRESENT) == 0)
            continue;
        table_paddr &= PMAP_PTE_PADDR;
        table = pmap_table(pmap, directory_index);
        if (table == 0)
            return EFAULT;
        for (table_index = 0; table_index < PMAP_TABLE_ENTRIES;
            ++table_index) {
            if ((table[table_index] & PMAP_PTE_PRESENT) == 0)
                continue;
            vaddr = (directory_index << PMAP_DIRECTORY_SHIFT) |
                (table_index << PMAP_TABLE_SHIFT);
            error = pmap_remove_pte(pmap, vaddr, &table[table_index]);
            if (error != 0)
                return error;
        }
        pmap->pm_directory[directory_index] = 0;
        error = pmap_free_table_page(table_paddr);
        if (error != 0)
            return error;
    }
    error = pmap_free_table_page(pmap->pm_directory_paddr);
    if (error != 0)
        return error;
    pmap_zero(pmap, sizeof(*pmap));
    return pmap_pv_release_empty_pages(0);
}

int
pmap_prepare(struct pmap *pmap, vm_vaddr_t vaddr)
{
    uint32_t *pte;
    int error;

    if (!pmap_valid(pmap) || !vm_vaddr_page_aligned(vaddr) ||
        vaddr >= PMAP_USER_END)
        return EINVAL;
    error = pmap_get_pte(pmap, vaddr, 1, &pte);
    if (error != 0)
        return error;
    return pmap_pv_reserve();
}

int
pmap_enter(struct pmap *pmap, vm_vaddr_t vaddr, struct vm_page *page,
    vm_prot_t protection, enum pmap_cache cache)
{
    uint32_t *pte;
    uint32_t entry;
    vm_paddr_t alias_mask;
    unsigned sync_operations;
    int error;

    if (!pmap_valid(pmap) || page == 0 ||
        !vm_vaddr_page_aligned(vaddr) || vaddr >= PMAP_USER_END ||
        !pmap_protection_valid(protection, 0) ||
        (cache != PMAP_CACHE_CACHED && cache != PMAP_CACHE_UNCACHED))
        return EINVAL;
    if (vm_page_lookup(pmap_allocator, page->vmp_paddr) != page)
        return EINVAL;
    alias_mask = pmap_md_cache_alias_mask();
    if (cache == PMAP_CACHE_CACHED &&
        ((vaddr ^ page->vmp_paddr) & alias_mask) != 0)
        return EINVAL;
    if (page->vmp_state == VM_PAGE_FREE ||
        page->vmp_state == VM_PAGE_RESERVED ||
        page->vmp_state == VM_PAGE_BAD)
        return EPERM;
    error = pmap_get_pte(pmap, vaddr, 1, &pte);
    if (error != 0)
        return error;
    error = pmap_pv_reserve();
    if (error != 0)
        return error;
    if ((*pte & PMAP_PTE_PRESENT) != 0) {
        error = pmap_remove_pte(pmap, vaddr, pte);
        if (error != 0)
            return error;
    }
    error = vm_page_counter_inc(pmap_allocator, page,
        VM_PAGE_COUNTER_HOLD);
    if (error != 0)
        return error;
    error = pmap_pv_insert(pmap, vaddr, page->vmp_paddr);
    if (error != 0) {
        (void)vm_page_counter_dec(pmap_allocator, page,
            VM_PAGE_COUNTER_HOLD);
        return error;
    }
    entry = page->vmp_paddr | PMAP_PTE_PRESENT |
        pmap_protection_bits(protection);
    if (cache == PMAP_CACHE_UNCACHED)
        entry |= PMAP_PTE_UNCACHED;
    *pte = entry;
    pmap_stat_increment(&pmap_statistics.pms_mappings);
    pmap_stat_increment(&pmap_statistics.pms_resident_pages);
    pmap_invalidate(pmap, vaddr);
    sync_operations = PMAP_SYNC_DATA;
    if ((protection & VM_PROT_EXECUTE) != 0)
        sync_operations |= PMAP_SYNC_INSTRUCTION;
    error = pmap_md_page_sync(page->vmp_paddr, sync_operations);
    if (error != 0) {
        (void)pmap_remove_pte(pmap, vaddr, pte);
        return error;
    }
    return 0;
}

int
pmap_enter_device(struct pmap *pmap, vm_vaddr_t vaddr, vm_paddr_t paddr,
    vm_prot_t protection, enum pmap_cache cache)
{
    struct vm_page *page;
    uint32_t *pte;
    uint32_t entry;
    int error;

    if (!pmap_valid(pmap) || !vm_vaddr_page_aligned(vaddr) ||
        vaddr >= PMAP_USER_END || !vm_paddr_page_aligned(paddr) ||
        paddr > PMAP_PTE_PADDR || !pmap_protection_valid(protection, 0) ||
        (protection & VM_PROT_EXECUTE) != 0 ||
        (cache != PMAP_CACHE_CACHED && cache != PMAP_CACHE_UNCACHED))
        return EINVAL;
    page = vm_page_lookup(pmap_allocator, paddr);
    if (page != 0 && page->vmp_state != VM_PAGE_RESERVED &&
        !(page->vmp_state == VM_PAGE_WIRED &&
        (page->vmp_flags & VM_PAGE_FLAG_DEVICE) != 0))
        return EBUSY;
    error = pmap_get_pte(pmap, vaddr, 1, &pte);
    if (error != 0)
        return error;
    if ((*pte & PMAP_PTE_PRESENT) != 0) {
        error = pmap_remove_pte(pmap, vaddr, pte);
        if (error != 0)
            return error;
    }
    entry = paddr | PMAP_PTE_PRESENT | PMAP_PTE_DEVICE |
        pmap_protection_bits(protection);
    if (cache == PMAP_CACHE_UNCACHED)
        entry |= PMAP_PTE_UNCACHED;
    *pte = entry;
    pmap_stat_increment(&pmap_statistics.pms_mappings);
    pmap_stat_increment(&pmap_statistics.pms_resident_pages);
    pmap_invalidate(pmap, vaddr);
    return 0;
}

int
pmap_remove(struct pmap *pmap, vm_vaddr_t start, vm_vaddr_t end)
{
    uint32_t *table;
    uint32_t *pte;
    vm_paddr_t table_paddr;
    vm_vaddr_t directory_end;
    vm_vaddr_t vaddr;
    unsigned directory_index;
    int error;

    if (!pmap_valid(pmap) || !vm_vaddr_page_aligned(start) ||
        !vm_vaddr_page_aligned(end) || start >= end ||
        end > PMAP_USER_END)
        return EINVAL;
    vaddr = start;
    while (vaddr < end) {
        directory_index = pmap_directory_index(vaddr);
        table = pmap_table(pmap, directory_index);
        directory_end = (vaddr & 0xffc00000u) + 0x00400000u;
        if (directory_end > end)
            directory_end = end;
        if (table == 0) {
            vaddr = directory_end;
            continue;
        }
        while (vaddr < directory_end) {
            pte = &table[pmap_table_index(vaddr)];
            error = pmap_remove_pte(pmap, vaddr, pte);
            if (error != 0)
                return error;
            vaddr += VM_PAGE_SIZE;
        }
        if (pmap_table_empty(table)) {
            table_paddr = pmap->pm_directory[directory_index] &
                PMAP_PTE_PADDR;
            pmap->pm_directory[directory_index] = 0;
            error = pmap_free_table_page(table_paddr);
            if (error != 0)
                return error;
        }
    }
    return pmap_pv_release_empty_pages(1);
}

int
pmap_protect(struct pmap *pmap, vm_vaddr_t start, vm_vaddr_t end,
    vm_prot_t protection)
{
    struct vm_page *page;
    uint32_t *pte;
    uint32_t preserve;
    vm_vaddr_t vaddr;
    int error;

    if (!pmap_valid(pmap) || !vm_vaddr_page_aligned(start) ||
        !vm_vaddr_page_aligned(end) || start >= end ||
        end > PMAP_USER_END || !pmap_protection_valid(protection, 1))
        return EINVAL;
    if (protection == VM_PROT_NONE)
        return pmap_remove(pmap, start, end);
    if ((protection & VM_PROT_EXECUTE) != 0) {
        for (vaddr = start; vaddr < end; vaddr += VM_PAGE_SIZE) {
            pte = pmap_lookup_pte(pmap, vaddr);
            if (pte != 0 && (*pte & PMAP_PTE_PRESENT) != 0 &&
                (*pte & PMAP_PTE_DEVICE) != 0)
                return EACCES;
        }
    }
    for (vaddr = start; vaddr < end; vaddr += VM_PAGE_SIZE) {
        pte = pmap_lookup_pte(pmap, vaddr);
        if (pte == 0 || (*pte & PMAP_PTE_PRESENT) == 0)
            continue;
        preserve = *pte & (PMAP_PTE_PADDR | PMAP_PTE_PRESENT |
            PMAP_PTE_REFERENCED | PMAP_PTE_MODIFIED |
            PMAP_PTE_UNCACHED | PMAP_PTE_DEVICE);
        *pte = preserve | pmap_protection_bits(protection);
        pmap_invalidate(pmap, vaddr);
        if ((protection & VM_PROT_EXECUTE) != 0 &&
            (*pte & PMAP_PTE_DEVICE) == 0) {
            page = vm_page_lookup(pmap_allocator,
                *pte & PMAP_PTE_PADDR);
            if (page == 0)
                return EFAULT;
            error = pmap_md_page_sync(page->vmp_paddr,
                PMAP_SYNC_DATA | PMAP_SYNC_INSTRUCTION);
            if (error != 0)
                return error;
        }
    }
    return 0;
}

int
pmap_extract(struct pmap *pmap, vm_vaddr_t vaddr, vm_paddr_t *result)
{
    uint32_t *pte;

    if (!pmap_valid(pmap) || result == 0 || vaddr >= PMAP_USER_END)
        return EINVAL;
    pte = pmap_lookup_pte(pmap, vaddr);
    if (pte == 0 || (*pte & PMAP_PTE_PRESENT) == 0)
        return ENOENT;
    *result = (*pte & PMAP_PTE_PADDR) | (vaddr & VM_PAGE_MASK);
    return 0;
}

int
pmap_activate(struct pmap *pmap)
{
#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
    int interrupt_status;
#endif

    if (!pmap_valid(pmap))
        return EINVAL;
#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
    /*
     * The fast-refill directory, EntryHi ASID, and diagnostic active pmap
     * form one hardware context.  Publish them with interrupts masked so an
     * exception can never observe a directory from one process and an ASID
     * from another.
     */
    interrupt_status = splhigh();
#endif
    if (pmap->pm_asid == 0 ||
        pmap->pm_generation != pmap_asid_generation) {
        if (pmap_next_asid > PMAP_ASID_LAST) {
            ++pmap_asid_generation;
            if (pmap_asid_generation == 0)
                pmap_asid_generation = 1;
            pmap_next_asid = PMAP_ASID_FIRST;
            pmap_md_tlb_flush();
            pmap_stat_increment(&pmap_statistics.pms_full_flushes);
            pmap_stat_increment(&pmap_statistics.pms_asid_rollovers);
        }
        pmap->pm_asid = pmap_next_asid++;
        pmap->pm_generation = pmap_asid_generation;
    }
    if (pmap_active != pmap)
        pmap_md_cache_activate();
    mips_pmap_fast_directory = pmap->pm_directory;
    pmap_md_activate(pmap->pm_asid);
    pmap_active = pmap;
#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
    splx(interrupt_status);
#endif
    return 0;
}

void
pmap_deactivate(struct pmap *pmap)
{
#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
    int interrupt_status;

    interrupt_status = splhigh();
#endif
    if (pmap_active == pmap) {
        mips_pmap_fast_directory = 0;
        pmap_md_activate(0);
        pmap_active = 0;
    }
#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
    splx(interrupt_status);
#endif
}

int
pmap_fault(struct pmap *pmap, vm_vaddr_t vaddr, vm_prot_t access,
    int user)
{
    struct vm_page *page;
    uint32_t *pte;
    uint32_t *even_pte;
    uint32_t *odd_pte;
    uint32_t entry;
    vm_vaddr_t page_vaddr;
    vm_vaddr_t pair;
    int error;

    if (!pmap_valid(pmap) || pmap_active != pmap ||
        (access != VM_PROT_READ && access != VM_PROT_WRITE &&
        access != VM_PROT_EXECUTE) || (user && vaddr >= PMAP_USER_END))
        return EINVAL;
    pte = pmap_lookup_pte(pmap, vaddr);
    if (pte == 0 || (*pte & PMAP_PTE_PRESENT) == 0)
        return ENOENT;
    entry = *pte;
    if ((access == VM_PROT_READ && (entry & PMAP_PTE_READ) == 0) ||
        (access == VM_PROT_WRITE && (entry & PMAP_PTE_WRITE) == 0) ||
        (access == VM_PROT_EXECUTE &&
        (entry & PMAP_PTE_EXECUTE) == 0)) {
        pmap_stat_increment(&pmap_statistics.pms_protection_faults);
        return EACCES;
    }
    page = 0;
    if ((entry & PMAP_PTE_DEVICE) == 0) {
        page = vm_page_lookup(pmap_allocator, entry & PMAP_PTE_PADDR);
        if (page == 0)
            return EFAULT;
    }
    if ((entry & PMAP_PTE_REFERENCED) == 0) {
        if (page != 0) {
            error = vm_page_counter_inc(pmap_allocator, page,
                VM_PAGE_COUNTER_REFERENCE);
            if (error != 0)
                return error;
        }
        *pte |= PMAP_PTE_REFERENCED;
    }
    if (access == VM_PROT_WRITE &&
        (entry & PMAP_PTE_MODIFIED) == 0) {
        if (page != 0) {
            error = vm_page_counter_inc(pmap_allocator, page,
                VM_PAGE_COUNTER_DIRTY);
            if (error != 0)
                return error;
        }
        *pte |= PMAP_PTE_MODIFIED;
        pmap_stat_increment(&pmap_statistics.pms_tlb_modified);
    }
    pair = vaddr & PMAP_TLB_PAIR_MASK;
    page_vaddr = vaddr & ~VM_PAGE_MASK;
    even_pte = pmap_lookup_pte(pmap, pair);
    odd_pte = pmap_lookup_pte(pmap, pair + VM_PAGE_SIZE);
    pmap_md_tlb_update(pair | pmap->pm_asid,
        even_pte == 0 ? 0 : pmap_tlb_entrylo(*even_pte),
        odd_pte == 0 ? 0 : pmap_tlb_entrylo(*odd_pte));
    pmap_stat_increment(&pmap_statistics.pms_tlb_refills);
    if (pmap_tlb_diagnostics.ptd_last_entryhi ==
        (pair | pmap->pm_asid) &&
        pmap_tlb_diagnostics.ptd_last_access == access) {
        if (pmap_tlb_diagnostics.ptd_repeat != ~0u)
            ++pmap_tlb_diagnostics.ptd_repeat;
    } else {
        pmap_tlb_diagnostics.ptd_repeat = 1;
    }
    pmap_tlb_diagnostics.ptd_refills = pmap_total_tlb_refills();
    pmap_tlb_diagnostics.ptd_last_pmap = (unsigned)(uintptr_t)pmap;
    pmap_tlb_diagnostics.ptd_last_vaddr = page_vaddr;
    pmap_tlb_diagnostics.ptd_last_access = access;
    pmap_tlb_diagnostics.ptd_last_entryhi = pair | pmap->pm_asid;
    pmap_tlb_diagnostics.ptd_last_entrylo0 =
        even_pte == 0 ? 0 : pmap_tlb_entrylo(*even_pte);
    pmap_tlb_diagnostics.ptd_last_entrylo1 =
        odd_pte == 0 ? 0 : pmap_tlb_entrylo(*odd_pte);
    return 0;
}

int
pmap_fault_active(vm_vaddr_t vaddr, vm_prot_t access, int user)
{
    if (pmap_active == 0)
        return ENOENT;
    return pmap_fault(pmap_active, vaddr, access, user);
}

static int
pmap_test_flag(struct pmap *pmap, vm_vaddr_t vaddr, uint32_t flag)
{
    uint32_t *pte;

    if (!pmap_valid(pmap) || vaddr >= PMAP_USER_END)
        return 0;
    pte = pmap_lookup_pte(pmap, vaddr);
    return pte != 0 && (*pte & (PMAP_PTE_PRESENT | flag)) ==
        (PMAP_PTE_PRESENT | flag);
}

int
pmap_is_referenced(struct pmap *pmap, vm_vaddr_t vaddr)
{
    return pmap_test_flag(pmap, vaddr, PMAP_PTE_REFERENCED);
}

int
pmap_clear_reference(struct pmap *pmap, vm_vaddr_t vaddr)
{
    struct vm_page *page;
    uint32_t *pte;
    int error;

    if (!pmap_valid(pmap) || vaddr >= PMAP_USER_END)
        return EINVAL;
    pte = pmap_lookup_pte(pmap, vaddr);
    if (pte == 0 || (*pte & PMAP_PTE_PRESENT) == 0)
        return ENOENT;
    if ((*pte & PMAP_PTE_REFERENCED) == 0)
        return 0;
    if ((*pte & PMAP_PTE_DEVICE) != 0) {
        *pte &= ~PMAP_PTE_REFERENCED;
        pmap_invalidate(pmap, vaddr);
        return 0;
    }
    page = vm_page_lookup(pmap_allocator, *pte & PMAP_PTE_PADDR);
    if (page == 0)
        return EFAULT;
    error = vm_page_counter_dec(pmap_allocator, page,
        VM_PAGE_COUNTER_REFERENCE);
    if (error != 0)
        return error;
    *pte &= ~PMAP_PTE_REFERENCED;
    pmap_invalidate(pmap, vaddr);
    return 0;
}

int
pmap_is_modified(struct pmap *pmap, vm_vaddr_t vaddr)
{
    return pmap_test_flag(pmap, vaddr, PMAP_PTE_MODIFIED);
}

int
pmap_clear_modify(struct pmap *pmap, vm_vaddr_t vaddr)
{
    struct vm_page *page;
    uint32_t *pte;
    int error;

    if (!pmap_valid(pmap) || vaddr >= PMAP_USER_END)
        return EINVAL;
    pte = pmap_lookup_pte(pmap, vaddr);
    if (pte == 0 || (*pte & PMAP_PTE_PRESENT) == 0)
        return ENOENT;
    if ((*pte & PMAP_PTE_MODIFIED) == 0)
        return 0;
    if ((*pte & PMAP_PTE_DEVICE) != 0) {
        *pte &= ~PMAP_PTE_MODIFIED;
        pmap_invalidate(pmap, vaddr);
        return 0;
    }
    page = vm_page_lookup(pmap_allocator, *pte & PMAP_PTE_PADDR);
    if (page == 0)
        return EFAULT;
    error = vm_page_counter_dec(pmap_allocator, page,
        VM_PAGE_COUNTER_DIRTY);
    if (error != 0)
        return error;
    *pte &= ~PMAP_PTE_MODIFIED;
    pmap_invalidate(pmap, vaddr);
    return 0;
}

int
pmap_remove_page(struct vm_page *page)
{
    struct pmap_pv_entry *entry;
    struct pmap *pmap;
    uint32_t *pte;
    vm_vaddr_t vaddr;
    unsigned bucket;
    int error;

    if (!pmap_initialized || page == 0 ||
        vm_page_lookup(pmap_allocator, page->vmp_paddr) != page)
        return EINVAL;
    bucket = pmap_pv_bucket(page->vmp_paddr);
    while (page->vmp_hold_count != 0) {
        for (entry = pmap_pv_hash[bucket]; entry != 0;
            entry = entry->pve_hash_next) {
            if (entry->pve_paddr == page->vmp_paddr)
                break;
        }
        if (entry == 0)
            return EFAULT;
        pmap = entry->pve_pmap;
        vaddr = entry->pve_vaddr;
        if (!pmap_valid(pmap))
            return EFAULT;
        pte = pmap_lookup_pte(pmap, vaddr);
        if (pte == 0 ||
            (*pte & (PMAP_PTE_PRESENT | PMAP_PTE_PADDR |
            PMAP_PTE_DEVICE)) !=
            (PMAP_PTE_PRESENT | page->vmp_paddr))
            return EFAULT;
        error = pmap_remove_pte(pmap, vaddr, pte);
        if (error != 0)
            return error;
    }
    return pmap_pv_release_empty_pages(1);
}

int
pmap_clear_page_reference(struct vm_page *page)
{
    struct pmap_pv_entry *entry;
    uint32_t *pte;
    unsigned bucket;
    int error;

    if (!pmap_initialized || page == 0 ||
        vm_page_lookup(pmap_allocator, page->vmp_paddr) != page)
        return EINVAL;
    if (page->vmp_reference_count == 0)
        return 0;
    bucket = pmap_pv_bucket(page->vmp_paddr);
    for (entry = pmap_pv_hash[bucket]; entry != 0;
        entry = entry->pve_hash_next) {
        if (entry->pve_paddr != page->vmp_paddr)
            continue;
        if (!pmap_valid(entry->pve_pmap))
            return EFAULT;
        pte = pmap_lookup_pte(entry->pve_pmap, entry->pve_vaddr);
        if (pte == 0 ||
            (*pte & (PMAP_PTE_PRESENT | PMAP_PTE_PADDR |
            PMAP_PTE_DEVICE)) !=
            (PMAP_PTE_PRESENT | page->vmp_paddr))
            return EFAULT;
        if ((*pte & PMAP_PTE_REFERENCED) == 0)
            continue;
        error = vm_page_counter_dec(pmap_allocator, page,
            VM_PAGE_COUNTER_REFERENCE);
        if (error != 0)
            return error;
        *pte &= ~PMAP_PTE_REFERENCED;
        pmap_invalidate(entry->pve_pmap, entry->pve_vaddr);
        if (page->vmp_reference_count == 0)
            return 0;
    }
    return page->vmp_reference_count == 0 ? 0 : EFAULT;
}

int
pmap_clear_page_modify(struct vm_page *page)
{
    struct pmap_pv_entry *entry;
    uint32_t *pte;
    unsigned bucket;
    int error;

    if (!pmap_initialized || page == 0 ||
        vm_page_lookup(pmap_allocator, page->vmp_paddr) != page)
        return EINVAL;
    if (page->vmp_dirty_count == 0)
        return 0;
    bucket = pmap_pv_bucket(page->vmp_paddr);
    for (entry = pmap_pv_hash[bucket]; entry != 0;
        entry = entry->pve_hash_next) {
        if (entry->pve_paddr != page->vmp_paddr)
            continue;
        if (!pmap_valid(entry->pve_pmap))
            return EFAULT;
        pte = pmap_lookup_pte(entry->pve_pmap, entry->pve_vaddr);
        if (pte == 0 ||
            (*pte & (PMAP_PTE_PRESENT | PMAP_PTE_PADDR |
            PMAP_PTE_DEVICE)) !=
            (PMAP_PTE_PRESENT | page->vmp_paddr))
            return EFAULT;
        if ((*pte & PMAP_PTE_MODIFIED) == 0)
            continue;
        error = vm_page_counter_dec(pmap_allocator, page,
            VM_PAGE_COUNTER_DIRTY);
        if (error != 0)
            return error;
        *pte &= ~PMAP_PTE_MODIFIED;
        pmap_invalidate(entry->pve_pmap, entry->pve_vaddr);
        if (page->vmp_dirty_count == 0)
            return 0;
    }
    return page->vmp_dirty_count == 0 ? 0 : EFAULT;
}

void *
pmap_pages_direct_map(struct vm_page *page, vm_pfn_t page_count,
    enum pmap_cache cache)
{
    struct vm_page *current;
    vm_paddr_t paddr;
    vm_paddr_t expected;
    vm_size_t size;
    vm_pfn_t index;

    if (!pmap_initialized || page == 0 || page_count == 0 ||
        page_count > VM_SIZE_MAX / VM_PAGE_SIZE ||
        (cache != PMAP_CACHE_CACHED && cache != PMAP_CACHE_UNCACHED))
        return 0;
    paddr = page->vmp_paddr;
    size = page_count * VM_PAGE_SIZE;
    if (size - 1 > VM_PADDR_MAX - paddr)
        return 0;
    for (index = 0; index < page_count; ++index) {
        expected = paddr + index * VM_PAGE_SIZE;
        current = vm_page_lookup(pmap_allocator, expected);
        if (current == 0 || (index == 0 && current != page) ||
            current->vmp_state == VM_PAGE_FREE ||
            current->vmp_state == VM_PAGE_RESERVED ||
            current->vmp_state == VM_PAGE_BAD)
            return 0;
    }
    return pmap_md_direct_map(paddr, size, cache);
}

void *
pmap_page_direct_map(struct vm_page *page, enum pmap_cache cache)
{
    return pmap_pages_direct_map(page, 1, cache);
}

void *
pmap_device_direct_map(vm_paddr_t paddr, enum pmap_cache cache)
{
    if (!pmap_initialized || !vm_paddr_page_aligned(paddr) ||
        (cache != PMAP_CACHE_CACHED && cache != PMAP_CACHE_UNCACHED))
        return 0;
    return pmap_md_direct_map(paddr, VM_PAGE_SIZE, cache);
}

vm_paddr_t
pmap_cache_alias_mask(void)
{
    return pmap_md_cache_alias_mask();
}

int
pmap_page_sync(struct vm_page *page, unsigned operations)
{
    if (!pmap_initialized || page == 0 || operations == 0 ||
        (operations & ~(PMAP_SYNC_DATA | PMAP_SYNC_INSTRUCTION |
        PMAP_INVALIDATE_DATA)) != 0 ||
        (operations & (PMAP_SYNC_DATA | PMAP_INVALIDATE_DATA)) ==
        (PMAP_SYNC_DATA | PMAP_INVALIDATE_DATA) ||
        vm_page_lookup(pmap_allocator, page->vmp_paddr) != page)
        return EINVAL;
    return pmap_md_page_sync(page->vmp_paddr, operations);
}

int
pmap_sync_phys_range(vm_paddr_t paddr, vm_size_t size,
    unsigned operations)
{
    if (!pmap_initialized || size == 0 || operations == 0 ||
        (operations & ~(PMAP_SYNC_DATA | PMAP_SYNC_INSTRUCTION |
        PMAP_INVALIDATE_DATA)) != 0 ||
        (operations & (PMAP_SYNC_DATA | PMAP_INVALIDATE_DATA)) ==
        (PMAP_SYNC_DATA | PMAP_INVALIDATE_DATA) ||
        paddr > VM_PADDR_MAX - (size - 1))
        return EINVAL;
    return pmap_md_range_sync(paddr, size, operations);
}

int
pmap_get_stats(struct pmap_stats *stats)
{
    if (!pmap_initialized || stats == 0)
        return EINVAL;
    *stats = pmap_statistics;
    stats->pms_tlb_refills = pmap_total_tlb_refills();
    return 0;
}

int
pmap_get_tlb_diagnostics(vm_vaddr_t vaddr,
    struct pmap_tlb_diagnostics *diagnostics)
{
    uint32_t *pte;
    uint32_t *even_pte;
    uint32_t *odd_pte;
    vm_vaddr_t pair;

    if (!pmap_initialized || diagnostics == 0 ||
        vaddr >= PMAP_USER_END)
        return EINVAL;
    *diagnostics = pmap_tlb_diagnostics;
    diagnostics->ptd_refills = pmap_total_tlb_refills();
    diagnostics->ptd_active_pmap = (unsigned)(uintptr_t)pmap_active;
    diagnostics->ptd_active_asid =
        pmap_active != 0 ? pmap_active->pm_asid : 0;
    diagnostics->ptd_active_generation =
        pmap_active != 0 ? pmap_active->pm_generation : 0;
    diagnostics->ptd_asid_generation = pmap_asid_generation;
    diagnostics->ptd_next_asid = pmap_next_asid;
    diagnostics->ptd_query_pte = 0;
    diagnostics->ptd_query_entryhi = 0;
    diagnostics->ptd_query_entrylo0 = 0;
    diagnostics->ptd_query_entrylo1 = 0;
    diagnostics->ptd_hardware_found = 0;
    diagnostics->ptd_hardware_index = 0;
    diagnostics->ptd_hardware_pagemask = 0;
    diagnostics->ptd_hardware_entryhi = 0;
    diagnostics->ptd_hardware_entrylo0 = 0;
    diagnostics->ptd_hardware_entrylo1 = 0;
    diagnostics->ptd_fast_sequence =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_SEQUENCE);
    diagnostics->ptd_fast_last_epc =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_EPC);
    diagnostics->ptd_fast_last_vaddr =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_BADVADDR);
    diagnostics->ptd_fast_last_at =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_AT);
    diagnostics->ptd_fast_last_v0 =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_V0);
    diagnostics->ptd_fast_last_a0 =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_A0);
    diagnostics->ptd_fast_last_cause =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_CAUSE);
    diagnostics->ptd_fast_last_status =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_STATUS);
    diagnostics->ptd_fast_last_entryhi =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_ENTRYHI);
    diagnostics->ptd_fast_last_entrylo0 =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_ENTRYLO0);
    diagnostics->ptd_fast_last_entrylo1 =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_ENTRYLO1);
    diagnostics->ptd_fast_last_index =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_INDEX);
    diagnostics->ptd_fast_last_random =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_RANDOM);
    diagnostics->ptd_fast_last_count =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_COUNT);
    diagnostics->ptd_fast_last_pte_pair =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_PTE_PAIR);
    diagnostics->ptd_fast_last_directory =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_DIRECTORY);
    diagnostics->ptd_fast_last_pte0 =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_PTE0);
    diagnostics->ptd_fast_last_pte1 =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_PTE1);
    diagnostics->ptd_fast_last_at_high =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_AT_HIGH);
    diagnostics->ptd_fast_last_v0_high =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_V0_HIGH);
    diagnostics->ptd_fast_last_a0_high =
        pmap_fast_diagnostic(MIPS_REFILL_DIAG_A0_HIGH);
    diagnostics->ptd_fast_repeat = mips_pmap_fast_repeat;
    diagnostics->ptd_active_directory = pmap_active == 0 ? 0 :
        (unsigned)(uintptr_t)pmap_active->pm_directory;
    diagnostics->ptd_fast_directory =
        (unsigned)(uintptr_t)mips_pmap_fast_directory;
    if (!pmap_valid(pmap_active))
        return 0;

    pair = vaddr & PMAP_TLB_PAIR_MASK;
    pte = pmap_lookup_pte(pmap_active, vaddr);
    even_pte = pmap_lookup_pte(pmap_active, pair);
    odd_pte = pmap_lookup_pte(pmap_active, pair + VM_PAGE_SIZE);
    diagnostics->ptd_query_pte = pte == 0 ? 0 : *pte;
    diagnostics->ptd_query_entryhi = pair | pmap_active->pm_asid;
    diagnostics->ptd_query_entrylo0 =
        even_pte == 0 ? 0 : pmap_tlb_entrylo(*even_pte);
    diagnostics->ptd_query_entrylo1 =
        odd_pte == 0 ? 0 : pmap_tlb_entrylo(*odd_pte);
#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
    if (pmap_md_tlb_lookup(diagnostics->ptd_query_entryhi,
        &diagnostics->ptd_hardware_index,
        &diagnostics->ptd_hardware_pagemask,
        &diagnostics->ptd_hardware_entryhi,
        &diagnostics->ptd_hardware_entrylo0,
        &diagnostics->ptd_hardware_entrylo1) == 0)
        diagnostics->ptd_hardware_found = 1;
#endif
    return 0;
}

int
pmap_validate(struct pmap *pmap)
{
    const uint32_t *table;
    const struct vm_page *page;
    const struct pmap_pv_entry *entry;
    const uint32_t *pte_address;
    uint32_t pte;
    vm_vaddr_t vaddr;
    vm_paddr_t alias_mask;
    unsigned bucket;
    unsigned directory_index;
    unsigned table_index;

    if (!pmap_valid(pmap) || pmap->pm_directory == 0 ||
        !vm_paddr_page_aligned(pmap->pm_directory_paddr))
        return EINVAL;
    alias_mask = pmap_md_cache_alias_mask();
    for (directory_index = 0;
        directory_index < PMAP_DIRECTORY_ENTRIES; ++directory_index) {
        if ((pmap->pm_directory[directory_index] &
            PMAP_PTE_PRESENT) == 0)
            continue;
        if ((pmap->pm_directory[directory_index] &
            (PMAP_PTE_FLAGS & ~PMAP_PTE_PRESENT)) != 0)
            return EINVAL;
        table = pmap_table(pmap, directory_index);
        if (table == 0)
            return EFAULT;
        for (table_index = 0; table_index < PMAP_TABLE_ENTRIES;
            ++table_index) {
            pte = table[table_index];
            if ((pte & PMAP_PTE_PRESENT) == 0)
                continue;
            if ((pte & (PMAP_PTE_READ | PMAP_PTE_WRITE |
                PMAP_PTE_EXECUTE)) == 0)
                return EINVAL;
            vaddr = (directory_index << PMAP_DIRECTORY_SHIFT) |
                (table_index << PMAP_TABLE_SHIFT);
            if ((pte & PMAP_PTE_DEVICE) == 0) {
                page = vm_page_lookup(pmap_allocator,
                    pte & PMAP_PTE_PADDR);
                if (page == 0 || page->vmp_state == VM_PAGE_FREE ||
                    page->vmp_state == VM_PAGE_RESERVED ||
                    page->vmp_state == VM_PAGE_BAD ||
                    page->vmp_hold_count == 0)
                    return EFAULT;
                if (pmap_pv_find(pmap, vaddr,
                    pte & PMAP_PTE_PADDR) == 0)
                    return EFAULT;
                if ((pte & PMAP_PTE_UNCACHED) == 0 &&
                    ((vaddr ^ page->vmp_paddr) & alias_mask) != 0)
                    return EFAULT;
            } else {
                if (pmap_pv_find(pmap, vaddr,
                    pte & PMAP_PTE_PADDR) != 0)
                    return EFAULT;
                page = vm_page_lookup(pmap_allocator,
                    pte & PMAP_PTE_PADDR);
                if ((pte & PMAP_PTE_EXECUTE) != 0 ||
                    (page != 0 &&
                    page->vmp_state != VM_PAGE_RESERVED &&
                    !(page->vmp_state == VM_PAGE_WIRED &&
                    (page->vmp_flags & VM_PAGE_FLAG_DEVICE) != 0)))
                    return EFAULT;
            }
        }
    }
    for (bucket = 0; bucket < PMAP_PV_BUCKETS; ++bucket) {
        for (entry = pmap_pv_hash[bucket]; entry != 0;
            entry = entry->pve_hash_next) {
            if (entry->pve_pmap != pmap)
                continue;
            if (entry->pve_page == 0 ||
                entry->pve_page->pvp_free >=
                entry->pve_page->pvp_entries ||
                pmap_pv_bucket(entry->pve_paddr) != bucket ||
                !vm_vaddr_page_aligned(entry->pve_vaddr) ||
                !vm_paddr_page_aligned(entry->pve_paddr))
                return EFAULT;
            pte_address = pmap_lookup_pte(pmap, entry->pve_vaddr);
            if (pte_address == 0 ||
                (*pte_address & (PMAP_PTE_PRESENT | PMAP_PTE_PADDR |
                PMAP_PTE_DEVICE)) !=
                (PMAP_PTE_PRESENT | entry->pve_paddr))
                return EFAULT;
        }
    }
    return 0;
}

static void
pmap_force_asid_rollover(void)
{
    pmap_next_asid = PMAP_ASID_LAST + 1;
}

#ifdef REBSD_VM_HOST_TEST
void
pmap_debug_force_asid_rollover(void)
{
    pmap_force_asid_rollover();
}
#endif

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
#define PMAP_SELFTEST_DATA_VA   0x10000000u
#define PMAP_SELFTEST_CODE_VA   0x10004000u

static int
pmap_selftest_alloc(vm_vaddr_t vaddr, struct vm_page **result)
{
    struct vm_page_request request;

    vm_page_request_init(&request);
    request.vpr_color_mask = pmap_cache_alias_mask();
    request.vpr_color = vaddr & request.vpr_color_mask;
    request.vpr_state = VM_PAGE_ACTIVE;
    return vm_page_alloc(pmap_allocator, &request, result);
}

static int
pmap_selftest_free(struct vm_page *page)
{
    if (page == 0)
        return 0;
    return vm_page_free(pmap_allocator, page, 1);
}

/* Keep PCC from folding a kuseg function pointer into a kernel-region jal. */
static int
pmap_selftest_execute(vm_vaddr_t address)
{
    int (*volatile function)(void);

    function = (int (*)(void))(uintptr_t)address;
    return (*function)();
}

int
pmap_bootstrap_selftest(void)
{
    struct pmap_stats before_stats;
    struct pmap *first;
    struct pmap *second;
    struct vm_page *first_page;
    struct vm_page *second_page;
    struct vm_page *code_page;
    volatile uint32_t *first_backing;
    volatile uint32_t *second_backing;
    volatile uint32_t *code_backing;
    volatile uint32_t *test_address;
    vm_paddr_t paddr;
    vm_pfn_t free_before;
    int error;

    first = 0;
    second = 0;
    first_page = 0;
    second_page = 0;
    code_page = 0;
    free_before = pmap_allocator->vpa_free_count;
    error = pmap_get_stats(&before_stats);
    if (error != 0)
        return error;
    error = pmap_create(&first);
    if (error != 0)
        goto out;
    error = pmap_create(&second);
    if (error != 0)
        goto out;
    error = pmap_selftest_alloc(PMAP_SELFTEST_DATA_VA, &first_page);
    if (error != 0)
        goto out;
    error = pmap_selftest_alloc(PMAP_SELFTEST_DATA_VA, &second_page);
    if (error != 0)
        goto out;
    error = pmap_selftest_alloc(PMAP_SELFTEST_CODE_VA, &code_page);
    if (error != 0)
        goto out;
    first_backing = pmap_page_direct_map(first_page,
        PMAP_CACHE_CACHED);
    second_backing = pmap_page_direct_map(second_page,
        PMAP_CACHE_CACHED);
    code_backing = pmap_page_direct_map(code_page, PMAP_CACHE_CACHED);
    if (first_backing == 0 || second_backing == 0 || code_backing == 0) {
        error = EFAULT;
        goto out;
    }
    first_backing[0] = 0x11223344u;
    second_backing[0] = 0x55667788u;
    error = pmap_enter(first, PMAP_SELFTEST_DATA_VA, first_page,
        VM_PROT_READ | VM_PROT_WRITE, PMAP_CACHE_CACHED);
    if (error != 0)
        goto out;
    error = pmap_enter(second, PMAP_SELFTEST_DATA_VA, second_page,
        VM_PROT_READ, PMAP_CACHE_CACHED);
    if (error != 0)
        goto out;
    error = pmap_activate(first);
    if (error != 0)
        goto out;
    error = pmap_fault(first, PMAP_SELFTEST_DATA_VA, VM_PROT_READ, 0);
    if (error != 0)
        goto out;
    test_address = (volatile uint32_t *)PMAP_SELFTEST_DATA_VA;
    if (*test_address != 0x11223344u) {
        error = EFAULT;
        goto out;
    }
    error = pmap_fault(first, PMAP_SELFTEST_DATA_VA, VM_PROT_WRITE, 0);
    if (error != 0)
        goto out;
    *test_address = 0xa5a55a5au;
    if (first_backing[0] != 0xa5a55a5au ||
        !pmap_is_referenced(first, PMAP_SELFTEST_DATA_VA) ||
        !pmap_is_modified(first, PMAP_SELFTEST_DATA_VA)) {
        error = EFAULT;
        goto out;
    }
    error = pmap_activate(second);
    if (error != 0)
        goto out;
    error = pmap_fault(second, PMAP_SELFTEST_DATA_VA, VM_PROT_READ, 0);
    if (error != 0 || *test_address != 0x55667788u) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = pmap_activate(first);
    if (error != 0)
        goto out;
    if (*test_address != 0xa5a55a5au) {
        error = EFAULT;
        goto out;
    }
    error = pmap_protect(first, PMAP_SELFTEST_DATA_VA,
        PMAP_SELFTEST_DATA_VA + VM_PAGE_SIZE, VM_PROT_READ);
    if (error != 0)
        goto out;
    error = pmap_fault(first, PMAP_SELFTEST_DATA_VA, VM_PROT_WRITE, 0);
    if (error != EACCES) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = pmap_fault(first, PMAP_SELFTEST_DATA_VA, VM_PROT_READ, 0);
    if (error != 0 || *test_address != 0xa5a55a5au) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }

    /* Verify that replacing a resident translation cannot reuse stale TLB. */
    error = pmap_remove(first, PMAP_SELFTEST_DATA_VA,
        PMAP_SELFTEST_DATA_VA + VM_PAGE_SIZE);
    if (error != 0)
        goto out;
    error = pmap_enter(first, PMAP_SELFTEST_DATA_VA, second_page,
        VM_PROT_READ, PMAP_CACHE_CACHED);
    if (error != 0)
        goto out;
    error = pmap_fault(first, PMAP_SELFTEST_DATA_VA, VM_PROT_READ, 0);
    if (error != 0 || *test_address != 0x55667788u) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }

    /* jr ra; addiu v0, zero, 42 (the second word is the delay slot). */
    code_backing[0] = 0x03e00008u;
    code_backing[1] = 0x2402002au;
    error = pmap_enter(first, PMAP_SELFTEST_CODE_VA, code_page,
        VM_PROT_READ | VM_PROT_EXECUTE, PMAP_CACHE_CACHED);
    if (error != 0)
        goto out;
    error = pmap_fault(first, PMAP_SELFTEST_CODE_VA,
        VM_PROT_EXECUTE, 0);
    if (error != 0)
        goto out;
    if (pmap_selftest_execute(PMAP_SELFTEST_CODE_VA) != 42) {
        error = EFAULT;
        goto out;
    }

    pmap_force_asid_rollover();
    second->pm_generation = 0;
    error = pmap_activate(second);
    if (error != 0)
        goto out;
    error = pmap_fault(second, PMAP_SELFTEST_DATA_VA, VM_PROT_READ, 0);
    if (error != 0 || *test_address != 0x55667788u) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = pmap_remove(second, PMAP_SELFTEST_DATA_VA,
        PMAP_SELFTEST_DATA_VA + VM_PAGE_SIZE);
    if (error != 0)
        goto out;
    error = pmap_extract(second, PMAP_SELFTEST_DATA_VA, &paddr);
    if (error != ENOENT) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = pmap_validate(first);
    if (error != 0)
        goto out;
    error = 0;

out:
    if (first != 0) {
        if (pmap_destroy(first) != 0 && error == 0)
            error = EFAULT;
    }
    if (second != 0) {
        if (pmap_destroy(second) != 0 && error == 0)
            error = EFAULT;
    }
    if (pmap_selftest_free(first_page) != 0 && error == 0)
        error = EFAULT;
    if (pmap_selftest_free(second_page) != 0 && error == 0)
        error = EFAULT;
    if (pmap_selftest_free(code_page) != 0 && error == 0)
        error = EFAULT;
    if (pmap_allocator->vpa_free_count != free_before && error == 0)
        error = EFAULT;
    if (pmap_statistics.pms_asid_rollovers ==
        before_stats.pms_asid_rollovers && error == 0)
        error = EFAULT;
    return error;
}

int
pmap_bootstrap_stats(struct pmap_stats *stats)
{
    return pmap_get_stats(stats);
}
#endif
