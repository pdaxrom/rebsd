/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#define vm_map_zero(p, n) bzero((caddr_t)(p), (unsigned)(n))
#else
#include <errno.h>
#include <string.h>
#define vm_map_zero(p, n) memset((p), 0, (n))
#endif

#include <vm/vm_map.h>
#include <vm/vm_param.h>

static int
vm_map_protection_valid(vm_prot_t protection)
{
    return (protection & ~VM_PROT_ALL) == 0;
}

static void vm_map_delete_entry(struct vm_map *, unsigned);

static int
vm_map_entry_matches(const struct vm_map_entry *entry,
    vm_prot_t protection, vm_prot_t maximum, unsigned flags)
{
    return entry->vme_protection == protection &&
        entry->vme_max_protection == maximum &&
        entry->vme_flags == flags;
}

int
vm_map_init(struct vm_map *map, vm_vaddr_t minimum, vm_vaddr_t maximum)
{
    if (map == 0 || !vm_vaddr_page_aligned(minimum) ||
        !vm_vaddr_page_aligned(maximum) || minimum >= maximum)
        return EINVAL;
    vm_map_zero(map, sizeof(*map));
    map->vmm_min = minimum;
    map->vmm_max = maximum;
    return 0;
}

int
vm_map_insert(struct vm_map *map, vm_vaddr_t start, vm_vaddr_t end,
    vm_prot_t protection, vm_prot_t maximum, unsigned flags)
{
    unsigned index;
    unsigned move;

    if (map == 0 || !vm_vaddr_page_aligned(start) ||
        !vm_vaddr_page_aligned(end) || start >= end ||
        start < map->vmm_min || end > map->vmm_max ||
        !vm_map_protection_valid(protection) ||
        !vm_map_protection_valid(maximum) ||
        (protection & ~maximum) != 0)
        return EINVAL;
    if (map->vmm_count >= VM_MAP_MAX_ENTRIES)
        return ENOSPC;
    for (index = 0; index < map->vmm_count; ++index) {
        if (end <= map->vmm_entries[index].vme_start)
            break;
        if (start < map->vmm_entries[index].vme_end)
            return EEXIST;
    }
    if (index != 0 && map->vmm_entries[index - 1].vme_end == start &&
        vm_map_entry_matches(&map->vmm_entries[index - 1], protection,
        maximum, flags)) {
        map->vmm_entries[index - 1].vme_end = end;
        if (index < map->vmm_count &&
            map->vmm_entries[index].vme_start == end &&
            vm_map_entry_matches(&map->vmm_entries[index], protection,
            maximum, flags)) {
            map->vmm_entries[index - 1].vme_end =
                map->vmm_entries[index].vme_end;
            vm_map_delete_entry(map, index);
        }
        return 0;
    }
    if (index < map->vmm_count &&
        map->vmm_entries[index].vme_start == end &&
        vm_map_entry_matches(&map->vmm_entries[index], protection,
        maximum, flags)) {
        map->vmm_entries[index].vme_start = start;
        return 0;
    }
    for (move = map->vmm_count; move > index; --move)
        map->vmm_entries[move] = map->vmm_entries[move - 1];
    map->vmm_entries[index].vme_start = start;
    map->vmm_entries[index].vme_end = end;
    map->vmm_entries[index].vme_protection = protection;
    map->vmm_entries[index].vme_max_protection = maximum;
    map->vmm_entries[index].vme_flags = flags;
    ++map->vmm_count;
    return 0;
}

static void
vm_map_delete_entry(struct vm_map *map, unsigned index)
{
    for (; index + 1 < map->vmm_count; ++index)
        map->vmm_entries[index] = map->vmm_entries[index + 1];
    --map->vmm_count;
    vm_map_zero(&map->vmm_entries[map->vmm_count],
        sizeof(map->vmm_entries[0]));
}

int
vm_map_remove(struct vm_map *map, vm_vaddr_t start, vm_vaddr_t end)
{
    struct vm_map_entry old;
    unsigned index;

    if (map == 0 || !vm_vaddr_page_aligned(start) ||
        !vm_vaddr_page_aligned(end) || start >= end ||
        start < map->vmm_min || end > map->vmm_max)
        return EINVAL;
    index = 0;
    while (index < map->vmm_count) {
        old = map->vmm_entries[index];
        if (old.vme_end <= start) {
            ++index;
            continue;
        }
        if (old.vme_start >= end)
            break;
        if (start <= old.vme_start && end >= old.vme_end) {
            vm_map_delete_entry(map, index);
            continue;
        }
        if (start <= old.vme_start) {
            map->vmm_entries[index].vme_start = end;
            break;
        }
        if (end >= old.vme_end) {
            map->vmm_entries[index].vme_end = start;
            ++index;
            continue;
        }
        if (map->vmm_count >= VM_MAP_MAX_ENTRIES)
            return ENOSPC;
        map->vmm_entries[index].vme_end = start;
        if (vm_map_insert(map, end, old.vme_end,
            old.vme_protection, old.vme_max_protection,
            old.vme_flags) != 0)
            return EFAULT;
        break;
    }
    return 0;
}

int
vm_map_protect(struct vm_map *map, vm_vaddr_t start, vm_vaddr_t end,
    vm_prot_t protection)
{
    struct vm_map replacement;
    const struct vm_map_entry *old;
    vm_vaddr_t middle_start;
    vm_vaddr_t middle_end;
    unsigned index;
    int error;

    if (map == 0 || !vm_vaddr_page_aligned(start) ||
        !vm_vaddr_page_aligned(end) || start >= end ||
        !vm_map_protection_valid(protection) ||
        vm_map_check(map, start, end - start, VM_PROT_NONE) != 0)
        return EINVAL;
    error = vm_map_init(&replacement, map->vmm_min, map->vmm_max);
    if (error != 0)
        return error;
    for (index = 0; index < map->vmm_count; ++index) {
        old = &map->vmm_entries[index];
        if (old->vme_end <= start || old->vme_start >= end) {
            error = vm_map_insert(&replacement, old->vme_start,
                old->vme_end, old->vme_protection,
                old->vme_max_protection, old->vme_flags);
            if (error != 0)
                return error;
            continue;
        }
        if ((protection & ~old->vme_max_protection) != 0)
            return EACCES;
        if (old->vme_start < start) {
            error = vm_map_insert(&replacement, old->vme_start, start,
                old->vme_protection, old->vme_max_protection,
                old->vme_flags);
            if (error != 0)
                return error;
        }
        middle_start = old->vme_start > start ? old->vme_start : start;
        middle_end = old->vme_end < end ? old->vme_end : end;
        error = vm_map_insert(&replacement, middle_start, middle_end,
            protection, old->vme_max_protection, old->vme_flags);
        if (error != 0)
            return error;
        if (old->vme_end > end) {
            error = vm_map_insert(&replacement, end, old->vme_end,
                old->vme_protection, old->vme_max_protection,
                old->vme_flags);
            if (error != 0)
                return error;
        }
    }
    *map = replacement;
    return 0;
}

const struct vm_map_entry *
vm_map_lookup(const struct vm_map *map, vm_vaddr_t address)
{
    unsigned index;

    if (map == 0 || address < map->vmm_min || address >= map->vmm_max)
        return 0;
    for (index = 0; index < map->vmm_count; ++index) {
        if (address < map->vmm_entries[index].vme_start)
            break;
        if (address < map->vmm_entries[index].vme_end)
            return &map->vmm_entries[index];
    }
    return 0;
}

int
vm_map_check(const struct vm_map *map, vm_vaddr_t start, vm_size_t size,
    vm_prot_t protection)
{
    const struct vm_map_entry *entry;
    vm_vaddr_t address;
    vm_vaddr_t end;

    if (map == 0 || size == 0 || !vm_map_protection_valid(protection) ||
        start < map->vmm_min || size - 1 > VM_VADDR_MAX - start)
        return EINVAL;
    end = start + size;
    if (end > map->vmm_max)
        return EFAULT;
    address = start;
    while (address < end) {
        entry = vm_map_lookup(map, address);
        if (entry == 0 ||
            (entry->vme_protection & protection) != protection)
            return EFAULT;
        address = entry->vme_end < end ? entry->vme_end : end;
    }
    return 0;
}

int
vm_map_validate(const struct vm_map *map)
{
    const struct vm_map_entry *entry;
    vm_vaddr_t previous;
    unsigned index;

    if (map == 0 || !vm_vaddr_page_aligned(map->vmm_min) ||
        !vm_vaddr_page_aligned(map->vmm_max) ||
        map->vmm_min >= map->vmm_max ||
        map->vmm_count > VM_MAP_MAX_ENTRIES)
        return EINVAL;
    previous = map->vmm_min;
    for (index = 0; index < map->vmm_count; ++index) {
        entry = &map->vmm_entries[index];
        if (!vm_vaddr_page_aligned(entry->vme_start) ||
            !vm_vaddr_page_aligned(entry->vme_end) ||
            entry->vme_start < previous || entry->vme_start >= entry->vme_end ||
            entry->vme_end > map->vmm_max ||
            !vm_map_protection_valid(entry->vme_protection) ||
            !vm_map_protection_valid(entry->vme_max_protection) ||
            (entry->vme_protection & ~entry->vme_max_protection) != 0)
            return EINVAL;
        previous = entry->vme_end;
    }
    return 0;
}
