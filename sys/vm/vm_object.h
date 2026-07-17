/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _VM_VM_OBJECT_H_
#define _VM_VM_OBJECT_H_

#include <vm/pmap.h>
#include <vm/vm_map.h>

struct vm_object;

struct vm_object_pager_ops {
    void (*vpo_reference)(void *);
    void (*vpo_release)(void *);
    int  (*vpo_pagein)(void *, vm_ooffset_t, void *, vm_size_t);
    int  (*vpo_pageout)(void *, vm_ooffset_t, const void *, vm_size_t,
        unsigned);
    int  (*vpo_sync)(void *, unsigned);
};

#define VM_PAGER_IO_SYNC        0x01u
#define VM_PAGER_IO_LOCKED      0x02u
#define VM_PAGER_IO_INVALIDATE  0x04u

struct vm_object_stats {
    vm_pfn_t vos_objects;
    vm_pfn_t vos_anon_pages;
    vm_pfn_t vos_resident_pages;
    vm_pfn_t vos_swapped_pages;
    vm_pfn_t vos_zero_faults;
    vm_pfn_t vos_cow_faults;
    vm_pfn_t vos_pageins;
    vm_pfn_t vos_pageouts;
    vm_pfn_t vos_swap_failures;
};

int vm_object_system_init(struct vm_page_allocator *);
int vm_object_create(vm_size_t, struct vm_object **);
int vm_object_create_paged(vm_size_t, const struct vm_object_pager_ops *,
    void *, vm_ooffset_t, struct vm_object **);
int vm_object_clone(const struct vm_object *, struct vm_object **);
int vm_object_reference(struct vm_object *);
int vm_object_is_shared(const struct vm_object *);
int vm_object_has_pageout(const struct vm_object *);
int vm_object_release(struct vm_object *);
int vm_object_fault(struct vm_object *, vm_ooffset_t, int,
    struct vm_page **);
struct vm_page *vm_object_resident_page(struct vm_object *, vm_ooffset_t);
int vm_object_mark_dirty(struct vm_object *, vm_ooffset_t);
int vm_object_remove(struct vm_object *, vm_ooffset_t, vm_size_t);
int vm_object_sync(struct vm_object *, vm_ooffset_t, vm_size_t, unsigned);
int vm_object_update(struct vm_object *, vm_ooffset_t, const void *,
    vm_size_t);
int vm_object_zero_range(struct vm_object *, vm_ooffset_t, vm_size_t);
int vm_object_invalidate(struct vm_object *, vm_ooffset_t, vm_size_t);
int vm_object_get_stats(struct vm_object_stats *);
int vm_pager_pageout_scan(void);

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
int vm_pager_swap_init(void);
#else
int vm_pager_debug_swap_configure(unsigned);
void vm_pager_debug_fail_io(int, int);
#endif

#endif /* _VM_VM_OBJECT_H_ */
