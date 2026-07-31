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
#else
#include <errno.h>
#endif

#include <vm/vmspace_internal.h>
#include <vm/vm_object.h>

int
vmspace_fault_context(struct vmspace *vmspace, vm_vaddr_t address,
    vm_prot_t access, unsigned context)
{
    const struct vm_map_entry *entry;
    struct vm_page *page;
    struct vm_page *old_page;
    vm_ooffset_t offset;
    vm_paddr_t current;
    vm_paddr_t alias_mask;
    vm_prot_t effective;
    vm_vaddr_t page_address;
    int cow_write;
    int error;

    if (!vmspace_valid(vmspace) || !vmspace_fault_context_valid(context) ||
        (access != VM_PROT_READ &&
        access != VM_PROT_WRITE && access != VM_PROT_EXECUTE))
        return EINVAL;
    entry = vm_map_lookup(&vmspace->vms_map, address);
    if (entry == 0 || (entry->vme_protection & access) != access)
        return EFAULT;
    page_address = vm_vaddr_trunc_page(address);
    offset = entry->vme_offset + (page_address - entry->vme_start);
    if ((entry->vme_flags & VM_MAP_DEVICE) != 0) {
        if (entry->vme_object != 0 || offset > VM_PADDR_MAX ||
            access == VM_PROT_EXECUTE)
            return EFAULT;
        effective = entry->vme_protection & ~VM_PROT_EXECUTE;
        error = pmap_extract(vmspace->vms_pmap, page_address, &current);
        if (error == 0 && (current & ~VM_PAGE_MASK) ==
            (vm_paddr_t)offset)
            return 0;
        if (error != 0 && error != ENOENT)
            return error;
        return pmap_enter_device(vmspace->vms_pmap, page_address,
            (vm_paddr_t)offset, effective,
            (entry->vme_flags & VM_MAP_UNCACHED) != 0 ?
            PMAP_CACHE_UNCACHED : PMAP_CACHE_CACHED);
    }
    if (entry->vme_object == 0)
        return EFAULT;
    /*
     * A new second-level PTE table and the pmap reverse-map pool consume
     * wired pages.  Reserve both before the object fault can consume the
     * last free page.  Preparing only the directory slot and metadata used
     * by this fault avoids synchronous pager reclaim on every ordinary
     * user-page allocation.
     */
    error = pmap_prepare(vmspace->vms_pmap, page_address);
    if (error == ENOMEM && (context & VM_FAULT_CAN_SLEEP) != 0 &&
        vm_pager_reclaim_page() == 0)
        error = pmap_prepare(vmspace->vms_pmap, page_address);
    if (error != 0)
        return error;
    cow_write = access == VM_PROT_WRITE &&
        (entry->vme_flags & VM_MAP_COW) != 0;
    old_page = cow_write && (entry->vme_flags & VM_MAP_WIRED) != 0 ?
        vm_object_resident_page(entry->vme_object, offset) : 0;
    alias_mask = (entry->vme_flags & VM_MAP_UNCACHED) != 0 ?
        0 : pmap_cache_alias_mask();
    error = vm_object_fault_context(entry->vme_object, offset, cow_write,
        (context & VM_FAULT_CAN_SLEEP) != 0 ? 0 :
        VM_OBJECT_FAULT_NOWAIT, alias_mask,
        page_address & alias_mask, &page);
    if (error != 0)
        return error;
    if (old_page != 0 && old_page != page) {
        error = vm_page_counter_inc(vmspace_allocator, page,
            VM_PAGE_COUNTER_WIRE);
        if (error != 0)
            return error;
        error = vm_page_counter_dec(vmspace_allocator, old_page,
            VM_PAGE_COUNTER_WIRE);
        if (error != 0) {
            (void)vm_page_counter_dec(vmspace_allocator, page,
                VM_PAGE_COUNTER_WIRE);
            return error;
        }
    }
    error = pmap_extract(vmspace->vms_pmap, page_address, &current);
    effective = entry->vme_protection;
    if ((entry->vme_flags & VM_MAP_COW) != 0 && !cow_write)
        effective &= ~VM_PROT_WRITE;
    if (error == 0 && (current & ~VM_PAGE_MASK) == page->vmp_paddr) {
        if (cow_write)
            return pmap_protect(vmspace->vms_pmap, page_address,
                page_address + VM_PAGE_SIZE, effective);
        return 0;
    }
    if (error != 0 && error != ENOENT)
        return error;
    return pmap_enter(vmspace->vms_pmap, page_address, page, effective,
        PMAP_CACHE_CACHED);
}
