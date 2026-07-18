/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/map.h>
#include <sys/systm.h>
#define VM_OBJECT_MAX          (NPROC * VM_MAP_MAX_ENTRIES)
#define vm_object_zero(p, n)   bzero((caddr_t)(p), (unsigned)(n))
#define vm_object_copy(s, d, n) bcopy((s), (d), (unsigned)(n))
#define VM_PAGER_DEV_BSIZE     DEV_BSIZE
#else
#include <errno.h>
#include <string.h>
#define VM_OBJECT_MAX          128
#define vm_object_zero(p, n)   memset((p), 0, (n))
#define vm_object_copy(s, d, n) memcpy((d), (s), (n))
#define VM_PAGER_DEV_BSIZE     1024u
#endif

#include <vm/vm_object.h>

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
extern int swap(size_t, size_t, int, int);
#endif

#define VM_ANON_MAX            2048
#define VM_ANON_BUSY           0x01u
#define VM_ANON_DIRTY          0x02u
#define VM_SWAP_BLOCKS         (VM_PAGE_SIZE / VM_PAGER_DEV_BSIZE)
#define VM_PAGER_FREE_MIN      8u
#define VM_PAGER_FREE_TARGET   16u
#if !defined(KERNEL) || defined(REBSD_VM_HOST_TEST)
#define VM_PAGER_HOST_SWAP_MAX 64u
#endif

struct vm_anon {
    struct vm_page *va_page;
    const struct vm_object_pager_ops *va_pager;
    void           *va_pager_cookie;
    vm_ooffset_t    va_pager_offset;
    size_t          va_swap_slot;
    uint16_t        va_references;
    uint8_t         va_flags;
    uint8_t         va_in_use;
};

struct vm_object_page {
    struct vm_object_page *vop_next;
    struct vm_anon        *vop_anon;
    vm_pfn_t               vop_index;
    unsigned               vop_in_use;
};

struct vm_object {
    struct vm_object_page *vo_pages;
    const struct vm_object_pager_ops *vo_pager;
    void                  *vo_pager_cookie;
    vm_ooffset_t           vo_pager_offset;
    vm_pfn_t               vo_size;
    uint16_t               vo_references;
    uint16_t               vo_in_use;
};

static struct vm_object vm_object_pool[VM_OBJECT_MAX];
static struct vm_object_page vm_object_page_pool[VM_ANON_MAX];
static struct vm_anon vm_anon_pool[VM_ANON_MAX];
static struct vm_page_allocator *vm_object_allocator;
static struct vm_object_stats vm_object_statistics;
static unsigned vm_object_initialized;
static unsigned vm_pager_clock;
static unsigned vm_pager_swap_ready;
#if !defined(KERNEL) || defined(REBSD_VM_HOST_TEST)
static unsigned char vm_pager_host_swap[
    VM_PAGER_HOST_SWAP_MAX * VM_PAGE_SIZE];
static unsigned char vm_pager_host_swap_used[VM_PAGER_HOST_SWAP_MAX];
static unsigned vm_pager_host_swap_pages;
static int vm_pager_host_fail_read;
static int vm_pager_host_fail_write;
#endif

static void
vm_object_stat_increment(vm_pfn_t *value)
{
    if (*value != VM_PFN_MAX)
        ++*value;
}

static void
vm_object_stat_decrement(vm_pfn_t *value)
{
    if (*value != 0)
        --*value;
}

static int
vm_object_valid(const struct vm_object *object)
{
    return object != 0 && object >= &vm_object_pool[0] &&
        object < &vm_object_pool[VM_OBJECT_MAX] && object->vo_in_use != 0;
}

static struct vm_object_page *
vm_object_page_alloc(void)
{
    unsigned index;

    for (index = 0; index < VM_ANON_MAX; ++index) {
        if (vm_object_page_pool[index].vop_in_use == 0) {
            vm_object_zero(&vm_object_page_pool[index],
                sizeof(vm_object_page_pool[index]));
            vm_object_page_pool[index].vop_in_use = 1;
            return &vm_object_page_pool[index];
        }
    }
    return 0;
}

static void
vm_object_page_free(struct vm_object_page *page)
{
    if (page != 0)
        vm_object_zero(page, sizeof(*page));
}

static struct vm_anon *
vm_anon_alloc(void)
{
    unsigned index;

    for (index = 0; index < VM_ANON_MAX; ++index) {
        if (vm_anon_pool[index].va_in_use == 0) {
            vm_object_zero(&vm_anon_pool[index],
                sizeof(vm_anon_pool[index]));
            vm_anon_pool[index].va_in_use = 1;
            vm_anon_pool[index].va_references = 1;
            vm_object_stat_increment(&vm_object_statistics.vos_anon_pages);
            return &vm_anon_pool[index];
        }
    }
    return 0;
}

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
static size_t
vm_pager_swap_alloc(void)
{
    if (!vm_pager_swap_ready)
        return 0;
    return malloc(swapmap, VM_SWAP_BLOCKS);
}

static void
vm_pager_swap_free(size_t slot)
{
    if (slot != 0)
        mfree(swapmap, VM_SWAP_BLOCKS, slot);
}

static int
vm_pager_swap_io(size_t slot, struct vm_page *page, int read)
{
    void *mapping;

    mapping = pmap_page_direct_map(page, PMAP_CACHE_CACHED);
    if (mapping == 0)
        return EFAULT;
    return swap(slot, (size_t)mapping, VM_PAGE_SIZE,
        read ? B_READ : 0);
}
#else
static size_t
vm_pager_swap_alloc(void)
{
    unsigned index;

    if (!vm_pager_swap_ready)
        return 0;
    for (index = 0; index < vm_pager_host_swap_pages; ++index) {
        if (vm_pager_host_swap_used[index] == 0) {
            vm_pager_host_swap_used[index] = 1;
            return (index + 1) * VM_SWAP_BLOCKS;
        }
    }
    return 0;
}

static void
vm_pager_swap_free(size_t slot)
{
    unsigned index;

    if (slot == 0 || slot % VM_SWAP_BLOCKS != 0)
        return;
    index = (unsigned)(slot / VM_SWAP_BLOCKS - 1);
    if (index < VM_PAGER_HOST_SWAP_MAX)
        vm_pager_host_swap_used[index] = 0;
}

static int
vm_pager_swap_io(size_t slot, struct vm_page *page, int read)
{
    void *mapping;
    unsigned index;

    if (slot == 0 || slot % VM_SWAP_BLOCKS != 0)
        return EINVAL;
    index = (unsigned)(slot / VM_SWAP_BLOCKS - 1);
    if (index >= vm_pager_host_swap_pages ||
        vm_pager_host_swap_used[index] == 0)
        return EINVAL;
    if ((read && vm_pager_host_fail_read) ||
        (!read && vm_pager_host_fail_write))
        return EIO;
    mapping = pmap_page_direct_map(page, PMAP_CACHE_CACHED);
    if (mapping == 0)
        return EFAULT;
    if (read)
        vm_object_copy(&vm_pager_host_swap[index * VM_PAGE_SIZE],
            mapping, VM_PAGE_SIZE);
    else
        vm_object_copy(mapping,
            &vm_pager_host_swap[index * VM_PAGE_SIZE], VM_PAGE_SIZE);
    return 0;
}
#endif

static int
vm_anon_release(struct vm_anon *anon)
{
    int error;

    if (anon == 0 || anon->va_in_use == 0 || anon->va_references == 0)
        return EINVAL;
    if (anon->va_references != 1) {
        --anon->va_references;
        return 0;
    }
    if (anon->va_page != 0) {
        if (anon->va_page->vmp_hold_count != 0 ||
            anon->va_page->vmp_reference_count != 0 ||
            anon->va_page->vmp_dirty_count != 0)
            return EBUSY;
        error = vm_page_free(vm_object_allocator, anon->va_page, 1);
        if (error != 0)
            return error;
        vm_object_stat_decrement(
            &vm_object_statistics.vos_resident_pages);
    }
    if (anon->va_swap_slot != 0) {
        vm_pager_swap_free(anon->va_swap_slot);
        vm_object_stat_decrement(&vm_object_statistics.vos_swapped_pages);
    }
    vm_object_zero(anon, sizeof(*anon));
    vm_object_stat_decrement(&vm_object_statistics.vos_anon_pages);
    return 0;
}

static int
vm_anon_discard_resident(struct vm_anon *anon)
{
    struct vm_page *page;
    int error;

    if (anon == 0 || anon->va_in_use == 0)
        return EINVAL;
    page = anon->va_page;
    if (page == 0)
        return 0;
    if (page->vmp_hold_count != 0 || page->vmp_wire_count != 0 ||
        page->vmp_busy_count != 0 || page->vmp_reference_count != 0 ||
        page->vmp_dirty_count != 0)
        return EBUSY;
    error = vm_page_free(vm_object_allocator, page, 1);
    if (error != 0)
        return error;
    anon->va_page = 0;
    vm_object_stat_decrement(&vm_object_statistics.vos_resident_pages);
    return 0;
}

static struct vm_object_page *
vm_object_page_find(const struct vm_object *object, vm_pfn_t index)
{
    struct vm_object_page *page;

    for (page = object->vo_pages; page != 0; page = page->vop_next) {
        if (page->vop_index == index)
            return page;
    }
    return 0;
}

static int vm_object_range_valid(const struct vm_object *, vm_ooffset_t,
    vm_size_t);

static int vm_pager_reclaim_one(struct vm_anon *);

static int
vm_object_page_allocate(struct vm_page **result)
{
    struct vm_page_request request;
    int error;
    int pass;

    vm_page_request_init(&request);
    request.vpr_state = VM_PAGE_ACTIVE;
    error = vm_page_alloc(vm_object_allocator, &request, result);
    for (pass = 0; error == ENOMEM && pass < 3; ++pass) {
        (void)vm_pager_reclaim_one(0);
        error = vm_page_alloc(vm_object_allocator, &request, result);
    }
    return error;
}

static int
vm_anon_make_resident(struct vm_anon *anon, int zero_fault)
{
    struct vm_page *page;
    void *mapping;
    int error;

    if (anon->va_page != 0)
        return 0;
    anon->va_flags |= VM_ANON_BUSY;
    error = vm_object_page_allocate(&page);
    if (error != 0) {
        anon->va_flags &= ~VM_ANON_BUSY;
        return error;
    }
    mapping = pmap_page_direct_map(page, PMAP_CACHE_CACHED);
    if (mapping == 0) {
        (void)vm_page_free(vm_object_allocator, page, 1);
        anon->va_flags &= ~VM_ANON_BUSY;
        return EFAULT;
    }
    if (anon->va_swap_slot == 0) {
        vm_object_zero(mapping, VM_PAGE_SIZE);
        if (zero_fault)
            vm_object_stat_increment(&vm_object_statistics.vos_zero_faults);
    } else {
        error = vm_pager_swap_io(anon->va_swap_slot, page, 1);
        if (error != 0) {
            (void)vm_page_free(vm_object_allocator, page, 1);
            anon->va_flags &= ~VM_ANON_BUSY;
            vm_object_stat_increment(
                &vm_object_statistics.vos_swap_failures);
            return error;
        }
        vm_object_stat_increment(&vm_object_statistics.vos_pageins);
    }
    anon->va_page = page;
    anon->va_flags &= ~VM_ANON_BUSY;
    vm_object_stat_increment(&vm_object_statistics.vos_resident_pages);
    return 0;
}

int
vm_object_system_init(struct vm_page_allocator *allocator)
{
    if (allocator == 0 || allocator->vpa_initialized == 0)
        return EINVAL;
    vm_object_zero(vm_object_pool, sizeof(vm_object_pool));
    vm_object_zero(vm_object_page_pool, sizeof(vm_object_page_pool));
    vm_object_zero(vm_anon_pool, sizeof(vm_anon_pool));
    vm_object_zero(&vm_object_statistics, sizeof(vm_object_statistics));
    vm_object_allocator = allocator;
    vm_pager_clock = 0;
    vm_pager_swap_ready = 0;
#if !defined(KERNEL) || defined(REBSD_VM_HOST_TEST)
    vm_object_zero(vm_pager_host_swap_used,
        sizeof(vm_pager_host_swap_used));
    vm_pager_host_swap_pages = 0;
    vm_pager_host_fail_read = 0;
    vm_pager_host_fail_write = 0;
#endif
    vm_object_initialized = 1;
    return 0;
}

int
vm_object_create(vm_size_t size, struct vm_object **result)
{
    struct vm_object *object;
    vm_pfn_t pages;
    unsigned index;
    int error;

    if (!vm_object_initialized || result == 0)
        return EINVAL;
    error = vm_size_to_pages(size, &pages);
    if (error != 0)
        return EINVAL;
    *result = 0;
    for (index = 0; index < VM_OBJECT_MAX; ++index) {
        if (vm_object_pool[index].vo_in_use == 0) {
            object = &vm_object_pool[index];
            vm_object_zero(object, sizeof(*object));
            object->vo_size = pages;
            object->vo_references = 1;
            object->vo_in_use = 1;
            vm_object_stat_increment(&vm_object_statistics.vos_objects);
            *result = object;
            return 0;
        }
    }
    return ENOSPC;
}

int
vm_object_resize(struct vm_object *object, vm_size_t size)
{
    vm_size_t old_size;
    vm_pfn_t pages;
    int error;

    if (!vm_object_valid(object) || !vm_size_page_aligned(size))
        return EINVAL;
    error = vm_size_to_pages(size, &pages);
    if (error != 0)
        return error;
    old_size = object->vo_size * VM_PAGE_SIZE;
    if (size < old_size) {
        error = vm_object_invalidate(object, size, old_size - size);
        if (error != 0)
            return error;
    }
    object->vo_size = pages;
    return 0;
}

int
vm_object_create_paged(vm_size_t size,
    const struct vm_object_pager_ops *pager, void *cookie,
    vm_ooffset_t offset, struct vm_object **result)
{
    struct vm_object *object;
    int error;

    if (pager == 0 || pager->vpo_reference == 0 ||
        pager->vpo_release == 0 || pager->vpo_pagein == 0 ||
        cookie == 0 || (offset & VM_PAGE_MASK) != 0 ||
        size == 0 || (vm_ooffset_t)size - 1 > UINT64_MAX - offset)
        return EINVAL;
    error = vm_object_create(size, &object);
    if (error != 0)
        return error;
    pager->vpo_reference(cookie);
    object->vo_pager = pager;
    object->vo_pager_cookie = cookie;
    object->vo_pager_offset = offset;
    *result = object;
    return 0;
}

int
vm_object_reference(struct vm_object *object)
{
    if (!vm_object_valid(object) || object->vo_references == UINT16_MAX)
        return EOVERFLOW;
    ++object->vo_references;
    return 0;
}

int
vm_object_is_shared(const struct vm_object *object)
{
    return vm_object_valid(object) && object->vo_references > 1;
}

int
vm_object_has_pageout(const struct vm_object *object)
{
    return vm_object_valid(object) && object->vo_pager != 0 &&
        object->vo_pager->vpo_pageout != 0;
}

int
vm_object_clone(const struct vm_object *source, struct vm_object **result)
{
    struct vm_object_page *source_page;
    struct vm_object_page *target_page;
    struct vm_object *target;
    int error;

    if (!vm_object_valid(source) || result == 0)
        return EINVAL;
    if (source->vo_pager != 0)
        error = vm_object_create_paged(source->vo_size * VM_PAGE_SIZE,
            source->vo_pager, source->vo_pager_cookie,
            source->vo_pager_offset, &target);
    else
        error = vm_object_create(source->vo_size * VM_PAGE_SIZE, &target);
    if (error != 0)
        return error;
    for (source_page = source->vo_pages; source_page != 0;
        source_page = source_page->vop_next) {
        target_page = vm_object_page_alloc();
        if (target_page == 0 ||
            source_page->vop_anon->va_references == UINT16_MAX) {
            if (target_page != 0)
                vm_object_page_free(target_page);
            (void)vm_object_release(target);
            return ENOSPC;
        }
        ++source_page->vop_anon->va_references;
        target_page->vop_index = source_page->vop_index;
        target_page->vop_anon = source_page->vop_anon;
        target_page->vop_next = target->vo_pages;
        target->vo_pages = target_page;
    }
    *result = target;
    return 0;
}

int
vm_object_release(struct vm_object *object)
{
    struct vm_object_page *page;
    struct vm_object_page *next;
    int error;

    if (!vm_object_valid(object) || object->vo_references == 0)
        return EINVAL;
    if (object->vo_references != 1) {
        --object->vo_references;
        return 0;
    }
    if (object->vo_pager != 0 && object->vo_pager->vpo_pageout != 0) {
        error = vm_object_sync(object, 0,
            object->vo_size * VM_PAGE_SIZE, VM_PAGER_IO_SYNC);
        if (error != 0)
            return error;
    }
    for (page = object->vo_pages; page != 0; page = next) {
        next = page->vop_next;
        error = vm_anon_release(page->vop_anon);
        if (error != 0)
            return error;
        vm_object_page_free(page);
    }
    if (object->vo_pager != 0)
        object->vo_pager->vpo_release(object->vo_pager_cookie);
    object->vo_references = 0;
    vm_object_zero(object, sizeof(*object));
    vm_object_stat_decrement(&vm_object_statistics.vos_objects);
    return 0;
}

static int
vm_object_private_copy(struct vm_object_page *object_page)
{
    struct vm_anon *source;
    struct vm_anon *target;
    struct vm_page *page;
    void *source_mapping;
    void *target_mapping;
    int error;

    source = object_page->vop_anon;
    if (source->va_references <= 1)
        return 0;
    source->va_flags |= VM_ANON_BUSY;
    error = vm_anon_make_resident(source, 0);
    if (error != 0) {
        source->va_flags &= ~VM_ANON_BUSY;
        return error;
    }
    target = vm_anon_alloc();
    if (target == 0) {
        source->va_flags &= ~VM_ANON_BUSY;
        return ENOSPC;
    }
    target->va_flags |= VM_ANON_BUSY;
    error = vm_object_page_allocate(&page);
    if (error != 0) {
        target->va_flags &= ~VM_ANON_BUSY;
        (void)vm_anon_release(target);
        source->va_flags &= ~VM_ANON_BUSY;
        return error;
    }
    source_mapping = pmap_page_direct_map(source->va_page,
        PMAP_CACHE_CACHED);
    target_mapping = pmap_page_direct_map(page, PMAP_CACHE_CACHED);
    if (source_mapping == 0 || target_mapping == 0) {
        (void)vm_page_free(vm_object_allocator, page, 1);
        target->va_flags &= ~VM_ANON_BUSY;
        (void)vm_anon_release(target);
        source->va_flags &= ~VM_ANON_BUSY;
        return EFAULT;
    }
    vm_object_copy(source_mapping, target_mapping, VM_PAGE_SIZE);
    target->va_page = page;
    target->va_flags &= ~VM_ANON_BUSY;
    --source->va_references;
    source->va_flags &= ~VM_ANON_BUSY;
    object_page->vop_anon = target;
    vm_object_stat_increment(&vm_object_statistics.vos_resident_pages);
    vm_object_stat_increment(&vm_object_statistics.vos_cow_faults);
    return 0;
}

int
vm_object_fault(struct vm_object *object, vm_ooffset_t offset,
    int private_write, struct vm_page **result)
{
    struct vm_object_page *object_page;
    struct vm_anon *anon;
    void *mapping;
    vm_pfn_t index;
    int new_page;
    int need_pagein;
    int error;

    if (!vm_object_valid(object) || result == 0)
        return EINVAL;
    if (offset >= (vm_ooffset_t)object->vo_size * VM_PAGE_SIZE)
        return ENXIO;
    index = (vm_pfn_t)(offset >> VM_PAGE_SHIFT);
    object_page = vm_object_page_find(object, index);
    new_page = object_page == 0;
    if (object_page == 0) {
        object_page = vm_object_page_alloc();
        anon = vm_anon_alloc();
        if (object_page == 0 || anon == 0) {
            if (object_page != 0)
                vm_object_page_free(object_page);
            if (anon != 0)
                (void)vm_anon_release(anon);
            return ENOSPC;
        }
        object_page->vop_index = index;
        object_page->vop_anon = anon;
        if (object->vo_pager != 0 &&
            object->vo_pager->vpo_pageout != 0) {
            anon->va_pager = object->vo_pager;
            anon->va_pager_cookie = object->vo_pager_cookie;
            anon->va_pager_offset = object->vo_pager_offset + offset;
        }
    }
    need_pagein = object->vo_pager != 0 &&
        object_page->vop_anon->va_page == 0 &&
        object_page->vop_anon->va_swap_slot == 0;
    error = vm_anon_make_resident(object_page->vop_anon,
        object->vo_pager == 0);
    if (error != 0) {
        if (new_page) {
            (void)vm_anon_release(object_page->vop_anon);
            vm_object_page_free(object_page);
        }
        return error;
    }
    if (need_pagein) {
        mapping = pmap_page_direct_map(object_page->vop_anon->va_page,
            PMAP_CACHE_CACHED);
        if (mapping == 0)
            error = EFAULT;
        else
            error = object->vo_pager->vpo_pagein(
                object->vo_pager_cookie,
                object->vo_pager_offset + offset, mapping,
                VM_PAGE_SIZE);
        if (error != 0) {
            if (new_page) {
                (void)vm_anon_release(object_page->vop_anon);
                vm_object_page_free(object_page);
            } else
                (void)vm_anon_discard_resident(
                    object_page->vop_anon);
            return error;
        }
        vm_object_stat_increment(&vm_object_statistics.vos_pageins);
    }
    if (new_page) {
        object_page->vop_next = object->vo_pages;
        object->vo_pages = object_page;
    }
    if (private_write) {
        error = vm_object_private_copy(object_page);
        if (error != 0)
            return error;
    }
    object_page->vop_anon->va_page->vmp_state = VM_PAGE_ACTIVE;
    *result = object_page->vop_anon->va_page;
    return 0;
}

struct vm_page *
vm_object_resident_page(struct vm_object *object, vm_ooffset_t offset)
{
    struct vm_object_page *object_page;
    vm_pfn_t index;

    if (!vm_object_valid(object) ||
        offset >= (vm_ooffset_t)object->vo_size * VM_PAGE_SIZE)
        return 0;
    index = (vm_pfn_t)(offset >> VM_PAGE_SHIFT);
    object_page = vm_object_page_find(object, index);
    return object_page == 0 ? 0 : object_page->vop_anon->va_page;
}

int
vm_object_mark_dirty(struct vm_object *object, vm_ooffset_t offset)
{
    struct vm_object_page *object_page;
    vm_pfn_t index;

    if (!vm_object_valid(object) ||
        offset >= (vm_ooffset_t)object->vo_size * VM_PAGE_SIZE)
        return EINVAL;
    index = (vm_pfn_t)(offset >> VM_PAGE_SHIFT);
    object_page = vm_object_page_find(object, index);
    if (object_page == 0)
        return ENOENT;
    object_page->vop_anon->va_flags |= VM_ANON_DIRTY;
    return 0;
}

int
vm_object_remove(struct vm_object *object, vm_ooffset_t offset,
    vm_size_t size)
{
    struct vm_object_page **link;
    struct vm_object_page *page;
    vm_pfn_t first;
    vm_pfn_t last;
    int error;

    if (!vm_object_range_valid(object, offset, size) ||
        (offset & VM_PAGE_MASK) != 0 || !vm_size_page_aligned(size))
        return EINVAL;
    first = (vm_pfn_t)(offset >> VM_PAGE_SHIFT);
    last = first + (size >> VM_PAGE_SHIFT);
    link = &object->vo_pages;
    while ((page = *link) != 0) {
        if (page->vop_index < first || page->vop_index >= last) {
            link = &page->vop_next;
            continue;
        }
        error = vm_anon_release(page->vop_anon);
        if (error != 0)
            return error;
        *link = page->vop_next;
        vm_object_page_free(page);
    }
    return 0;
}

static int
vm_object_range_valid(const struct vm_object *object, vm_ooffset_t offset,
    vm_size_t size)
{
    vm_ooffset_t end;
    vm_ooffset_t object_size;

    if (!vm_object_valid(object) || size == 0 ||
        vm_ooffset_add(offset, size, &end) != 0)
        return 0;
    object_size = (vm_ooffset_t)object->vo_size * VM_PAGE_SIZE;
    return end <= object_size;
}

int
vm_object_sync(struct vm_object *object, vm_ooffset_t offset,
    vm_size_t size, unsigned flags)
{
    struct vm_object_page *object_page;
    struct vm_anon *anon;
    struct vm_page *page;
    void *mapping;
    vm_pfn_t first;
    vm_pfn_t last;
    vm_ooffset_t end;
    int error;

    if (!vm_object_range_valid(object, offset, size) ||
        (flags & ~(VM_PAGER_IO_SYNC | VM_PAGER_IO_LOCKED |
        VM_PAGER_IO_INVALIDATE)) != 0)
        return EINVAL;
    if (object->vo_pager == 0 ||
        object->vo_pager->vpo_pageout == 0)
        return 0;
    first = (vm_pfn_t)(offset >> VM_PAGE_SHIFT);
    if (vm_ooffset_add(offset, size, &end) != 0)
        return EINVAL;
    last = (vm_pfn_t)(((end - 1) >> VM_PAGE_SHIFT) + 1);
    for (object_page = object->vo_pages; object_page != 0;
        object_page = object_page->vop_next) {
        if (object_page->vop_index < first ||
            object_page->vop_index >= last)
            continue;
        anon = object_page->vop_anon;
        page = anon->va_page;
        if (page == 0 || (page->vmp_dirty_count == 0 &&
            (anon->va_flags & VM_ANON_DIRTY) == 0))
            continue;
        mapping = pmap_page_direct_map(page, PMAP_CACHE_CACHED);
        if (mapping == 0)
            return EFAULT;
        error = object->vo_pager->vpo_pageout(
            object->vo_pager_cookie,
            object->vo_pager_offset +
            ((vm_ooffset_t)object_page->vop_index << VM_PAGE_SHIFT),
            mapping, VM_PAGE_SIZE, flags);
        if (error != 0)
            return error;
        error = pmap_clear_page_modify(page);
        if (error != 0)
            return error;
        anon->va_flags &= ~VM_ANON_DIRTY;
    }
    if ((flags & VM_PAGER_IO_SYNC) != 0 &&
        object->vo_pager->vpo_sync != 0) {
        error = object->vo_pager->vpo_sync(object->vo_pager_cookie,
            flags);
        if (error != 0)
            return error;
    }
    if ((flags & VM_PAGER_IO_INVALIDATE) != 0)
        return vm_object_invalidate(object, offset, size);
    return 0;
}

int
vm_object_update(struct vm_object *object, vm_ooffset_t offset,
    const void *buffer, vm_size_t size)
{
    struct vm_object_page *object_page;
    struct vm_anon *anon;
    unsigned char *mapping;
    const unsigned char *source;
    vm_ooffset_t page_offset;
    vm_size_t chunk;
    int error;

    if (!vm_object_range_valid(object, offset, size) || buffer == 0)
        return EINVAL;
    source = buffer;
    while (size != 0) {
        page_offset = offset & ~((vm_ooffset_t)VM_PAGE_MASK);
        chunk = VM_PAGE_SIZE - (vm_size_t)(offset & VM_PAGE_MASK);
        if (chunk > size)
            chunk = size;
        object_page = vm_object_page_find(object,
            (vm_pfn_t)(page_offset >> VM_PAGE_SHIFT));
        if (object_page != 0 && object_page->vop_anon->va_page != 0) {
            anon = object_page->vop_anon;
            mapping = pmap_page_direct_map(anon->va_page,
                PMAP_CACHE_CACHED);
            if (mapping == 0)
                return EFAULT;
            vm_object_copy(source,
                mapping + (vm_size_t)(offset & VM_PAGE_MASK), chunk);
            error = pmap_clear_page_modify(anon->va_page);
            if (error != 0)
                return error;
            anon->va_flags &= ~VM_ANON_DIRTY;
        }
        offset += chunk;
        source += chunk;
        size -= chunk;
    }
    return 0;
}

int
vm_object_zero_range(struct vm_object *object, vm_ooffset_t offset,
    vm_size_t size)
{
    unsigned char zeros[64];
    vm_size_t chunk;
    int error;

    if (!vm_object_range_valid(object, offset, size))
        return EINVAL;
    vm_object_zero(zeros, sizeof(zeros));
    while (size != 0) {
        chunk = size > sizeof(zeros) ? sizeof(zeros) : size;
        error = vm_object_update(object, offset, zeros, chunk);
        if (error != 0)
            return error;
        offset += chunk;
        size -= chunk;
    }
    return 0;
}

int
vm_object_invalidate(struct vm_object *object, vm_ooffset_t offset,
    vm_size_t size)
{
    struct vm_object_page **link;
    struct vm_object_page *page;
    vm_pfn_t first;
    vm_pfn_t last;
    vm_ooffset_t end;
    int error;

    if (!vm_object_range_valid(object, offset, size))
        return EINVAL;
    first = (vm_pfn_t)(offset >> VM_PAGE_SHIFT);
    if (vm_ooffset_add(offset, size, &end) != 0)
        return EINVAL;
    last = (vm_pfn_t)(((end - 1) >> VM_PAGE_SHIFT) + 1);
    for (page = object->vo_pages; page != 0; page = page->vop_next) {
        if (page->vop_index >= first && page->vop_index < last &&
            page->vop_anon->va_page != 0 &&
            page->vop_anon->va_page->vmp_wire_count != 0)
            return EBUSY;
    }
    link = &object->vo_pages;
    while ((page = *link) != 0) {
        if (page->vop_index < first || page->vop_index >= last) {
            link = &page->vop_next;
            continue;
        }
        if (page->vop_anon->va_page != 0) {
            error = pmap_remove_page(page->vop_anon->va_page);
            if (error != 0)
                return error;
        }
        error = vm_anon_release(page->vop_anon);
        if (error != 0)
            return error;
        *link = page->vop_next;
        vm_object_page_free(page);
    }
    return 0;
}

static int
vm_pager_reclaim_one(struct vm_anon *exclude)
{
    struct vm_anon *anon;
    struct vm_page *page;
    size_t slot;
    void *mapping;
    unsigned scanned;
    int deferred_error;
    int error;
    int new_slot;

    deferred_error = 0;
    for (scanned = 0; scanned < VM_ANON_MAX; ++scanned) {
        anon = &vm_anon_pool[vm_pager_clock++ % VM_ANON_MAX];
        if (anon == exclude || anon->va_in_use == 0 ||
            anon->va_page == 0 || (anon->va_flags & VM_ANON_BUSY) != 0)
            continue;
        page = anon->va_page;
        if (page->vmp_wire_count != 0 || page->vmp_busy_count != 0)
            continue;
        if (page->vmp_reference_count != 0) {
            (void)pmap_clear_page_reference(page);
            page->vmp_state = VM_PAGE_ACTIVE;
            continue;
        }
        if (page->vmp_state == VM_PAGE_ACTIVE) {
            page->vmp_state = VM_PAGE_INACTIVE;
            continue;
        }
        anon->va_flags |= VM_ANON_BUSY;
        if (anon->va_pager != 0 &&
            anon->va_pager->vpo_pageout != 0) {
            if (page->vmp_dirty_count != 0 ||
                (anon->va_flags & VM_ANON_DIRTY) != 0) {
                mapping = pmap_page_direct_map(page,
                    PMAP_CACHE_CACHED);
                if (mapping == 0) {
                    anon->va_flags &= ~VM_ANON_BUSY;
                    return EFAULT;
                }
                error = anon->va_pager->vpo_pageout(
                    anon->va_pager_cookie, anon->va_pager_offset,
                    mapping, VM_PAGE_SIZE, 0);
                if (error != 0) {
                    anon->va_flags &= ~VM_ANON_BUSY;
                    page->vmp_state = VM_PAGE_ACTIVE;
                    deferred_error = error;
                    continue;
                }
                error = pmap_clear_page_modify(page);
                if (error != 0) {
                    anon->va_flags &= ~VM_ANON_BUSY;
                    return error;
                }
                anon->va_flags &= ~VM_ANON_DIRTY;
            }
            error = pmap_remove_page(page);
            if (error == 0)
                error = vm_anon_discard_resident(anon);
            anon->va_flags &= ~VM_ANON_BUSY;
            if (error != 0)
                return error;
            vm_object_stat_increment(&vm_object_statistics.vos_pageouts);
            return 0;
        }
        slot = anon->va_swap_slot;
        new_slot = slot == 0;
        if (new_slot)
            slot = vm_pager_swap_alloc();
        if (slot == 0) {
            anon->va_flags &= ~VM_ANON_BUSY;
            deferred_error = ENOSPC;
            continue;
        }
        if (new_slot || page->vmp_dirty_count != 0 ||
            (anon->va_flags & VM_ANON_DIRTY) != 0) {
            error = vm_pager_swap_io(slot, page, 0);
            if (error != 0) {
                if (new_slot)
                    vm_pager_swap_free(slot);
                anon->va_flags &= ~VM_ANON_BUSY;
                page->vmp_state = VM_PAGE_ACTIVE;
                vm_object_stat_increment(
                    &vm_object_statistics.vos_swap_failures);
                deferred_error = error;
                continue;
            }
            anon->va_flags &= ~VM_ANON_DIRTY;
        }
        error = pmap_remove_page(page);
        if (error != 0) {
            if (new_slot)
                vm_pager_swap_free(slot);
            anon->va_flags &= ~VM_ANON_BUSY;
            return error;
        }
        error = vm_page_free(vm_object_allocator, page, 1);
        if (error != 0) {
            if (new_slot)
                vm_pager_swap_free(slot);
            anon->va_flags &= ~VM_ANON_BUSY;
            return error;
        }
        anon->va_page = 0;
        if (new_slot) {
            anon->va_swap_slot = slot;
            vm_object_stat_increment(
                &vm_object_statistics.vos_swapped_pages);
        }
        anon->va_flags &= ~VM_ANON_BUSY;
        vm_object_stat_decrement(
            &vm_object_statistics.vos_resident_pages);
        vm_object_stat_increment(&vm_object_statistics.vos_pageouts);
        return 0;
    }
    return deferred_error != 0 ? deferred_error : ENOMEM;
}

int
vm_object_get_stats(struct vm_object_stats *stats)
{
    if (!vm_object_initialized || stats == 0)
        return EINVAL;
    *stats = vm_object_statistics;
    return 0;
}

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
int
vm_pager_swap_init(void)
{
    if (!vm_object_initialized || nswap < VM_SWAP_BLOCKS + 1)
        return ENOSPC;
    vm_pager_swap_ready = 1;
    return 0;
}
#else
int
vm_pager_debug_swap_configure(unsigned pages)
{
    if (!vm_object_initialized || pages > VM_PAGER_HOST_SWAP_MAX)
        return EINVAL;
    vm_object_zero(vm_pager_host_swap_used,
        sizeof(vm_pager_host_swap_used));
    vm_object_zero(vm_pager_host_swap, sizeof(vm_pager_host_swap));
    vm_pager_host_swap_pages = pages;
    vm_pager_host_fail_read = 0;
    vm_pager_host_fail_write = 0;
    vm_pager_swap_ready = pages != 0;
    return 0;
}

void
vm_pager_debug_fail_io(int read, int write)
{
    vm_pager_host_fail_read = read != 0;
    vm_pager_host_fail_write = write != 0;
}
#endif

int
vm_pager_pageout_scan(void)
{
    int error;
    int aged;

    if (vm_object_allocator->vpa_free_count >= VM_PAGER_FREE_MIN)
        return 0;
    aged = 0;
    while (vm_object_allocator->vpa_free_count < VM_PAGER_FREE_TARGET) {
        error = vm_pager_reclaim_one(0);
        if (error == ENOMEM && aged++ == 0)
            continue;
        if (error != 0)
            return error;
    }
    return 0;
}
