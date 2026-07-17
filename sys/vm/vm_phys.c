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
#include <vm/vm_phys.h>
#include <vm/vm_page.h>

static int
vm_phys_name_valid(const char *name)
{
    return name != 0 && name[0] != '\0';
}

static int
vm_phys_name_equal(const char *left, const char *right)
{
    if (left == right)
        return 1;
    if (left == 0 || right == 0)
        return 0;
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static int
vm_phys_kind_valid(enum vm_phys_region_kind kind)
{
    return kind == VM_PHYS_AVAILABLE || kind == VM_PHYS_RESERVED;
}

static int
vm_phys_range_end(vm_paddr_t start, vm_size_t size, vm_paddr_t *end)
{
    if (size == 0 || !vm_paddr_page_aligned(start) ||
        !vm_size_page_aligned(size))
        return EINVAL;
    return vm_paddr_add(start, size, end);
}

static int
vm_phys_insert(struct vm_phys_map *map, unsigned index,
    const struct vm_phys_region *region)
{
    unsigned i;

    if (map->vpm_count >= VM_PHYS_MAX_REGIONS || index > map->vpm_count)
        return ENOSPC;
    for (i = map->vpm_count; i > index; --i)
        map->vpm_regions[i] = map->vpm_regions[i - 1];
    map->vpm_regions[index] = *region;
    ++map->vpm_count;
    return 0;
}

static void
vm_phys_remove(struct vm_phys_map *map, unsigned index)
{
    unsigned i;

    for (i = index; i + 1 < map->vpm_count; ++i)
        map->vpm_regions[i] = map->vpm_regions[i + 1];
    --map->vpm_count;
}

static void
vm_phys_coalesce(struct vm_phys_map *map)
{
    struct vm_phys_region *left;
    struct vm_phys_region *right;
    unsigned i;

    i = 0;
    while (i + 1 < map->vpm_count) {
        left = &map->vpm_regions[i];
        right = &map->vpm_regions[i + 1];
        if (left->vpr_end == right->vpr_start &&
            left->vpr_kind == right->vpr_kind &&
            vm_phys_name_equal(left->vpr_name, right->vpr_name)) {
            left->vpr_end = right->vpr_end;
            vm_phys_remove(map, i + 1);
        } else {
            ++i;
        }
    }
}

void
vm_phys_map_init(struct vm_phys_map *map)
{
    unsigned i;

    if (map == 0)
        return;
    map->vpm_count = 0;
    map->vpm_finalized = 0;
    for (i = 0; i < VM_PHYS_MAX_REGIONS; ++i) {
        map->vpm_regions[i].vpr_start = 0;
        map->vpm_regions[i].vpr_end = 0;
        map->vpm_regions[i].vpr_kind = 0;
        map->vpm_regions[i].vpr_name = 0;
    }
}

int
vm_phys_map_add_ram(struct vm_phys_map *map, vm_paddr_t start,
    vm_size_t size, const char *name)
{
    struct vm_phys_region region;
    unsigned pos;
    int error;

    if (map == 0 || map->vpm_finalized || !vm_phys_name_valid(name))
        return EINVAL;
    error = vm_phys_range_end(start, size, &region.vpr_end);
    if (error != 0)
        return error;
    region.vpr_start = start;
    region.vpr_kind = VM_PHYS_AVAILABLE;
    region.vpr_name = name;

    pos = 0;
    while (pos < map->vpm_count &&
        map->vpm_regions[pos].vpr_start < start)
        ++pos;
    if (pos != 0 && map->vpm_regions[pos - 1].vpr_end > start)
        return EBUSY;
    if (pos < map->vpm_count &&
        region.vpr_end > map->vpm_regions[pos].vpr_start)
        return EBUSY;
    error = vm_phys_insert(map, pos, &region);
    if (error != 0)
        return error;
    vm_phys_coalesce(map);
    return 0;
}

int
vm_phys_map_reserve(struct vm_phys_map *map, vm_paddr_t start,
    vm_size_t size, const char *name)
{
    struct vm_phys_region old;
    struct vm_phys_region region;
    struct vm_phys_region tail;
    vm_paddr_t end;
    unsigned i;
    unsigned needed;
    int error;

    if (map == 0 || map->vpm_finalized || !vm_phys_name_valid(name))
        return EINVAL;
    error = vm_phys_range_end(start, size, &end);
    if (error != 0)
        return error;

    for (i = 0; i < map->vpm_count; ++i) {
        old = map->vpm_regions[i];
        if (end <= old.vpr_start || start >= old.vpr_end)
            continue;
        if (old.vpr_kind == VM_PHYS_RESERVED)
            return EBUSY;
        if (start >= old.vpr_start && end <= old.vpr_end)
            break;
    }
    if (i == map->vpm_count)
        return ENOENT;

    needed = 0;
    if (start != old.vpr_start)
        ++needed;
    if (end != old.vpr_end)
        ++needed;
    if (map->vpm_count + needed > VM_PHYS_MAX_REGIONS)
        return ENOSPC;

    region.vpr_start = start;
    region.vpr_end = end;
    region.vpr_kind = VM_PHYS_RESERVED;
    region.vpr_name = name;

    if (start == old.vpr_start && end == old.vpr_end) {
        map->vpm_regions[i] = region;
    } else if (start == old.vpr_start) {
        tail = old;
        tail.vpr_start = end;
        map->vpm_regions[i] = region;
        error = vm_phys_insert(map, i + 1, &tail);
        if (error != 0)
            return error;
    } else if (end == old.vpr_end) {
        map->vpm_regions[i].vpr_end = start;
        error = vm_phys_insert(map, i + 1, &region);
        if (error != 0)
            return error;
    } else {
        tail = old;
        tail.vpr_start = end;
        map->vpm_regions[i].vpr_end = start;
        error = vm_phys_insert(map, i + 1, &region);
        if (error != 0)
            return error;
        error = vm_phys_insert(map, i + 2, &tail);
        if (error != 0)
            return error;
    }
    vm_phys_coalesce(map);
    return 0;
}

int
vm_phys_map_clip(const struct vm_phys_map *map, vm_paddr_t start,
    vm_size_t size, vm_paddr_t *clipped_start, vm_size_t *clipped_size)
{
    const struct vm_phys_region *region;
    vm_paddr_t end;
    vm_paddr_t first;
    vm_paddr_t last;
    vm_paddr_t region_end;
    unsigned i;
    int found;
    int error;

    if (map == 0 || clipped_start == 0 || clipped_size == 0)
        return EINVAL;
    error = vm_phys_range_end(start, size, &end);
    if (error != 0)
        return error;

    first = 0;
    last = 0;
    found = 0;
    for (i = 0; i < map->vpm_count; ++i) {
        region = &map->vpm_regions[i];
        if (region->vpr_end <= start)
            continue;
        if (region->vpr_start >= end)
            break;
        if (!found) {
            first = region->vpr_start > start ? region->vpr_start : start;
            last = region->vpr_end < end ? region->vpr_end : end;
            found = 1;
            continue;
        }
        if (region->vpr_start != last)
            break;
        region_end = region->vpr_end < end ? region->vpr_end : end;
        last = region_end;
    }
    if (!found)
        return ENOENT;
    *clipped_start = first;
    *clipped_size = last - first;
    return 0;
}

int
vm_phys_map_validate(const struct vm_phys_map *map)
{
    const struct vm_phys_region *region;
    vm_size_t available;
    vm_size_t reserved;
    vm_size_t length;
    unsigned i;
    int error;

    if (map == 0 || map->vpm_count == 0 ||
        map->vpm_count > VM_PHYS_MAX_REGIONS)
        return EINVAL;
    available = 0;
    reserved = 0;
    for (i = 0; i < map->vpm_count; ++i) {
        region = &map->vpm_regions[i];
        if (!vm_phys_kind_valid(region->vpr_kind) ||
            !vm_phys_name_valid(region->vpr_name) ||
            !vm_paddr_page_aligned(region->vpr_start) ||
            !vm_paddr_page_aligned(region->vpr_end) ||
            region->vpr_start >= region->vpr_end)
            return EINVAL;
        if (i != 0 &&
            map->vpm_regions[i - 1].vpr_end > region->vpr_start)
            return EINVAL;
        length = region->vpr_end - region->vpr_start;
        if (region->vpr_kind == VM_PHYS_AVAILABLE)
            error = vm_size_add(available, length, &available);
        else
            error = vm_size_add(reserved, length, &reserved);
        if (error != 0)
            return error;
    }
    return vm_size_add(available, reserved, &length);
}

int
vm_phys_map_finalize(struct vm_phys_map *map)
{
    int error;

    if (map == 0 || map->vpm_finalized)
        return EINVAL;
    vm_phys_coalesce(map);
    error = vm_phys_map_validate(map);
    if (error != 0)
        return error;
    map->vpm_finalized = 1;
    return 0;
}

const struct vm_phys_region *
vm_phys_map_region(const struct vm_phys_map *map, unsigned index)
{
    if (map == 0 || index >= map->vpm_count)
        return 0;
    return &map->vpm_regions[index];
}

unsigned
vm_phys_map_count(const struct vm_phys_map *map)
{
    return map == 0 ? 0 : map->vpm_count;
}

int
vm_phys_map_total(const struct vm_phys_map *map,
    enum vm_phys_region_kind kind, vm_size_t *total)
{
    const struct vm_phys_region *region;
    vm_size_t value;
    vm_size_t length;
    unsigned i;
    int error;

    if (map == 0 || total == 0 || !vm_phys_kind_valid(kind))
        return EINVAL;
    value = 0;
    for (i = 0; i < map->vpm_count; ++i) {
        region = &map->vpm_regions[i];
        if (region->vpr_kind != kind)
            continue;
        length = region->vpr_end - region->vpr_start;
        error = vm_size_add(value, length, &value);
        if (error != 0)
            return error;
    }
    *total = value;
    return 0;
}

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
struct vm_phys_map vm_phys_boot_map;

int
vm_phys_bootstrap(vm_size_t ram_size)
{
    vm_paddr_t metadata_start;
    vm_size_t metadata_size;
    vm_size_t available;
    vm_size_t reserved;
    vm_size_t total;
    vm_pfn_t metadata_entries;
    int error;

    if (ram_size == 0 || !vm_size_page_aligned(ram_size))
        return EINVAL;
    vm_phys_map_init(&vm_phys_boot_map);
    error = vm_phys_board_register(&vm_phys_boot_map, ram_size);
    if (error != 0)
        return error;
    error = vm_page_metadata_reserve(&vm_phys_boot_map, &metadata_start,
        &metadata_size, &metadata_entries);
    if (error != 0 || metadata_entries == 0)
        return error != 0 ? error : EINVAL;
    error = vm_phys_map_finalize(&vm_phys_boot_map);
    if (error != 0)
        return error;
    error = vm_phys_map_total(&vm_phys_boot_map, VM_PHYS_AVAILABLE,
        &available);
    if (error != 0)
        return error;
    error = vm_phys_map_total(&vm_phys_boot_map, VM_PHYS_RESERVED,
        &reserved);
    if (error != 0)
        return error;
    error = vm_size_add(available, reserved, &total);
    if (error != 0)
        return error;
    if (total != ram_size)
        return EINVAL;
    return vm_page_bootstrap_init(&vm_phys_boot_map, metadata_start,
        metadata_size);
}

void
vm_phys_bootstrap_summary(void)
{
    vm_size_t available;
    vm_size_t reserved;

    if (!vm_phys_boot_map.vpm_finalized)
        return;
    if (vm_phys_map_total(&vm_phys_boot_map, VM_PHYS_AVAILABLE,
        &available) != 0 ||
        vm_phys_map_total(&vm_phys_boot_map, VM_PHYS_RESERVED,
        &reserved) != 0)
        return;
    printf("vm phys: %uK available, %uK reserved, %u regions\n",
        available >> 10, reserved >> 10, vm_phys_boot_map.vpm_count);
}
#endif
