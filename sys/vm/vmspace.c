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
#include <sys/inode.h>
#include <sys/uio.h>
#define VMSPACE_MAX_SPACES      (NPROC + 4)
#define vmspace_zero_memory(p, n) bzero((caddr_t)(p), (unsigned)(n))
#define vmspace_copy_memory(s, d, n) bcopy((s), (d), (unsigned)(n))
#else
#include <errno.h>
#include <string.h>
#define VMSPACE_MAX_SPACES      36
#define vmspace_zero_memory(p, n) memset((p), 0, (n))
#define vmspace_copy_memory(s, d, n) memcpy((d), (s), (n))
#endif

#include <vm/vmspace.h>

#define VMSPACE_USER_MIN        0x00001000u
#define VMSPACE_USER_MAX        0x80000000u

static struct vmspace vmspace_pool[VMSPACE_MAX_SPACES];
static struct vm_page_allocator *vmspace_allocator;
static unsigned vmspace_initialized;

static int
vmspace_valid(const struct vmspace *vmspace)
{
    return vmspace != 0 && vmspace >= &vmspace_pool[0] &&
        vmspace < &vmspace_pool[VMSPACE_MAX_SPACES] &&
        vmspace->vms_in_use != 0;
}

int
vmspace_system_init(struct vm_page_allocator *allocator)
{
    if (allocator == 0 || allocator->vpa_initialized == 0)
        return EINVAL;
    vmspace_zero_memory(vmspace_pool, sizeof(vmspace_pool));
    vmspace_allocator = allocator;
    vmspace_initialized = 1;
    return 0;
}

int
vmspace_create(struct vmspace **result)
{
    struct vmspace *vmspace;
    unsigned index;
    int error;

    if (!vmspace_initialized || result == 0)
        return EINVAL;
    *result = 0;
    vmspace = 0;
    for (index = 0; index < VMSPACE_MAX_SPACES; ++index) {
        if (vmspace_pool[index].vms_in_use == 0) {
            vmspace = &vmspace_pool[index];
            break;
        }
    }
    if (vmspace == 0)
        return ENOSPC;
    vmspace_zero_memory(vmspace, sizeof(*vmspace));
    error = pmap_create(&vmspace->vms_pmap);
    if (error != 0)
        return error;
    error = vm_map_init(&vmspace->vms_map, VMSPACE_USER_MIN,
        VMSPACE_USER_MAX);
    if (error != 0) {
        (void)pmap_destroy(vmspace->vms_pmap);
        vmspace_zero_memory(vmspace, sizeof(*vmspace));
        return error;
    }
    vmspace->vms_in_use = 1;
    *result = vmspace;
    return 0;
}

static int
vmspace_free_page(struct vmspace *vmspace, vm_vaddr_t address)
{
    struct vm_page *page;
    vm_paddr_t paddr;
    int error;

    error = pmap_extract(vmspace->vms_pmap, address, &paddr);
    if (error == ENOENT)
        return 0;
    if (error != 0)
        return error;
    page = vm_page_lookup(vmspace_allocator,
        paddr & ~VM_PAGE_MASK);
    if (page == 0)
        return EFAULT;
    error = pmap_remove(vmspace->vms_pmap, address,
        address + VM_PAGE_SIZE);
    if (error != 0)
        return error;
    if (page->vmp_hold_count != 0)
        return 0;
    return vm_page_free(vmspace_allocator, page, 1);
}

int
vmspace_unmap(struct vmspace *vmspace, vm_vaddr_t start, vm_size_t size)
{
    vm_vaddr_t address;
    vm_vaddr_t end;
    int error;

    if (!vmspace_valid(vmspace) || size == 0 ||
        !vm_vaddr_page_aligned(start) || !vm_size_page_aligned(size) ||
        size - 1 > VM_VADDR_MAX - start)
        return EINVAL;
    end = start + size;
    if (end > vmspace->vms_map.vmm_max)
        return EINVAL;
    for (address = start; address < end; address += VM_PAGE_SIZE) {
        error = vmspace_free_page(vmspace, address);
        if (error != 0)
            return error;
    }
    return vm_map_remove(&vmspace->vms_map, start, end);
}

int
vmspace_destroy(struct vmspace *vmspace)
{
    struct vm_map_entry entry;
    int error;

    if (!vmspace_valid(vmspace))
        return EINVAL;
    while (vmspace->vms_map.vmm_count != 0) {
        entry = vmspace->vms_map.vmm_entries[0];
        error = vmspace_unmap(vmspace, entry.vme_start,
            entry.vme_end - entry.vme_start);
        if (error != 0)
            return error;
    }
    error = pmap_destroy(vmspace->vms_pmap);
    if (error != 0)
        return error;
    vmspace_zero_memory(vmspace, sizeof(*vmspace));
    return 0;
}

int
vmspace_activate(struct vmspace *vmspace)
{
    if (!vmspace_valid(vmspace))
        return EINVAL;
    return pmap_activate(vmspace->vms_pmap);
}

static int
vmspace_alloc_page(struct vmspace *vmspace, vm_vaddr_t address,
    vm_prot_t protection)
{
    struct vm_page_request request;
    struct vm_page *page;
    void *mapping;
    int error;

    vm_page_request_init(&request);
    request.vpr_state = VM_PAGE_ACTIVE;
    error = vm_page_alloc(vmspace_allocator, &request, &page);
    if (error != 0)
        return error;
    mapping = pmap_page_direct_map(page, PMAP_CACHE_CACHED);
    if (mapping == 0) {
        (void)vm_page_free(vmspace_allocator, page, 1);
        return EFAULT;
    }
    vmspace_zero_memory(mapping, VM_PAGE_SIZE);
    error = pmap_enter(vmspace->vms_pmap, address, page, protection,
        PMAP_CACHE_CACHED);
    if (error != 0) {
        (void)vm_page_free(vmspace_allocator, page, 1);
        return error;
    }
    return 0;
}

int
vmspace_map_anon(struct vmspace *vmspace, vm_vaddr_t start, vm_size_t size,
    vm_prot_t protection, unsigned flags)
{
    vm_vaddr_t address;
    vm_vaddr_t end;
    int error;

    if (!vmspace_valid(vmspace) || size == 0 ||
        !vm_vaddr_page_aligned(start) || !vm_size_page_aligned(size) ||
        size - 1 > VM_VADDR_MAX - start)
        return EINVAL;
    end = start + size;
    error = vm_map_insert(&vmspace->vms_map, start, end, protection,
        VM_PROT_ALL, flags | VM_MAP_ANON);
    if (error != 0)
        return error;
    for (address = start; address < end; address += VM_PAGE_SIZE) {
        error = vmspace_alloc_page(vmspace, address, protection);
        if (error != 0) {
            (void)vmspace_unmap(vmspace, start, size);
            return error;
        }
    }
    return 0;
}

int
vmspace_protect(struct vmspace *vmspace, vm_vaddr_t start, vm_size_t size,
    vm_prot_t protection)
{
    int error;

    if (!vmspace_valid(vmspace) || size == 0 ||
        !vm_vaddr_page_aligned(start) || !vm_size_page_aligned(size) ||
        size - 1 > VM_VADDR_MAX - start)
        return EINVAL;
    error = vm_map_protect(&vmspace->vms_map, start, start + size,
        protection);
    if (error != 0)
        return error;
    return pmap_protect(vmspace->vms_pmap, start, start + size,
        protection);
}

int
vmspace_check(const struct vmspace *vmspace, vm_vaddr_t start,
    vm_size_t size, vm_prot_t protection)
{
    if (!vmspace_valid(vmspace))
        return EINVAL;
    return vm_map_check(&vmspace->vms_map, start, size, protection);
}

static int
vmspace_transfer(const struct vmspace *vmspace, vm_vaddr_t address,
    void *buffer, vm_size_t size, vm_prot_t protection, int write)
{
    struct vm_page *page;
    unsigned char *physical;
    unsigned char *bytes;
    vm_paddr_t paddr;
    vm_size_t chunk;
    int error;

    if (!vmspace_valid(vmspace) || buffer == 0 || size == 0)
        return EINVAL;
    error = vm_map_check(&vmspace->vms_map, address, size, protection);
    if (error != 0)
        return error;
    bytes = (unsigned char *)buffer;
    while (size != 0) {
        error = pmap_extract(vmspace->vms_pmap, address, &paddr);
        if (error != 0)
            return error;
        page = vm_page_lookup(vmspace_allocator,
            paddr & ~VM_PAGE_MASK);
        if (page == 0)
            return EFAULT;
        physical = (unsigned char *)pmap_page_direct_map(page,
            PMAP_CACHE_CACHED);
        if (physical == 0)
            return EFAULT;
        physical += paddr & VM_PAGE_MASK;
        chunk = VM_PAGE_SIZE - (paddr & VM_PAGE_MASK);
        if (chunk > size)
            chunk = size;
        if (write)
            vmspace_copy_memory(bytes, physical, chunk);
        else
            vmspace_copy_memory(physical, bytes, chunk);
        if (write &&
            (vm_map_lookup(&vmspace->vms_map, address)->vme_protection &
            VM_PROT_EXECUTE) != 0) {
            error = pmap_page_sync(page,
                PMAP_SYNC_DATA | PMAP_SYNC_INSTRUCTION);
            if (error != 0)
                return error;
        }
        address += chunk;
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
        VM_PROT_READ, 0);
}

int
vmspace_write(struct vmspace *vmspace, vm_vaddr_t address,
    const void *buffer, vm_size_t size)
{
    return vmspace_transfer(vmspace, address, (void *)buffer, size,
        VM_PROT_WRITE, 1);
}

int
vmspace_zero(struct vmspace *vmspace, vm_vaddr_t address, vm_size_t size)
{
    unsigned char zeros[64];
    vm_size_t chunk;
    int error;

    if (size == 0)
        return 0;
    vmspace_zero_memory(zeros, sizeof(zeros));
    while (size != 0) {
        chunk = size < sizeof(zeros) ? size : sizeof(zeros);
        error = vmspace_write(vmspace, address, zeros, chunk);
        if (error != 0)
            return error;
        address += chunk;
        size -= chunk;
    }
    return 0;
}

int
vmspace_grow_stack(struct vmspace *vmspace, vm_vaddr_t current_start,
    vm_vaddr_t requested_start, vm_vaddr_t guard_end)
{
    vm_vaddr_t old_page;
    vm_vaddr_t new_page;

    if (!vmspace_valid(vmspace))
        return EINVAL;
    old_page = vm_vaddr_trunc_page(current_start);
    new_page = vm_vaddr_trunc_page(requested_start);
    if (new_page >= old_page)
        return 0;
    if (new_page < guard_end || old_page - new_page > VM_SIZE_MAX)
        return EFAULT;
    return vmspace_map_anon(vmspace, new_page, old_page - new_page,
        VM_PROT_READ | VM_PROT_WRITE, VM_MAP_STACK);
}

int
vmspace_clone(const struct vmspace *source, struct vmspace **result)
{
    struct vmspace *target;
    const struct vm_map_entry *entry;
    struct vm_page *source_page;
    struct vm_page *target_page;
    void *source_mapping;
    void *target_mapping;
    vm_paddr_t source_paddr;
    vm_paddr_t target_paddr;
    vm_vaddr_t address;
    unsigned index;
    int error;

    if (!vmspace_valid(source) || result == 0)
        return EINVAL;
    error = vmspace_create(&target);
    if (error != 0)
        return error;
    for (index = 0; index < source->vms_map.vmm_count; ++index) {
        entry = &source->vms_map.vmm_entries[index];
        error = vmspace_map_anon(target, entry->vme_start,
            entry->vme_end - entry->vme_start, entry->vme_protection,
            entry->vme_flags);
        if (error != 0)
            goto fail;
        for (address = entry->vme_start; address < entry->vme_end;
            address += VM_PAGE_SIZE) {
            error = pmap_extract(source->vms_pmap, address,
                &source_paddr);
            if (error != 0)
                goto fail;
            error = pmap_extract(target->vms_pmap, address,
                &target_paddr);
            if (error != 0)
                goto fail;
            source_page = vm_page_lookup(vmspace_allocator,
                source_paddr & ~VM_PAGE_MASK);
            target_page = vm_page_lookup(vmspace_allocator,
                target_paddr & ~VM_PAGE_MASK);
            if (source_page == 0 || target_page == 0) {
                error = EFAULT;
                goto fail;
            }
            source_mapping = pmap_page_direct_map(source_page,
                PMAP_CACHE_CACHED);
            target_mapping = pmap_page_direct_map(target_page,
                PMAP_CACHE_CACHED);
            if (source_mapping == 0 || target_mapping == 0) {
                error = EFAULT;
                goto fail;
            }
            vmspace_copy_memory(source_mapping, target_mapping,
                VM_PAGE_SIZE);
            if ((entry->vme_protection & VM_PROT_EXECUTE) != 0) {
                error = pmap_page_sync(target_page,
                    PMAP_SYNC_DATA | PMAP_SYNC_INSTRUCTION);
                if (error != 0)
                    goto fail;
            }
        }
    }
    *result = target;
    return 0;
fail:
    (void)vmspace_destroy(target);
    return error;
}

int
vmspace_validate(const struct vmspace *vmspace)
{
    if (!vmspace_valid(vmspace) ||
        vm_map_validate(&vmspace->vms_map) != 0)
        return EINVAL;
    return pmap_validate(vmspace->vms_pmap);
}

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
int
vmspace_read_inode(struct vmspace *vmspace, struct inode *inode,
    vm_vaddr_t address, vm_size_t size, off_t offset)
{
    struct vm_page *page;
    unsigned char *physical;
    vm_paddr_t paddr;
    vm_size_t chunk;
    int error;

    if (!vmspace_valid(vmspace) || inode == 0 || size == 0 ||
        vmspace_check(vmspace, address, size, VM_PROT_WRITE) != 0)
        return EINVAL;
    while (size != 0) {
        error = pmap_extract(vmspace->vms_pmap, address, &paddr);
        if (error != 0)
            return error;
        page = vm_page_lookup(vmspace_allocator,
            paddr & ~VM_PAGE_MASK);
        if (page == 0)
            return EFAULT;
        physical = (unsigned char *)pmap_page_direct_map(page,
            PMAP_CACHE_CACHED);
        if (physical == 0)
            return EFAULT;
        physical += paddr & VM_PAGE_MASK;
        chunk = VM_PAGE_SIZE - (paddr & VM_PAGE_MASK);
        if (chunk > size)
            chunk = size;
        error = rdwri(UIO_READ, inode, (caddr_t)physical, chunk,
            offset, IO_UNIT, 0);
        if (error != 0)
            return error;
        if ((vm_map_lookup(&vmspace->vms_map, address)->vme_protection &
            VM_PROT_EXECUTE) != 0) {
            error = pmap_page_sync(page,
                PMAP_SYNC_DATA | PMAP_SYNC_INSTRUCTION);
            if (error != 0)
                return error;
        }
        address += chunk;
        offset += chunk;
        size -= chunk;
    }
    return 0;
}

int
vmspace_write_inode(const struct vmspace *vmspace, struct inode *inode,
    vm_vaddr_t address, vm_size_t size, off_t offset)
{
    unsigned char buffer[256];
    vm_size_t chunk;
    int error;

    if (!vmspace_valid(vmspace) || inode == 0)
        return EINVAL;
    if (size == 0)
        return 0;
    if (vmspace_check(vmspace, address, size, VM_PROT_READ) != 0)
        return EFAULT;
    while (size != 0) {
        chunk = size < sizeof(buffer) ? size : sizeof(buffer);
        error = vmspace_read(vmspace, address, buffer, chunk);
        if (error != 0)
            return error;
        error = rdwri(UIO_WRITE, inode, (caddr_t)buffer, chunk,
            offset, IO_UNIT, 0);
        if (error != 0)
            return error;
        address += chunk;
        offset += chunk;
        size -= chunk;
    }
    return 0;
}
#endif
