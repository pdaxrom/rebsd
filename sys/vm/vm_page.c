/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifdef REBSD_VM_HOST_TEST
#include <errno.h>
#else
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#endif
#include <vm/vm_page.h>
#include <vm/vm_assert.h>

static int
vm_page_state_valid(enum vm_page_state state)
{
    return state >= VM_PAGE_FREE && state <= VM_PAGE_RESERVED;
}

static int
vm_page_alloc_state_valid(enum vm_page_state state)
{
    return vm_page_state_valid(state) && state != VM_PAGE_FREE &&
        state != VM_PAGE_BAD && state != VM_PAGE_RESERVED;
}

static int
vm_page_power_of_two(vm_size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static void
vm_page_stat_add(vm_pfn_t *value, vm_pfn_t amount)
{
    if (amount > VM_PFN_MAX - *value)
        *value = VM_PFN_MAX;
    else
        *value += amount;
}

static int
vm_page_map_page_count(const struct vm_phys_map *map, vm_pfn_t *count)
{
    const struct vm_phys_region *region;
    vm_pfn_t pages;
    vm_pfn_t total;
    unsigned i;
    int error;

    if (map == 0 || count == 0)
        return EINVAL;
    error = vm_phys_map_validate(map);
    if (error != 0)
        return error;
    total = 0;
    for (i = 0; i < vm_phys_map_count(map); ++i) {
        region = vm_phys_map_region(map, i);
        pages = (region->vpr_end - region->vpr_start) >> VM_PAGE_SHIFT;
        if (pages > VM_PFN_MAX - total)
            return EOVERFLOW;
        total += pages;
    }
    if (total == 0)
        return EINVAL;
    *count = total;
    return 0;
}

int
vm_page_metadata_reserve(struct vm_phys_map *map, vm_paddr_t *start,
    vm_size_t *size, vm_pfn_t *page_count)
{
    const struct vm_phys_region *region;
    vm_paddr_t best_start;
    vm_size_t best_size;
    vm_size_t bytes;
    vm_size_t rounded;
    vm_size_t region_size;
    vm_pfn_t pages;
    unsigned i;
    int error;

    if (map == 0 || start == 0 || size == 0 || page_count == 0 ||
        map->vpm_finalized)
        return EINVAL;
    error = vm_page_map_page_count(map, &pages);
    if (error != 0)
        return error;
    if (pages > VM_SIZE_MAX / sizeof(struct vm_page))
        return EOVERFLOW;
    bytes = pages * sizeof(struct vm_page);
    error = vm_size_round_page(bytes, &rounded);
    if (error != 0)
        return error;

    best_start = 0;
    best_size = 0;
    for (i = 0; i < vm_phys_map_count(map); ++i) {
        region = vm_phys_map_region(map, i);
        if (region->vpr_kind != VM_PHYS_AVAILABLE)
            continue;
        region_size = region->vpr_end - region->vpr_start;
        if (region_size >= rounded && region_size > best_size) {
            best_start = region->vpr_start;
            best_size = region_size;
        }
    }
    if (best_size == 0)
        return ENOSPC;
    error = vm_phys_map_reserve(map, best_start, rounded,
        "vm_page metadata");
    if (error != 0)
        return error;
    *start = best_start;
    *size = rounded;
    *page_count = pages;
    return 0;
}

int
vm_page_allocator_init(struct vm_page_allocator *allocator,
    const struct vm_phys_map *map, void *storage, vm_size_t storage_size)
{
    const struct vm_phys_region *region;
    struct vm_page *page;
    vm_paddr_t paddr;
    vm_size_t needed;
    vm_pfn_t page_count;
    vm_pfn_t index;
    unsigned i;
    int error;

    if (allocator == 0 || map == 0 || storage == 0 ||
        !map->vpm_finalized)
        return EINVAL;
    error = vm_page_map_page_count(map, &page_count);
    if (error != 0)
        return error;
    if (page_count > VM_SIZE_MAX / sizeof(struct vm_page))
        return EOVERFLOW;
    needed = page_count * sizeof(struct vm_page);
    if (storage_size < needed)
        return ENOSPC;

    allocator->vpa_pages = (struct vm_page *)storage;
    allocator->vpa_page_count = page_count;
    allocator->vpa_free_count = 0;
    allocator->vpa_reserved_count = 0;
    allocator->vpa_bad_count = 0;
    allocator->vpa_allocations = 0;
    allocator->vpa_frees = 0;
    allocator->vpa_allocation_failures = 0;
    allocator->vpa_poison_failures = 0;
    allocator->vpa_poison = 0;
    allocator->vpa_poison_arg = 0;
    allocator->vpa_initialized = 0;

    index = 0;
    for (i = 0; i < vm_phys_map_count(map); ++i) {
        region = vm_phys_map_region(map, i);
        for (paddr = region->vpr_start; paddr < region->vpr_end;
            paddr += VM_PAGE_SIZE) {
            page = &allocator->vpa_pages[index++];
            page->vmp_paddr = paddr;
            page->vmp_wire_count = 0;
            page->vmp_hold_count = 0;
            page->vmp_reference_count = 0;
            page->vmp_dirty_count = 0;
            page->vmp_busy_count = 0;
            page->vmp_flags = 0;
            if (region->vpr_kind == VM_PHYS_AVAILABLE) {
                page->vmp_state = VM_PAGE_FREE;
                ++allocator->vpa_free_count;
            } else {
                page->vmp_state = VM_PAGE_RESERVED;
                ++allocator->vpa_reserved_count;
            }
        }
    }
    if (index != page_count)
        return EINVAL;
    allocator->vpa_initialized = 1;
    return vm_page_allocator_validate(allocator, map);
}

void
vm_page_allocator_set_poison(struct vm_page_allocator *allocator,
    vm_page_poison_fn poison, void *arg)
{
    if (allocator == 0 || !allocator->vpa_initialized)
        return;
    allocator->vpa_poison = poison;
    allocator->vpa_poison_arg = arg;
}

void
vm_page_request_init(struct vm_page_request *request)
{
    if (request == 0)
        return;
    request->vpr_npages = 1;
    request->vpr_alignment = VM_PAGE_SIZE;
    request->vpr_boundary = 0;
    request->vpr_max_address = VM_PADDR_MAX;
    request->vpr_color_mask = 0;
    request->vpr_color = 0;
    request->vpr_state = VM_PAGE_ACTIVE;
}

static int
vm_page_request_validate(const struct vm_page_request *request,
    vm_size_t *run_size)
{
    if (request == 0 || run_size == 0 || request->vpr_npages == 0 ||
        request->vpr_npages > VM_SIZE_MAX / VM_PAGE_SIZE ||
        !vm_page_alloc_state_valid(request->vpr_state) ||
        (request->vpr_color_mask & VM_PAGE_MASK) != 0 ||
        (request->vpr_color & ~request->vpr_color_mask) != 0 ||
        request->vpr_alignment < VM_PAGE_SIZE ||
        !vm_size_page_aligned(request->vpr_alignment) ||
        !vm_page_power_of_two(request->vpr_alignment))
        return EINVAL;
    if (request->vpr_boundary != 0 &&
        (request->vpr_boundary < VM_PAGE_SIZE ||
        !vm_size_page_aligned(request->vpr_boundary) ||
        !vm_page_power_of_two(request->vpr_boundary)))
        return EINVAL;
    *run_size = request->vpr_npages * VM_PAGE_SIZE;
    if (request->vpr_boundary != 0 &&
        *run_size > request->vpr_boundary)
        return EINVAL;
    return 0;
}

static int
vm_page_poison_check(struct vm_page_allocator *allocator,
    struct vm_page *page)
{
    int error;

    if ((page->vmp_flags & VM_PAGE_FLAG_POISONED) == 0 ||
        allocator->vpa_poison == 0)
        return 0;
    error = (*allocator->vpa_poison)(allocator->vpa_poison_arg,
        page->vmp_paddr, VM_PAGE_FREE_POISON, 1);
    if (error != 0)
        vm_page_stat_add(&allocator->vpa_poison_failures, 1);
    return error;
}

static int
vm_page_scrub(struct vm_page_allocator *allocator, struct vm_page *page)
{
    int error;

    if (allocator->vpa_poison == 0)
        return 0;
    error = (*allocator->vpa_poison)(allocator->vpa_poison_arg,
        page->vmp_paddr, 0, 0);
    if (error != 0)
        vm_page_stat_add(&allocator->vpa_poison_failures, 1);
    return error;
}

int
vm_page_alloc(struct vm_page_allocator *allocator,
    const struct vm_page_request *request, struct vm_page **result)
{
    struct vm_page *page;
    vm_paddr_t start;
    vm_paddr_t last_start;
    vm_paddr_t last_byte;
    vm_size_t run_size;
    vm_pfn_t i;
    vm_pfn_t j;
    int error;

    if (allocator == 0 || result == 0 || !allocator->vpa_initialized)
        return EINVAL;
    *result = 0;
    error = vm_page_request_validate(request, &run_size);
    if (error != 0)
        return error;
    if (request->vpr_npages > allocator->vpa_free_count) {
        vm_page_stat_add(&allocator->vpa_allocation_failures, 1);
        return ENOMEM;
    }

    for (i = 0; i + request->vpr_npages <= allocator->vpa_page_count;
        ++i) {
        page = &allocator->vpa_pages[i];
        start = page->vmp_paddr;
        if (page->vmp_state != VM_PAGE_FREE ||
            (start & (request->vpr_alignment - 1)) != 0 ||
            (start & request->vpr_color_mask) != request->vpr_color)
            continue;
        if (run_size - 1 > VM_PADDR_MAX - start)
            continue;
        last_byte = start + run_size - 1;
        if (last_byte > request->vpr_max_address)
            continue;
        if (request->vpr_boundary != 0 &&
            (start / request->vpr_boundary) !=
            (last_byte / request->vpr_boundary))
            continue;
        last_start = start + run_size - VM_PAGE_SIZE;
        for (j = 0; j < request->vpr_npages; ++j) {
            page = &allocator->vpa_pages[i + j];
            if (page->vmp_state != VM_PAGE_FREE ||
                page->vmp_paddr != start + j * VM_PAGE_SIZE)
                break;
        }
        if (j != request->vpr_npages ||
            allocator->vpa_pages[i + j - 1].vmp_paddr != last_start)
            continue;
        for (j = 0; j < request->vpr_npages; ++j) {
            error = vm_page_poison_check(allocator,
                &allocator->vpa_pages[i + j]);
            if (error != 0) {
                vm_page_stat_add(&allocator->vpa_allocation_failures, 1);
                return EFAULT;
            }
        }
        for (j = 0; j < request->vpr_npages; ++j) {
            error = vm_page_scrub(allocator,
                &allocator->vpa_pages[i + j]);
            if (error != 0) {
                vm_pfn_t rollback;

                for (rollback = 0; rollback <= j; ++rollback)
                    (void)(*allocator->vpa_poison)(
                        allocator->vpa_poison_arg,
                        allocator->vpa_pages[i + rollback].vmp_paddr,
                        VM_PAGE_FREE_POISON, 0);
                vm_page_stat_add(&allocator->vpa_allocation_failures, 1);
                return error;
            }
        }
        for (j = 0; j < request->vpr_npages; ++j) {
            page = &allocator->vpa_pages[i + j];
            page->vmp_state = request->vpr_state;
            page->vmp_flags &= ~VM_PAGE_FLAG_POISONED;
            if (request->vpr_state == VM_PAGE_WIRED)
                page->vmp_wire_count = 1;
            if (request->vpr_state == VM_PAGE_BUSY)
                page->vmp_busy_count = 1;
        }
        allocator->vpa_free_count -= request->vpr_npages;
        vm_page_stat_add(&allocator->vpa_allocations,
            request->vpr_npages);
        *result = &allocator->vpa_pages[i];
        VM_ASSERT((*result)->vmp_state == request->vpr_state);
        VM_ASSERT(allocator->vpa_free_count < allocator->vpa_page_count);
        return 0;
    }
    vm_page_stat_add(&allocator->vpa_allocation_failures, 1);
    return ENOMEM;
}

static int
vm_page_index(struct vm_page_allocator *allocator, struct vm_page *page,
    vm_pfn_t *index)
{
    uintptr_t base;
    uintptr_t address;
    uintptr_t offset;

    if (allocator == 0 || page == 0 || index == 0 ||
        !allocator->vpa_initialized)
        return EINVAL;
    base = (uintptr_t)allocator->vpa_pages;
    address = (uintptr_t)page;
    if (address < base)
        return EINVAL;
    offset = address - base;
    if (offset % sizeof(*page) != 0 ||
        offset / sizeof(*page) >= allocator->vpa_page_count)
        return EINVAL;
    *index = (vm_pfn_t)(offset / sizeof(*page));
    return 0;
}

static int
vm_page_counts_zero(const struct vm_page *page)
{
    return page->vmp_wire_count == 0 && page->vmp_hold_count == 0 &&
        page->vmp_reference_count == 0 && page->vmp_dirty_count == 0 &&
        page->vmp_busy_count == 0;
}

int
vm_page_free(struct vm_page_allocator *allocator, struct vm_page *first,
    vm_pfn_t npages)
{
    struct vm_page *page;
    vm_paddr_t expected;
    vm_pfn_t index;
    vm_pfn_t i;
    int error;

    if (npages == 0)
        return EINVAL;
    error = vm_page_index(allocator, first, &index);
    if (error != 0 || npages > allocator->vpa_page_count - index)
        return EINVAL;
    expected = first->vmp_paddr;
    for (i = 0; i < npages; ++i) {
        page = &allocator->vpa_pages[index + i];
        if (page->vmp_paddr != expected + i * VM_PAGE_SIZE)
            return EINVAL;
        if (page->vmp_state == VM_PAGE_FREE)
            return EALREADY;
        if (page->vmp_state == VM_PAGE_RESERVED ||
            page->vmp_state == VM_PAGE_BAD)
            return EPERM;
        if (!vm_page_counts_zero(page))
            return EBUSY;
    }
    if (allocator->vpa_poison != 0) {
        for (i = 0; i < npages; ++i) {
            page = &allocator->vpa_pages[index + i];
            error = (*allocator->vpa_poison)(allocator->vpa_poison_arg,
                page->vmp_paddr, VM_PAGE_FREE_POISON, 0);
            if (error != 0) {
                vm_page_stat_add(&allocator->vpa_poison_failures, 1);
                return EIO;
            }
        }
    }
    for (i = 0; i < npages; ++i) {
        page = &allocator->vpa_pages[index + i];
        page->vmp_state = VM_PAGE_FREE;
        if (allocator->vpa_poison != 0)
            page->vmp_flags |= VM_PAGE_FLAG_POISONED;
        else
            page->vmp_flags &= ~VM_PAGE_FLAG_POISONED;
    }
    allocator->vpa_free_count += npages;
    vm_page_stat_add(&allocator->vpa_frees, npages);
    return 0;
}

struct vm_page *
vm_page_lookup(struct vm_page_allocator *allocator, vm_paddr_t paddr)
{
    vm_pfn_t low;
    vm_pfn_t high;
    vm_pfn_t middle;
    vm_paddr_t page_paddr;

    if (allocator == 0 || !allocator->vpa_initialized ||
        !vm_paddr_page_aligned(paddr))
        return 0;
    low = 0;
    high = allocator->vpa_page_count;
    while (low < high) {
        middle = low + (high - low) / 2;
        page_paddr = allocator->vpa_pages[middle].vmp_paddr;
        if (paddr < page_paddr)
            high = middle;
        else if (paddr > page_paddr)
            low = middle + 1;
        else
            return &allocator->vpa_pages[middle];
    }
    return 0;
}

int
vm_page_free_paddr(struct vm_page_allocator *allocator, vm_paddr_t paddr,
    vm_pfn_t npages)
{
    struct vm_page *page;

    page = vm_page_lookup(allocator, paddr);
    if (page == 0)
        return ENOENT;
    return vm_page_free(allocator, page, npages);
}

int
vm_page_set_state(struct vm_page_allocator *allocator, struct vm_page *page,
    enum vm_page_state state)
{
    vm_pfn_t index;
    int error;

    if (!vm_page_alloc_state_valid(state))
        return EINVAL;
    error = vm_page_index(allocator, page, &index);
    if (error != 0)
        return error;
    if (page->vmp_state == VM_PAGE_FREE ||
        page->vmp_state == VM_PAGE_RESERVED ||
        page->vmp_state == VM_PAGE_BAD)
        return EPERM;
    if (state == VM_PAGE_WIRED && page->vmp_wire_count == 0)
        return EBUSY;
    if (state == VM_PAGE_BUSY && page->vmp_busy_count == 0)
        return EBUSY;
    page->vmp_state = state;
    return 0;
}

int
vm_page_mark_bad(struct vm_page_allocator *allocator, struct vm_page *page)
{
    vm_pfn_t index;
    int error;

    error = vm_page_index(allocator, page, &index);
    if (error != 0)
        return error;
    if (page->vmp_state == VM_PAGE_RESERVED)
        return EPERM;
    if (page->vmp_state == VM_PAGE_BAD)
        return EALREADY;
    if (page->vmp_state != VM_PAGE_FREE || !vm_page_counts_zero(page))
        return EBUSY;
    page->vmp_state = VM_PAGE_BAD;
    page->vmp_flags = 0;
    --allocator->vpa_free_count;
    vm_page_stat_add(&allocator->vpa_bad_count, 1);
    return 0;
}

static uint16_t *
vm_page_counter_pointer(struct vm_page *page, enum vm_page_counter counter)
{
    switch (counter) {
    case VM_PAGE_COUNTER_WIRE:
        return &page->vmp_wire_count;
    case VM_PAGE_COUNTER_HOLD:
        return &page->vmp_hold_count;
    case VM_PAGE_COUNTER_REFERENCE:
        return &page->vmp_reference_count;
    case VM_PAGE_COUNTER_DIRTY:
        return &page->vmp_dirty_count;
    case VM_PAGE_COUNTER_BUSY:
        return &page->vmp_busy_count;
    default:
        return 0;
    }
}

int
vm_page_counter_inc(struct vm_page_allocator *allocator,
    struct vm_page *page, enum vm_page_counter counter)
{
    uint16_t *value;
    vm_pfn_t index;
    int error;

    error = vm_page_index(allocator, page, &index);
    if (error != 0)
        return error;
    if (page->vmp_state == VM_PAGE_FREE ||
        page->vmp_state == VM_PAGE_RESERVED ||
        page->vmp_state == VM_PAGE_BAD)
        return EPERM;
    value = vm_page_counter_pointer(page, counter);
    if (value == 0)
        return EINVAL;
    if (*value == UINT16_MAX)
        return EOVERFLOW;
    ++*value;
    return 0;
}

int
vm_page_counter_dec(struct vm_page_allocator *allocator,
    struct vm_page *page, enum vm_page_counter counter)
{
    uint16_t *value;
    vm_pfn_t index;
    int error;

    error = vm_page_index(allocator, page, &index);
    if (error != 0)
        return error;
    if (page->vmp_state == VM_PAGE_FREE ||
        page->vmp_state == VM_PAGE_RESERVED ||
        page->vmp_state == VM_PAGE_BAD)
        return EPERM;
    value = vm_page_counter_pointer(page, counter);
    if (value == 0)
        return EINVAL;
    if (*value == 0)
        return EINVAL;
    --*value;
    return 0;
}

static int
vm_page_matches_map(const struct vm_page *page,
    const struct vm_phys_map *map)
{
    const struct vm_phys_region *region;
    unsigned i;

    for (i = 0; i < vm_phys_map_count(map); ++i) {
        region = vm_phys_map_region(map, i);
        if (page->vmp_paddr < region->vpr_start)
            break;
        if (page->vmp_paddr >= region->vpr_end)
            continue;
        if (region->vpr_kind == VM_PHYS_RESERVED)
            return page->vmp_state == VM_PAGE_RESERVED;
        return page->vmp_state != VM_PAGE_RESERVED;
    }
    return 0;
}

int
vm_page_allocator_validate(const struct vm_page_allocator *allocator,
    const struct vm_phys_map *map)
{
    const struct vm_page *page;
    vm_pfn_t free_count;
    vm_pfn_t reserved_count;
    vm_pfn_t bad_count;
    vm_pfn_t i;

    if (allocator == 0 || map == 0 || !allocator->vpa_initialized ||
        allocator->vpa_pages == 0 || allocator->vpa_page_count == 0 ||
        !map->vpm_finalized)
        return EINVAL;
    free_count = 0;
    reserved_count = 0;
    bad_count = 0;
    for (i = 0; i < allocator->vpa_page_count; ++i) {
        page = &allocator->vpa_pages[i];
        if (!vm_paddr_page_aligned(page->vmp_paddr) ||
            !vm_page_state_valid((enum vm_page_state)page->vmp_state) ||
            (i != 0 && allocator->vpa_pages[i - 1].vmp_paddr >=
            page->vmp_paddr) || !vm_page_matches_map(page, map))
            return EINVAL;
        if (page->vmp_state == VM_PAGE_FREE) {
            if (!vm_page_counts_zero(page))
                return EINVAL;
            ++free_count;
        } else if (page->vmp_state == VM_PAGE_RESERVED) {
            if (!vm_page_counts_zero(page) || page->vmp_flags != 0)
                return EINVAL;
            ++reserved_count;
        } else if (page->vmp_state == VM_PAGE_BAD) {
            if (!vm_page_counts_zero(page) || page->vmp_flags != 0)
                return EINVAL;
            ++bad_count;
        } else if (page->vmp_state == VM_PAGE_WIRED &&
            page->vmp_wire_count == 0) {
            return EINVAL;
        } else if (page->vmp_state == VM_PAGE_BUSY &&
            page->vmp_busy_count == 0) {
            return EINVAL;
        }
    }
    if (free_count != allocator->vpa_free_count ||
        reserved_count != allocator->vpa_reserved_count ||
        bad_count != allocator->vpa_bad_count)
        return EINVAL;
    return 0;
}

int
vm_page_allocator_stats(const struct vm_page_allocator *allocator,
    struct vm_page_stats *stats)
{
    if (allocator == 0 || stats == 0 || !allocator->vpa_initialized)
        return EINVAL;
    stats->vps_total = allocator->vpa_page_count;
    stats->vps_free = allocator->vpa_free_count;
    stats->vps_reserved = allocator->vpa_reserved_count;
    stats->vps_bad = allocator->vpa_bad_count;
    stats->vps_allocations = allocator->vpa_allocations;
    stats->vps_frees = allocator->vpa_frees;
    stats->vps_allocation_failures = allocator->vpa_allocation_failures;
    stats->vps_poison_failures = allocator->vpa_poison_failures;
    return 0;
}

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
struct vm_page_allocator vm_page_boot_allocator;

extern void *vm_page_md_direct_map(vm_paddr_t, vm_size_t);
extern int vm_page_md_poison(void *, vm_paddr_t, uint8_t, int);

int
vm_page_bootstrap_init(const struct vm_phys_map *map,
    vm_paddr_t metadata_start, vm_size_t metadata_size)
{
    void *storage;
    int error;

    storage = vm_page_md_direct_map(metadata_start, metadata_size);
    if (storage == 0)
        return EFAULT;
    error = vm_page_allocator_init(&vm_page_boot_allocator, map, storage,
        metadata_size);
    if (error != 0)
        return error;
    vm_page_allocator_set_poison(&vm_page_boot_allocator,
        vm_page_md_poison, 0);
    return 0;
}

int
vm_page_bootstrap_selftest(void)
{
    struct vm_page_request request;
    struct vm_page_stats before;
    struct vm_page_stats after;
    struct vm_page *page;
    vm_paddr_t first_paddr;
    int error;

    error = vm_page_allocator_stats(&vm_page_boot_allocator, &before);
    if (error != 0 || before.vps_free < 3)
        return ENOMEM;
    vm_page_request_init(&request);
    error = vm_page_alloc(&vm_page_boot_allocator, &request, &page);
    if (error != 0)
        return error;
    first_paddr = page->vmp_paddr;
    error = vm_page_free(&vm_page_boot_allocator, page, 1);
    if (error != 0)
        return error;
    request.vpr_max_address = first_paddr + VM_PAGE_MASK;
    error = vm_page_alloc(&vm_page_boot_allocator, &request, &page);
    if (error != 0 || page->vmp_paddr != first_paddr)
        return EFAULT;
    error = vm_page_free(&vm_page_boot_allocator, page, 1);
    if (error != 0)
        return error;

    vm_page_request_init(&request);
    request.vpr_npages = 2;
    request.vpr_alignment = 2u * VM_PAGE_SIZE;
    request.vpr_boundary = 16u * VM_PAGE_SIZE;
    error = vm_page_alloc(&vm_page_boot_allocator, &request, &page);
    if (error != 0)
        return error;
    error = vm_page_free(&vm_page_boot_allocator, page, 2);
    if (error != 0)
        return error;
    error = vm_page_allocator_validate(&vm_page_boot_allocator,
        &vm_phys_boot_map);
    if (error != 0)
        return error;
    error = vm_page_allocator_stats(&vm_page_boot_allocator, &after);
    if (error != 0 || after.vps_free != before.vps_free)
        return EINVAL;
    return 0;
}

void
vm_page_bootstrap_summary(void)
{
    struct vm_page_stats stats;

    if (vm_page_allocator_stats(&vm_page_boot_allocator, &stats) != 0)
        return;
    printf("vm page: %u total, %u free, %u reserved, %u bad\n",
        stats.vps_total, stats.vps_free, stats.vps_reserved,
        stats.vps_bad);
}

int
vm_page_bootstrap_stats(struct vm_page_stats *stats)
{
    return vm_page_allocator_stats(&vm_page_boot_allocator, stats);
}
#endif
