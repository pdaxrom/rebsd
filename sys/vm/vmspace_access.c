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
#define vmspace_zero_memory(p, n) bzero((caddr_t)(p), (unsigned)(n))
#define vmspace_copy_memory(s, d, n) bcopy((s), (d), (unsigned)(n))
#else
#include <errno.h>
#include <string.h>
#define vmspace_zero_memory(p, n) memset((p), 0, (n))
#define vmspace_copy_memory(s, d, n) memcpy((d), (s), (n))
#endif

#include <vm/vmspace_internal.h>
#include <vm/vm_object.h>

static int
vmspace_transfer(const struct vmspace *vmspace, vm_vaddr_t address,
    void *buffer, vm_size_t size, vm_prot_t protection, int write,
    int zero, unsigned context)
{
    struct vm_page *page;
    unsigned char *physical;
    unsigned char *bytes;
    vm_paddr_t paddr;
    vm_size_t chunk;
    int error;

    if (!vmspace_valid(vmspace) || (buffer == 0 && !zero) || size == 0 ||
        (zero && !write) ||
        !vmspace_fault_context_valid(context))
        return EINVAL;
    error = vm_map_check(&vmspace->vms_map, address, size, protection);
    if (error != 0)
        return error;
    bytes = (unsigned char *)buffer;
    while (size != 0) {
        const struct vm_map_entry *entry;
        vm_ooffset_t offset;

        entry = vm_map_lookup(&vmspace->vms_map, address);
        if (entry == 0)
            return EFAULT;
        error = pmap_extract(vmspace->vms_pmap, address, &paddr);
        /*
         * copyin/copyout is used for every system call instruction,
         * pathname and argument.  A resident mapping needs no object fault;
         * the map-wide protection check above has already authorised the
         * transfer.  COW writes are the exception because their write fault
         * must first create the private page and writable PTE.
         */
        if (error == ENOENT ||
            (write && (entry->vme_flags & VM_MAP_COW) != 0)) {
            error = vmspace_fault_context((struct vmspace *)vmspace,
                address, protection, context);
            if (error != 0)
                return error;
            error = pmap_extract(vmspace->vms_pmap, address, &paddr);
        }
        if (error != 0)
            return error;
        if ((entry->vme_flags & VM_MAP_DEVICE) != 0) {
            page = 0;
            physical = (unsigned char *)pmap_device_direct_map(
                paddr & ~VM_PAGE_MASK,
                (entry->vme_flags & VM_MAP_UNCACHED) != 0 ?
                PMAP_CACHE_UNCACHED : PMAP_CACHE_CACHED);
        } else {
            page = vm_page_lookup(vmspace_allocator,
                paddr & ~VM_PAGE_MASK);
            if (page == 0)
                return EFAULT;
            physical = (unsigned char *)pmap_page_direct_map(page,
                PMAP_CACHE_CACHED);
        }
        if (physical == 0)
            return EFAULT;
        physical += paddr & VM_PAGE_MASK;
        chunk = VM_PAGE_SIZE - (paddr & VM_PAGE_MASK);
        if (chunk > size)
            chunk = size;
        /*
         * Cached user pages are allocated with the same cache colour as
         * their virtual address.  The KSEG0 direct map therefore selects
         * the same cache index, so ordinary copyin/copyout needs no
         * page-wide flush.  Writable executable mappings still need the
         * D-cache to I-cache handoff below.
         */
        if (write) {
            if (zero)
                vmspace_zero_memory(physical, chunk);
            else
                vmspace_copy_memory(bytes, physical, chunk);
        }
        else
            vmspace_copy_memory(physical, bytes, chunk);
        if (write && (entry->vme_flags & VM_MAP_DEVICE) == 0) {
            offset = entry->vme_offset +
                (vm_vaddr_trunc_page(address) - entry->vme_start);
            error = vm_object_mark_dirty(entry->vme_object, offset);
            if (error != 0)
                return error;
        }
        if (write && page != 0 &&
            (entry->vme_protection & VM_PROT_EXECUTE) != 0) {
            error = pmap_page_sync(page,
                PMAP_SYNC_DATA | PMAP_SYNC_INSTRUCTION);
            if (error != 0)
                return error;
        }
        address += chunk;
        if (!zero)
            bytes += chunk;
        size -= chunk;
    }
    return 0;
}

int
vmspace_read(const struct vmspace *vmspace, vm_vaddr_t address,
    void *buffer, vm_size_t size)
{
    return vmspace_transfer(vmspace, address, buffer, size,
        VM_PROT_READ, 0, 0, VM_FAULT_COPY | VM_FAULT_CAN_SLEEP);
}

int
vmspace_write(struct vmspace *vmspace, vm_vaddr_t address,
    const void *buffer, vm_size_t size)
{
    return vmspace_transfer(vmspace, address, (void *)buffer, size,
        VM_PROT_WRITE, 1, 0, VM_FAULT_COPY | VM_FAULT_CAN_SLEEP);
}

int
vmspace_read_context(const struct vmspace *vmspace, vm_vaddr_t address,
    void *buffer, vm_size_t size, unsigned context)
{
    return vmspace_transfer(vmspace, address, buffer, size,
        VM_PROT_READ, 0, 0, context);
}

int
vmspace_write_context(struct vmspace *vmspace, vm_vaddr_t address,
    const void *buffer, vm_size_t size, unsigned context)
{
    return vmspace_transfer(vmspace, address, (void *)buffer, size,
        VM_PROT_WRITE, 1, 0, context);
}

int
vmspace_zero(struct vmspace *vmspace, vm_vaddr_t address, vm_size_t size)
{
    if (size == 0)
        return 0;
    return vmspace_transfer(vmspace, address, 0, size,
        VM_PROT_WRITE, 1, 1, VM_FAULT_COPY | VM_FAULT_CAN_SLEEP);
}
