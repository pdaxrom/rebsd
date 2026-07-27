/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/*
 * i386 non-PAE pmap.
 *
 * Every process page directory inherits the bootstrap identity mappings
 * needed by the still low-linked kernel and the permanent 0xc0000000 direct
 * map.  A user PDE becomes private on first use; inherited PTEs in that
 * private copy are tagged so they are never charged as user mappings.
 */

#include <sys/errno.h>
#include <machine/machparam.h>
#include <vm/pmap.h>

#include "memory.h"
#include "paging.h"

#define PMAP_DIRECTORY_ENTRIES       1024u
#define PMAP_TABLE_ENTRIES           1024u
#define PMAP_DIRECTORY_SHIFT         22u
#define PMAP_INDEX_MASK              0x3ffu
#define PMAP_USER_END                I386_USER_VADDR_END
#define PMAP_MAX_MAPS                (NPROC + 4)

#define PMAP_PTE_PRESENT             0x001u
#define PMAP_PTE_WRITABLE            0x002u
#define PMAP_PTE_USER                0x004u
#define PMAP_PTE_CACHE_DISABLE       0x010u
#define PMAP_PTE_ACCESSED            0x020u
#define PMAP_PTE_DIRTY               0x040u
#define PMAP_PTE_BOOTSTRAP           0x100u
#define PMAP_PTE_REFERENCE_ACCOUNTED 0x200u
#define PMAP_PTE_DIRTY_ACCOUNTED     0x400u
#define PMAP_PTE_DEVICE              0x800u
#define PMAP_PTE_FRAME               0xfffff000u
#define PMAP_PTE_FLAGS               0x00000fffu

#define PMAP_OWNED_WORDS             (PMAP_DIRECTORY_ENTRIES / 32u)
#define PMAP_SELFTEST_DATA_VA        0x40000000u
#define PMAP_SELFTEST_CODE_VA        0x40004000u
#define PMAP_SELFTEST_DEVICE_VA      0x40008000u
#define PMAP_SELFTEST_DEVICE_PADDR   0xf0000000u

extern char __kernel_start[];
extern char __kernel_end[];

struct pmap {
    uint32_t    *pm_directory;
    vm_paddr_t   pm_directory_paddr;
    uint32_t     pm_owned[PMAP_OWNED_WORDS];
    unsigned     pm_in_use;
};

static struct pmap pmap_maps[PMAP_MAX_MAPS];
static struct vm_page_allocator *pmap_allocator;
static struct pmap *pmap_active;
static struct pmap_stats pmap_statistics;
static struct pmap_tlb_diagnostics pmap_diagnostics;
static vm_paddr_t pmap_bootstrap_directory;
static vm_vaddr_t pmap_device_vaddr_next;
static unsigned pmap_initialized;

static void
pmap_zero(void *pointer, unsigned size)
{
    unsigned char *bytes;

    bytes = (unsigned char *)pointer;
    while (size-- != 0)
        *bytes++ = 0;
}

static void
pmap_copy_words(uint32_t *destination, const uint32_t *source,
    unsigned count)
{
    while (count-- != 0)
        *destination++ = *source++;
}

static void
pmap_stat_increment(vm_pfn_t *value)
{
    if (*value != VM_PFN_MAX)
        ++*value;
}

static int
pmap_valid(const struct pmap *pmap)
{
    return pmap != (const struct pmap *)0 &&
        pmap >= &pmap_maps[0] && pmap < &pmap_maps[PMAP_MAX_MAPS] &&
        pmap->pm_in_use != 0;
}

static int
pmap_protection_valid(vm_prot_t protection, int allow_none)
{
    if ((protection & ~VM_PROT_ALL) != 0)
        return 0;
    return allow_none || protection != VM_PROT_NONE;
}

static unsigned
pmap_directory_index(vm_vaddr_t vaddr)
{
    return (vaddr >> PMAP_DIRECTORY_SHIFT) & PMAP_INDEX_MASK;
}

static unsigned
pmap_table_index(vm_vaddr_t vaddr)
{
    return (vaddr >> VM_PAGE_SHIFT) & PMAP_INDEX_MASK;
}

static int
pmap_directory_owned(const struct pmap *pmap, unsigned index)
{
    return (pmap->pm_owned[index >> 5] & (1u << (index & 31u))) != 0;
}

static void
pmap_directory_set_owned(struct pmap *pmap, unsigned index)
{
    pmap->pm_owned[index >> 5] |= 1u << (index & 31u);
}

static void
pmap_directory_clear_owned(struct pmap *pmap, unsigned index)
{
    pmap->pm_owned[index >> 5] &= ~(1u << (index & 31u));
}

static void *
pmap_direct(vm_paddr_t paddr)
{
    if (paddr >= I386_DIRECT_MAP_SIZE)
        return (void *)0;
    return (void *)(I386_KERNEL_BASE + paddr);
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
    request.vpr_max_address = I386_DIRECT_MAP_SIZE - 1u;
    error = vm_page_alloc(pmap_allocator, &request, &page);
    if (error != 0)
        return error;
    mapping = pmap_direct(page->vmp_paddr);
    if (mapping == (void *)0) {
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
    if (page == (struct vm_page *)0)
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
pmap_table_raw(const struct pmap *pmap, unsigned index)
{
    vm_paddr_t paddr;

    paddr = pmap->pm_directory[index];
    if ((paddr & PMAP_PTE_PRESENT) == 0)
        return (uint32_t *)0;
    return (uint32_t *)pmap_direct(paddr & PMAP_PTE_FRAME);
}

static int
pmap_get_table(struct pmap *pmap, unsigned index, int create,
    uint32_t **result)
{
    uint32_t *old_table;
    uint32_t *table;
    vm_paddr_t table_paddr;
    unsigned i;
    int error;

    table = pmap_table_raw(pmap, index);
    if (table == (uint32_t *)0) {
        if (!create)
            return ENOENT;
        error = pmap_alloc_table_page(&table_paddr, &table);
        if (error != 0)
            return error;
        pmap->pm_directory[index] = table_paddr | PMAP_PTE_PRESENT |
            PMAP_PTE_WRITABLE | PMAP_PTE_USER;
        pmap_directory_set_owned(pmap, index);
    } else if (create && !pmap_directory_owned(pmap, index)) {
        old_table = table;
        error = pmap_alloc_table_page(&table_paddr, &table);
        if (error != 0)
            return error;
        pmap_copy_words(table, old_table, PMAP_TABLE_ENTRIES);
        for (i = 0; i < PMAP_TABLE_ENTRIES; ++i) {
            if ((table[i] & PMAP_PTE_PRESENT) != 0)
                table[i] |= PMAP_PTE_BOOTSTRAP;
        }
        pmap->pm_directory[index] = table_paddr | PMAP_PTE_PRESENT |
            PMAP_PTE_WRITABLE | PMAP_PTE_USER;
        pmap_directory_set_owned(pmap, index);
    }
    *result = table;
    return 0;
}

static uint32_t *
pmap_lookup_pte(struct pmap *pmap, vm_vaddr_t vaddr)
{
    uint32_t *table;
    unsigned index;

    index = pmap_directory_index(vaddr);
    if (!pmap_directory_owned(pmap, index) ||
        pmap_get_table(pmap, index, 0, &table) != 0)
        return (uint32_t *)0;
    return &table[pmap_table_index(vaddr)];
}

static int
pmap_get_pte(struct pmap *pmap, vm_vaddr_t vaddr, int create,
    uint32_t **result)
{
    uint32_t *table;
    int error;

    error = pmap_get_table(pmap, pmap_directory_index(vaddr), create,
        &table);
    if (error != 0)
        return error;
    *result = &table[pmap_table_index(vaddr)];
    return 0;
}

static int
pmap_kernel_collision(vm_vaddr_t vaddr)
{
    vm_vaddr_t start;
    vm_vaddr_t end;

    start = vm_vaddr_trunc_page((vm_vaddr_t)(uintptr_t)__kernel_start);
    end = (vm_vaddr_t)(uintptr_t)__kernel_end;
    return vaddr >= start && vaddr < end;
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
    if (pmap_active != pmap)
        return;
    i386_paging_invalidate_page(vaddr);
    pmap_stat_increment(&pmap_statistics.pms_targeted_invalidations);
}

static int
pmap_drop_account(struct vm_page *page, uint32_t entry)
{
    int error;

    if ((entry & PMAP_PTE_DIRTY_ACCOUNTED) != 0) {
        error = vm_page_counter_dec(pmap_allocator, page,
            VM_PAGE_COUNTER_DIRTY);
        if (error != 0)
            return error;
    }
    if ((entry & PMAP_PTE_REFERENCE_ACCOUNTED) != 0) {
        error = vm_page_counter_dec(pmap_allocator, page,
            VM_PAGE_COUNTER_REFERENCE);
        if (error != 0) {
            if ((entry & PMAP_PTE_DIRTY_ACCOUNTED) != 0)
                (void)vm_page_counter_inc(pmap_allocator, page,
                    VM_PAGE_COUNTER_DIRTY);
            return error;
        }
    }
    error = vm_page_counter_dec(pmap_allocator, page,
        VM_PAGE_COUNTER_HOLD);
    if (error != 0) {
        if ((entry & PMAP_PTE_REFERENCE_ACCOUNTED) != 0)
            (void)vm_page_counter_inc(pmap_allocator, page,
                VM_PAGE_COUNTER_REFERENCE);
        if ((entry & PMAP_PTE_DIRTY_ACCOUNTED) != 0)
            (void)vm_page_counter_inc(pmap_allocator, page,
                VM_PAGE_COUNTER_DIRTY);
    }
    return error;
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
    if ((old & PMAP_PTE_BOOTSTRAP) != 0) {
        *pte = 0;
        pmap_invalidate(pmap, vaddr);
        return 0;
    }
    if ((old & PMAP_PTE_DEVICE) == 0) {
        page = vm_page_lookup(pmap_allocator, old & PMAP_PTE_FRAME);
        if (page == (struct vm_page *)0)
            return EFAULT;
        error = pmap_drop_account(page, old);
        if (error != 0)
            return error;
    }
    *pte = 0;
    if (pmap_statistics.pms_mappings != 0)
        --pmap_statistics.pms_mappings;
    if (pmap_statistics.pms_resident_pages != 0)
        --pmap_statistics.pms_resident_pages;
    pmap_invalidate(pmap, vaddr);
    return 0;
}

int
pmap_system_init(struct vm_page_allocator *allocator)
{
    uint32_t *bootstrap;

    if (allocator == (struct vm_page_allocator *)0 ||
        allocator->vpa_initialized == 0)
        return EINVAL;
    pmap_bootstrap_directory = i386_paging_directory();
    bootstrap = (uint32_t *)pmap_direct(pmap_bootstrap_directory);
    if (pmap_bootstrap_directory == 0 || bootstrap == (uint32_t *)0)
        return EFAULT;

    pmap_zero(pmap_maps, sizeof(pmap_maps));
    pmap_zero(&pmap_statistics, sizeof(pmap_statistics));
    pmap_zero(&pmap_diagnostics, sizeof(pmap_diagnostics));
    pmap_allocator = allocator;
    pmap_active = (struct pmap *)0;
    pmap_device_vaddr_next = I386_DEVICE_VADDR_START;
    pmap_initialized = 1;
    return 0;
}

int
pmap_create(struct pmap **result)
{
    struct pmap *pmap;
    uint32_t *bootstrap;
    uint32_t *directory;
    vm_paddr_t directory_paddr;
    unsigned i;
    int error;

    if (!pmap_initialized || result == (struct pmap **)0)
        return EINVAL;
    *result = (struct pmap *)0;
    pmap = (struct pmap *)0;
    for (i = 0; i < PMAP_MAX_MAPS; ++i) {
        if (pmap_maps[i].pm_in_use == 0) {
            pmap = &pmap_maps[i];
            break;
        }
    }
    if (pmap == (struct pmap *)0)
        return ENOSPC;
    error = pmap_alloc_table_page(&directory_paddr, &directory);
    if (error != 0)
        return error;
    bootstrap = (uint32_t *)pmap_direct(pmap_bootstrap_directory);
    if (bootstrap == (uint32_t *)0) {
        (void)pmap_free_table_page(directory_paddr);
        return EFAULT;
    }
    pmap_zero(pmap, sizeof(*pmap));
    pmap_copy_words(directory, bootstrap, PMAP_DIRECTORY_ENTRIES);
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
        if (!pmap_directory_owned(pmap, directory_index))
            continue;
        table = pmap_table_raw(pmap, directory_index);
        if (table == (uint32_t *)0)
            return EFAULT;
        for (table_index = 0; table_index < PMAP_TABLE_ENTRIES;
            ++table_index) {
            if ((table[table_index] & PMAP_PTE_PRESENT) == 0)
                continue;
            vaddr = (directory_index << PMAP_DIRECTORY_SHIFT) |
                (table_index << VM_PAGE_SHIFT);
            error = pmap_remove_pte(pmap, vaddr, &table[table_index]);
            if (error != 0)
                return error;
        }
        table_paddr = pmap->pm_directory[directory_index] &
            PMAP_PTE_FRAME;
        pmap->pm_directory[directory_index] = 0;
        pmap_directory_clear_owned(pmap, directory_index);
        error = pmap_free_table_page(table_paddr);
        if (error != 0)
            return error;
    }
    error = pmap_free_table_page(pmap->pm_directory_paddr);
    if (error != 0)
        return error;
    pmap_zero(pmap, sizeof(*pmap));
    return 0;
}

int
pmap_prepare(struct pmap *pmap, vm_vaddr_t vaddr)
{
    uint32_t *pte;

    if (!pmap_valid(pmap) || !vm_vaddr_page_aligned(vaddr) ||
        vaddr >= PMAP_USER_END)
        return EINVAL;
    if (pmap_kernel_collision(vaddr))
        return EBUSY;
    return pmap_get_pte(pmap, vaddr, 1, &pte);
}

int
pmap_enter(struct pmap *pmap, vm_vaddr_t vaddr, struct vm_page *page,
    vm_prot_t protection, enum pmap_cache cache)
{
    uint32_t *pte;
    uint32_t entry;
    int error;

    if (!pmap_valid(pmap) || page == (struct vm_page *)0 ||
        !vm_vaddr_page_aligned(vaddr) || vaddr >= PMAP_USER_END ||
        !pmap_protection_valid(protection, 0) ||
        (cache != PMAP_CACHE_CACHED && cache != PMAP_CACHE_UNCACHED))
        return EINVAL;
    if (pmap_kernel_collision(vaddr))
        return EBUSY;
    if (vm_page_lookup(pmap_allocator, page->vmp_paddr) != page)
        return EINVAL;
    if (page->vmp_state == VM_PAGE_FREE ||
        page->vmp_state == VM_PAGE_RESERVED ||
        page->vmp_state == VM_PAGE_BAD)
        return EPERM;
    error = pmap_get_pte(pmap, vaddr, 1, &pte);
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
    entry = page->vmp_paddr | PMAP_PTE_PRESENT | PMAP_PTE_USER;
    if ((protection & VM_PROT_WRITE) != 0)
        entry |= PMAP_PTE_WRITABLE;
    if (cache == PMAP_CACHE_UNCACHED)
        entry |= PMAP_PTE_CACHE_DISABLE;
    *pte = entry;
    pmap_stat_increment(&pmap_statistics.pms_mappings);
    pmap_stat_increment(&pmap_statistics.pms_resident_pages);
    pmap_invalidate(pmap, vaddr);
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
        !pmap_protection_valid(protection, 0) ||
        (protection & VM_PROT_EXECUTE) != 0 ||
        (cache != PMAP_CACHE_CACHED && cache != PMAP_CACHE_UNCACHED))
        return EINVAL;
    if (pmap_kernel_collision(vaddr))
        return EBUSY;
    page = vm_page_lookup(pmap_allocator, paddr);
    if (page != (struct vm_page *)0 &&
        page->vmp_state != VM_PAGE_RESERVED)
        return EBUSY;
    error = pmap_get_pte(pmap, vaddr, 1, &pte);
    if (error != 0)
        return error;
    if ((*pte & PMAP_PTE_PRESENT) != 0) {
        error = pmap_remove_pte(pmap, vaddr, pte);
        if (error != 0)
            return error;
    }
    entry = paddr | PMAP_PTE_PRESENT | PMAP_PTE_USER | PMAP_PTE_DEVICE;
    if ((protection & VM_PROT_WRITE) != 0)
        entry |= PMAP_PTE_WRITABLE;
    if (cache == PMAP_CACHE_UNCACHED)
        entry |= PMAP_PTE_CACHE_DISABLE;
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
        directory_end = (vaddr & 0xffc00000u) + 0x00400000u;
        if (directory_end > end)
            directory_end = end;
        if (!pmap_directory_owned(pmap, directory_index) ||
            pmap_get_table(pmap, directory_index, 0, &table) != 0) {
            vaddr = directory_end;
            continue;
        }
        while (vaddr < directory_end) {
            error = pmap_remove_pte(pmap, vaddr,
                &table[pmap_table_index(vaddr)]);
            if (error != 0)
                return error;
            vaddr += VM_PAGE_SIZE;
        }
        if (pmap_table_empty(table)) {
            table_paddr = pmap->pm_directory[directory_index] &
                PMAP_PTE_FRAME;
            pmap->pm_directory[directory_index] = 0;
            pmap_directory_clear_owned(pmap, directory_index);
            error = pmap_free_table_page(table_paddr);
            if (error != 0)
                return error;
        }
    }
    return 0;
}

int
pmap_protect(struct pmap *pmap, vm_vaddr_t start, vm_vaddr_t end,
    vm_prot_t protection)
{
    uint32_t *pte;
    uint32_t preserve;
    vm_vaddr_t vaddr;

    if (!pmap_valid(pmap) || !vm_vaddr_page_aligned(start) ||
        !vm_vaddr_page_aligned(end) || start >= end ||
        end > PMAP_USER_END || !pmap_protection_valid(protection, 1))
        return EINVAL;
    if (protection == VM_PROT_NONE)
        return pmap_remove(pmap, start, end);
    for (vaddr = start; vaddr < end; vaddr += VM_PAGE_SIZE) {
        pte = pmap_lookup_pte(pmap, vaddr);
        if (pte == (uint32_t *)0 ||
            (*pte & (PMAP_PTE_PRESENT | PMAP_PTE_BOOTSTRAP)) !=
            PMAP_PTE_PRESENT)
            continue;
        if ((*pte & PMAP_PTE_DEVICE) != 0 &&
            (protection & VM_PROT_EXECUTE) != 0)
            return EACCES;
    }
    for (vaddr = start; vaddr < end; vaddr += VM_PAGE_SIZE) {
        pte = pmap_lookup_pte(pmap, vaddr);
        if (pte == (uint32_t *)0 ||
            (*pte & (PMAP_PTE_PRESENT | PMAP_PTE_BOOTSTRAP)) !=
            PMAP_PTE_PRESENT)
            continue;
        preserve = *pte & ~PMAP_PTE_WRITABLE;
        if ((protection & VM_PROT_WRITE) != 0)
            preserve |= PMAP_PTE_WRITABLE;
        *pte = preserve;
        pmap_invalidate(pmap, vaddr);
    }
    return 0;
}

int
pmap_extract(struct pmap *pmap, vm_vaddr_t vaddr, vm_paddr_t *result)
{
    uint32_t *pte;

    if (!pmap_valid(pmap) || result == (vm_paddr_t *)0 ||
        vaddr >= PMAP_USER_END)
        return EINVAL;
    pte = pmap_lookup_pte(pmap, vaddr);
    if (pte == (uint32_t *)0 ||
        (*pte & (PMAP_PTE_PRESENT | PMAP_PTE_BOOTSTRAP)) !=
        PMAP_PTE_PRESENT)
        return ENOENT;
    *result = (*pte & PMAP_PTE_FRAME) | (vaddr & VM_PAGE_MASK);
    return 0;
}

int
pmap_activate(struct pmap *pmap)
{
    if (!pmap_valid(pmap))
        return EINVAL;
    if (pmap_active == pmap)
        return 0;
    if (!i386_paging_activate_directory(pmap->pm_directory_paddr))
        return EFAULT;
    pmap_active = pmap;
    pmap_stat_increment(&pmap_statistics.pms_full_flushes);
    return 0;
}

void
pmap_deactivate(struct pmap *pmap)
{
    if (pmap_active != pmap)
        return;
    if (i386_paging_activate_directory(pmap_bootstrap_directory)) {
        pmap_active = (struct pmap *)0;
        pmap_stat_increment(&pmap_statistics.pms_full_flushes);
    }
}

static int
pmap_account_access(uint32_t *pte, struct vm_page *page,
    vm_prot_t access)
{
    int added_reference;
    int error;

    added_reference = 0;
    if (page != (struct vm_page *)0 &&
        (*pte & PMAP_PTE_REFERENCE_ACCOUNTED) == 0) {
        error = vm_page_counter_inc(pmap_allocator, page,
            VM_PAGE_COUNTER_REFERENCE);
        if (error != 0)
            return error;
        *pte |= PMAP_PTE_REFERENCE_ACCOUNTED;
        added_reference = 1;
    }
    *pte |= PMAP_PTE_ACCESSED;
    if (access == VM_PROT_WRITE && page != (struct vm_page *)0 &&
        (*pte & PMAP_PTE_DIRTY_ACCOUNTED) == 0) {
        error = vm_page_counter_inc(pmap_allocator, page,
            VM_PAGE_COUNTER_DIRTY);
        if (error != 0) {
            if (added_reference) {
                *pte &= ~PMAP_PTE_REFERENCE_ACCOUNTED;
                (void)vm_page_counter_dec(pmap_allocator, page,
                    VM_PAGE_COUNTER_REFERENCE);
            }
            return error;
        }
        *pte |= PMAP_PTE_DIRTY_ACCOUNTED;
        pmap_stat_increment(&pmap_statistics.pms_tlb_modified);
    }
    if (access == VM_PROT_WRITE)
        *pte |= PMAP_PTE_DIRTY;
    return 0;
}

int
pmap_fault(struct pmap *pmap, vm_vaddr_t vaddr, vm_prot_t access,
    int user)
{
    struct vm_page *page;
    uint32_t *pte;
    vm_vaddr_t page_vaddr;
    int error;

    if (!pmap_valid(pmap) || pmap_active != pmap ||
        (access != VM_PROT_READ && access != VM_PROT_WRITE &&
        access != VM_PROT_EXECUTE) ||
        (user && vaddr >= PMAP_USER_END))
        return EINVAL;
    pte = pmap_lookup_pte(pmap, vaddr);
    if (pte == (uint32_t *)0 ||
        (*pte & (PMAP_PTE_PRESENT | PMAP_PTE_BOOTSTRAP)) !=
        PMAP_PTE_PRESENT)
        return ENOENT;
    if (user && (*pte & PMAP_PTE_USER) == 0)
        return EACCES;
    if (access == VM_PROT_WRITE && (*pte & PMAP_PTE_WRITABLE) == 0) {
        pmap_stat_increment(&pmap_statistics.pms_protection_faults);
        return EACCES;
    }
    page = (struct vm_page *)0;
    if ((*pte & PMAP_PTE_DEVICE) == 0) {
        page = vm_page_lookup(pmap_allocator, *pte & PMAP_PTE_FRAME);
        if (page == (struct vm_page *)0)
            return EFAULT;
    }
    error = pmap_account_access(pte, page, access);
    if (error != 0)
        return error;

    page_vaddr = vm_vaddr_trunc_page(vaddr);
    pmap_diagnostics.ptd_last_pmap = (unsigned)(uintptr_t)pmap;
    pmap_diagnostics.ptd_last_vaddr = page_vaddr;
    pmap_diagnostics.ptd_last_access = access;
    pmap_diagnostics.ptd_last_entryhi = page_vaddr;
    pmap_diagnostics.ptd_last_entrylo0 = *pte;
    return 0;
}

int
pmap_fault_active(vm_vaddr_t vaddr, vm_prot_t access, int user)
{
    if (pmap_active == (struct pmap *)0)
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
    return pte != (uint32_t *)0 &&
        (*pte & (PMAP_PTE_PRESENT | PMAP_PTE_BOOTSTRAP | flag)) ==
        (PMAP_PTE_PRESENT | flag);
}

int
pmap_is_referenced(struct pmap *pmap, vm_vaddr_t vaddr)
{
    return pmap_test_flag(pmap, vaddr, PMAP_PTE_ACCESSED);
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
    if (pte == (uint32_t *)0 ||
        (*pte & (PMAP_PTE_PRESENT | PMAP_PTE_BOOTSTRAP)) !=
        PMAP_PTE_PRESENT)
        return ENOENT;
    if ((*pte & PMAP_PTE_REFERENCE_ACCOUNTED) != 0 &&
        (*pte & PMAP_PTE_DEVICE) == 0) {
        page = vm_page_lookup(pmap_allocator, *pte & PMAP_PTE_FRAME);
        if (page == (struct vm_page *)0)
            return EFAULT;
        error = vm_page_counter_dec(pmap_allocator, page,
            VM_PAGE_COUNTER_REFERENCE);
        if (error != 0)
            return error;
    }
    *pte &= ~(PMAP_PTE_ACCESSED | PMAP_PTE_REFERENCE_ACCOUNTED);
    pmap_invalidate(pmap, vaddr);
    return 0;
}

int
pmap_is_modified(struct pmap *pmap, vm_vaddr_t vaddr)
{
    return pmap_test_flag(pmap, vaddr, PMAP_PTE_DIRTY);
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
    if (pte == (uint32_t *)0 ||
        (*pte & (PMAP_PTE_PRESENT | PMAP_PTE_BOOTSTRAP)) !=
        PMAP_PTE_PRESENT)
        return ENOENT;
    if ((*pte & PMAP_PTE_DIRTY_ACCOUNTED) != 0 &&
        (*pte & PMAP_PTE_DEVICE) == 0) {
        page = vm_page_lookup(pmap_allocator, *pte & PMAP_PTE_FRAME);
        if (page == (struct vm_page *)0)
            return EFAULT;
        error = vm_page_counter_dec(pmap_allocator, page,
            VM_PAGE_COUNTER_DIRTY);
        if (error != 0)
            return error;
    }
    *pte &= ~(PMAP_PTE_DIRTY | PMAP_PTE_DIRTY_ACCOUNTED);
    pmap_invalidate(pmap, vaddr);
    return 0;
}

int
pmap_remove_page(struct vm_page *page)
{
    struct pmap *pmap;
    uint32_t *table;
    vm_vaddr_t vaddr;
    unsigned directory_index;
    unsigned map_index;
    unsigned table_index;
    int error;

    if (!pmap_initialized || page == (struct vm_page *)0 ||
        vm_page_lookup(pmap_allocator, page->vmp_paddr) != page)
        return EINVAL;
    if (page->vmp_hold_count == 0)
        return 0;
    for (map_index = 0; map_index < PMAP_MAX_MAPS; ++map_index) {
        pmap = &pmap_maps[map_index];
        if (!pmap_valid(pmap))
            continue;
        for (directory_index = 0;
            directory_index < PMAP_DIRECTORY_ENTRIES; ++directory_index) {
            if (!pmap_directory_owned(pmap, directory_index))
                continue;
            table = pmap_table_raw(pmap, directory_index);
            if (table == (uint32_t *)0)
                return EFAULT;
            for (table_index = 0; table_index < PMAP_TABLE_ENTRIES;
                ++table_index) {
                if ((table[table_index] & (PMAP_PTE_PRESENT |
                    PMAP_PTE_FRAME | PMAP_PTE_BOOTSTRAP |
                    PMAP_PTE_DEVICE)) !=
                    (PMAP_PTE_PRESENT | page->vmp_paddr))
                    continue;
                vaddr = (directory_index << PMAP_DIRECTORY_SHIFT) |
                    (table_index << VM_PAGE_SHIFT);
                error = pmap_remove_pte(pmap, vaddr,
                    &table[table_index]);
                if (error != 0)
                    return error;
                if (page->vmp_hold_count == 0)
                    return 0;
            }
        }
    }
    return 0;
}

static int
pmap_clear_page_flag(struct vm_page *page, uint32_t flag)
{
    struct pmap *pmap;
    uint32_t *table;
    vm_vaddr_t vaddr;
    unsigned directory_index;
    unsigned map_index;
    unsigned table_index;
    int error;

    if (!pmap_initialized || page == (struct vm_page *)0 ||
        vm_page_lookup(pmap_allocator, page->vmp_paddr) != page)
        return EINVAL;
    for (map_index = 0; map_index < PMAP_MAX_MAPS; ++map_index) {
        pmap = &pmap_maps[map_index];
        if (!pmap_valid(pmap))
            continue;
        for (directory_index = 0;
            directory_index < PMAP_DIRECTORY_ENTRIES; ++directory_index) {
            if (!pmap_directory_owned(pmap, directory_index))
                continue;
            table = pmap_table_raw(pmap, directory_index);
            if (table == (uint32_t *)0)
                return EFAULT;
            for (table_index = 0; table_index < PMAP_TABLE_ENTRIES;
                ++table_index) {
                if ((table[table_index] & (PMAP_PTE_PRESENT |
                    PMAP_PTE_FRAME | PMAP_PTE_BOOTSTRAP |
                    PMAP_PTE_DEVICE | flag)) !=
                    (PMAP_PTE_PRESENT | page->vmp_paddr | flag))
                    continue;
                vaddr = (directory_index << PMAP_DIRECTORY_SHIFT) |
                    (table_index << VM_PAGE_SHIFT);
                error = flag == PMAP_PTE_ACCESSED ?
                    pmap_clear_reference(pmap, vaddr) :
                    pmap_clear_modify(pmap, vaddr);
                if (error != 0)
                    return error;
            }
        }
    }
    return 0;
}

int
pmap_clear_page_reference(struct vm_page *page)
{
    return pmap_clear_page_flag(page, PMAP_PTE_ACCESSED);
}

int
pmap_clear_page_modify(struct vm_page *page)
{
    return pmap_clear_page_flag(page, PMAP_PTE_DIRTY);
}

vm_paddr_t
pmap_cache_alias_mask(void)
{
    return 0;
}

int
pmap_page_sync(struct vm_page *page, unsigned operations)
{
    if (!pmap_initialized || page == (struct vm_page *)0 ||
        operations == 0 ||
        (operations & ~(PMAP_SYNC_DATA | PMAP_SYNC_INSTRUCTION |
        PMAP_INVALIDATE_DATA)) != 0 ||
        (operations & (PMAP_SYNC_DATA | PMAP_INVALIDATE_DATA)) ==
        (PMAP_SYNC_DATA | PMAP_INVALIDATE_DATA) ||
        vm_page_lookup(pmap_allocator, page->vmp_paddr) != page)
        return EINVAL;
    return 0;
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
    return 0;
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

    if (!pmap_initialized || page == (struct vm_page *)0 ||
        page_count == 0 || page_count > VM_SIZE_MAX / VM_PAGE_SIZE ||
        (cache != PMAP_CACHE_CACHED && cache != PMAP_CACHE_UNCACHED))
        return (void *)0;
    paddr = page->vmp_paddr;
    size = page_count * VM_PAGE_SIZE;
    if (size > I386_DIRECT_MAP_SIZE ||
        paddr > I386_DIRECT_MAP_SIZE - size)
        return (void *)0;
    for (index = 0; index < page_count; ++index) {
        expected = paddr + index * VM_PAGE_SIZE;
        current = vm_page_lookup(pmap_allocator, expected);
        if (current == (struct vm_page *)0 ||
            (index == 0 && current != page) ||
            current->vmp_state == VM_PAGE_FREE ||
            current->vmp_state == VM_PAGE_RESERVED ||
            current->vmp_state == VM_PAGE_BAD)
            return (void *)0;
    }
    return pmap_direct(paddr);
}

void *
pmap_page_direct_map(struct vm_page *page, enum pmap_cache cache)
{
    return pmap_pages_direct_map(page, 1, cache);
}

static void *
pmap_device_window_find(vm_paddr_t paddr, enum pmap_cache cache,
    int *cache_alias)
{
    uint32_t *directory;
    uint32_t *table;
    uint32_t entry;
    vm_vaddr_t vaddr;

    *cache_alias = 0;
    directory = (uint32_t *)pmap_direct(pmap_bootstrap_directory);
    if (directory == (uint32_t *)0)
        return (void *)0;
    for (vaddr = I386_DEVICE_VADDR_START;
        vaddr < pmap_device_vaddr_next; vaddr += VM_PAGE_SIZE) {
        entry = directory[pmap_directory_index(vaddr)];
        if ((entry & PMAP_PTE_PRESENT) == 0)
            continue;
        table = (uint32_t *)pmap_direct(entry & PMAP_PTE_FRAME);
        if (table == (uint32_t *)0)
            return (void *)0;
        entry = table[pmap_table_index(vaddr)];
        if ((entry & (PMAP_PTE_PRESENT | PMAP_PTE_DEVICE)) !=
            (PMAP_PTE_PRESENT | PMAP_PTE_DEVICE) ||
            (entry & PMAP_PTE_FRAME) != paddr)
            continue;
        if (((entry & PMAP_PTE_CACHE_DISABLE) != 0) !=
            (cache == PMAP_CACHE_UNCACHED)) {
            *cache_alias = 1;
            return (void *)0;
        }
        return (void *)(uintptr_t)vaddr;
    }
    return (void *)0;
}

static int
pmap_device_window_table(vm_vaddr_t vaddr, uint32_t **result)
{
    uint32_t *directory;
    uint32_t *table;
    vm_paddr_t table_paddr;
    uint32_t directory_entry;
    unsigned directory_index;
    unsigned i;
    int error;

    directory = (uint32_t *)pmap_direct(pmap_bootstrap_directory);
    if (directory == (uint32_t *)0)
        return EFAULT;
    directory_index = pmap_directory_index(vaddr);
    directory_entry = directory[directory_index];
    if ((directory_entry & PMAP_PTE_PRESENT) == 0) {
        for (i = 0; i < PMAP_MAX_MAPS; ++i) {
            if (!pmap_maps[i].pm_in_use)
                continue;
            if (pmap_directory_owned(&pmap_maps[i], directory_index) ||
                (pmap_maps[i].pm_directory[directory_index] &
                PMAP_PTE_PRESENT) != 0)
                return EBUSY;
        }
        error = pmap_alloc_table_page(&table_paddr, &table);
        if (error != 0)
            return error;
        directory_entry = table_paddr | PMAP_PTE_PRESENT |
            PMAP_PTE_WRITABLE;
        directory[directory_index] = directory_entry;
    } else {
        table = (uint32_t *)pmap_direct(
            directory_entry & PMAP_PTE_FRAME);
        if (table == (uint32_t *)0)
            return EFAULT;
    }
    for (i = 0; i < PMAP_MAX_MAPS; ++i) {
        if (!pmap_maps[i].pm_in_use)
            continue;
        if (pmap_directory_owned(&pmap_maps[i], directory_index))
            return EBUSY;
        if ((pmap_maps[i].pm_directory[directory_index] &
            PMAP_PTE_PRESENT) != 0 &&
            pmap_maps[i].pm_directory[directory_index] !=
            directory_entry)
            return EBUSY;
    }
    for (i = 0; i < PMAP_MAX_MAPS; ++i) {
        if (!pmap_maps[i].pm_in_use)
            continue;
        pmap_maps[i].pm_directory[directory_index] = directory_entry;
    }
    *result = table;
    return 0;
}

void *
pmap_device_direct_map(vm_paddr_t paddr, enum pmap_cache cache)
{
    struct vm_page *page;
    uint32_t *table;
    uint32_t entry;
    void *mapping;
    int cache_alias;

    if (!pmap_initialized || !vm_paddr_page_aligned(paddr) ||
        (cache != PMAP_CACHE_CACHED && cache != PMAP_CACHE_UNCACHED))
        return (void *)0;
    if (paddr < I386_DIRECT_MAP_SIZE) {
        page = vm_page_lookup(pmap_allocator, paddr);
        if (page == (struct vm_page *)0 && paddr >= 0x00400000u)
            return (void *)0;
        return pmap_direct(paddr);
    }

    mapping = pmap_device_window_find(paddr, cache, &cache_alias);
    if (mapping != (void *)0 || cache_alias)
        return mapping;
    if (pmap_device_vaddr_next < I386_DEVICE_VADDR_START ||
        pmap_device_vaddr_next >= I386_DEVICE_VADDR_END)
        return (void *)0;
    if (pmap_device_window_table(pmap_device_vaddr_next, &table) != 0)
        return (void *)0;
    entry = table[pmap_table_index(pmap_device_vaddr_next)];
    if ((entry & PMAP_PTE_PRESENT) != 0)
        return (void *)0;
    entry = paddr | PMAP_PTE_PRESENT | PMAP_PTE_WRITABLE |
        PMAP_PTE_DEVICE;
    if (cache == PMAP_CACHE_UNCACHED)
        entry |= PMAP_PTE_CACHE_DISABLE;
    table[pmap_table_index(pmap_device_vaddr_next)] = entry;
    mapping = (void *)(uintptr_t)pmap_device_vaddr_next;
    i386_paging_invalidate_page(pmap_device_vaddr_next);
    pmap_device_vaddr_next += VM_PAGE_SIZE;
    return mapping;
}

int
pmap_get_stats(struct pmap_stats *stats)
{
    if (!pmap_initialized || stats == (struct pmap_stats *)0)
        return EINVAL;
    *stats = pmap_statistics;
    return 0;
}

int
pmap_get_tlb_diagnostics(vm_vaddr_t vaddr,
    struct pmap_tlb_diagnostics *diagnostics)
{
    uint32_t *pte;

    if (!pmap_initialized ||
        diagnostics == (struct pmap_tlb_diagnostics *)0 ||
        vaddr >= PMAP_USER_END)
        return EINVAL;
    *diagnostics = pmap_diagnostics;
    diagnostics->ptd_active_pmap =
        (unsigned)(uintptr_t)pmap_active;
    diagnostics->ptd_active_asid = 0;
    diagnostics->ptd_query_pte = 0;
    diagnostics->ptd_active_directory = pmap_active == (struct pmap *)0 ?
        pmap_bootstrap_directory : pmap_active->pm_directory_paddr;
    diagnostics->ptd_fast_directory = 0;
    if (!pmap_valid(pmap_active))
        return 0;
    pte = pmap_lookup_pte(pmap_active, vaddr);
    if (pte != (uint32_t *)0)
        diagnostics->ptd_query_pte = *pte;
    return 0;
}

int
pmap_validate(struct pmap *pmap)
{
    const struct vm_page *page;
    uint32_t *table;
    uint32_t pte;
    unsigned directory_index;
    unsigned table_index;

    if (!pmap_valid(pmap) || pmap->pm_directory == (uint32_t *)0 ||
        !vm_paddr_page_aligned(pmap->pm_directory_paddr))
        return EINVAL;
    page = vm_page_lookup(pmap_allocator, pmap->pm_directory_paddr);
    if (page == (const struct vm_page *)0 ||
        page->vmp_state != VM_PAGE_WIRED || page->vmp_wire_count == 0)
        return EFAULT;
    for (directory_index = 0;
        directory_index < PMAP_DIRECTORY_ENTRIES; ++directory_index) {
        if (!pmap_directory_owned(pmap, directory_index))
            continue;
        if ((pmap->pm_directory[directory_index] &
            (PMAP_PTE_PRESENT | PMAP_PTE_WRITABLE | PMAP_PTE_USER)) !=
            (PMAP_PTE_PRESENT | PMAP_PTE_WRITABLE | PMAP_PTE_USER))
            return EFAULT;
        page = vm_page_lookup(pmap_allocator,
            pmap->pm_directory[directory_index] & PMAP_PTE_FRAME);
        if (page == (const struct vm_page *)0 ||
            page->vmp_state != VM_PAGE_WIRED ||
            page->vmp_wire_count == 0)
            return EFAULT;
        table = pmap_table_raw(pmap, directory_index);
        if (table == (uint32_t *)0)
            return EFAULT;
        for (table_index = 0; table_index < PMAP_TABLE_ENTRIES;
            ++table_index) {
            pte = table[table_index];
            if ((pte & PMAP_PTE_PRESENT) == 0 ||
                (pte & PMAP_PTE_BOOTSTRAP) != 0)
                continue;
            if ((pte & PMAP_PTE_USER) == 0)
                return EFAULT;
            page = vm_page_lookup(pmap_allocator,
                pte & PMAP_PTE_FRAME);
            if ((pte & PMAP_PTE_DEVICE) != 0) {
                if (page != (const struct vm_page *)0 &&
                    page->vmp_state != VM_PAGE_RESERVED)
                    return EFAULT;
                continue;
            }
            if (page == (const struct vm_page *)0 ||
                page->vmp_state == VM_PAGE_FREE ||
                page->vmp_state == VM_PAGE_RESERVED ||
                page->vmp_state == VM_PAGE_BAD ||
                page->vmp_hold_count == 0)
                return EFAULT;
        }
    }
    return 0;
}

void
pmap_md_legacy_user_disable(void)
{
    if (pmap_active != (struct pmap *)0)
        pmap_deactivate(pmap_active);
}

static int
pmap_selftest_alloc(struct vm_page **result)
{
    struct vm_page_request request;

    vm_page_request_init(&request);
    request.vpr_state = VM_PAGE_ACTIVE;
    request.vpr_max_address = I386_DIRECT_MAP_SIZE - 1u;
    return vm_page_alloc(pmap_allocator, &request, result);
}

static int
pmap_selftest_free(struct vm_page *page)
{
    if (page == (struct vm_page *)0)
        return 0;
    return vm_page_free(pmap_allocator, page, 1);
}

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
    struct pmap *first;
    struct pmap *second;
    struct vm_page *first_page;
    struct vm_page *second_page;
    struct vm_page *code_page;
    volatile uint32_t *first_backing;
    volatile uint32_t *second_backing;
    volatile unsigned char *code_backing;
    volatile uint32_t *test_address;
    void *device_mapping;
    vm_paddr_t paddr;
    vm_pfn_t free_before;
    int error;

    first = (struct pmap *)0;
    second = (struct pmap *)0;
    first_page = (struct vm_page *)0;
    second_page = (struct vm_page *)0;
    code_page = (struct vm_page *)0;
    device_mapping = pmap_device_direct_map(PMAP_SELFTEST_DEVICE_PADDR,
        PMAP_CACHE_UNCACHED);
    if ((vm_vaddr_t)(uintptr_t)device_mapping <
        I386_DEVICE_VADDR_START ||
        (vm_vaddr_t)(uintptr_t)device_mapping >=
        I386_DEVICE_VADDR_END ||
        pmap_device_direct_map(PMAP_SELFTEST_DEVICE_PADDR,
        PMAP_CACHE_UNCACHED) != device_mapping ||
        pmap_device_direct_map(PMAP_SELFTEST_DEVICE_PADDR,
        PMAP_CACHE_CACHED) != (void *)0)
        return EFAULT;
    free_before = pmap_allocator->vpa_free_count;
    error = pmap_create(&first);
    if (error != 0)
        goto out;
    error = pmap_create(&second);
    if (error != 0)
        goto out;
    /*
     * Force a private copy of the low bootstrap PDE.  The inherited
     * identity entries must keep the low-linked kernel runnable after CR3
     * activation, but they must not appear as user mappings.
     */
    error = pmap_prepare(first, 0x00002000u);
    if (error != 0)
        goto out;
    error = pmap_extract(first, 0x00002000u, &paddr);
    if (error != ENOENT) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = pmap_enter_device(first, PMAP_SELFTEST_DEVICE_VA,
        PMAP_SELFTEST_DEVICE_PADDR, VM_PROT_READ | VM_PROT_WRITE,
        PMAP_CACHE_UNCACHED);
    if (error != 0)
        goto out;
    error = pmap_extract(first, PMAP_SELFTEST_DEVICE_VA, &paddr);
    if (error != 0 || paddr != PMAP_SELFTEST_DEVICE_PADDR) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = pmap_remove(first, PMAP_SELFTEST_DEVICE_VA,
        PMAP_SELFTEST_DEVICE_VA + VM_PAGE_SIZE);
    if (error != 0)
        goto out;
    error = pmap_selftest_alloc(&first_page);
    if (error != 0)
        goto out;
    error = pmap_selftest_alloc(&second_page);
    if (error != 0)
        goto out;
    error = pmap_selftest_alloc(&code_page);
    if (error != 0)
        goto out;
    first_backing = pmap_page_direct_map(first_page, PMAP_CACHE_CACHED);
    second_backing = pmap_page_direct_map(second_page, PMAP_CACHE_CACHED);
    code_backing = pmap_page_direct_map(code_page, PMAP_CACHE_CACHED);
    if (first_backing == (volatile uint32_t *)0 ||
        second_backing == (volatile uint32_t *)0 ||
        code_backing == (volatile unsigned char *)0) {
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
    code_backing[0] = 0xb8u;
    code_backing[1] = 42u;
    code_backing[2] = 0;
    code_backing[3] = 0;
    code_backing[4] = 0;
    code_backing[5] = 0xc3u;
    error = pmap_enter(first, PMAP_SELFTEST_CODE_VA, code_page,
        VM_PROT_READ | VM_PROT_EXECUTE, PMAP_CACHE_CACHED);
    if (error != 0)
        goto out;
    error = pmap_fault(first, PMAP_SELFTEST_CODE_VA,
        VM_PROT_EXECUTE, 0);
    if (error != 0 || pmap_selftest_execute(PMAP_SELFTEST_CODE_VA) != 42) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = pmap_extract(first, PMAP_SELFTEST_DATA_VA + 37u, &paddr);
    if (error != 0 || paddr != second_page->vmp_paddr + 37u) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    error = pmap_validate(first);
    if (error != 0)
        goto out;
    error = 0;

out:
    if (first != (struct pmap *)0) {
        if (pmap_destroy(first) != 0 && error == 0)
            error = EFAULT;
    }
    if (second != (struct pmap *)0) {
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
    return error;
}

int
pmap_bootstrap_stats(struct pmap_stats *stats)
{
    return pmap_get_stats(stats);
}
