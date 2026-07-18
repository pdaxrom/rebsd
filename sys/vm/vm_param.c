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
#include <sys/errno.h>
#endif
#include <vm/vm_param.h>

int
vm_vaddr_add(vm_vaddr_t base, vm_size_t size, vm_vaddr_t *result)
{
    if (result == 0)
        return EINVAL;
    if (size > VM_VADDR_MAX - base)
        return EOVERFLOW;
    *result = base + size;
    return 0;
}

int
vm_paddr_add(vm_paddr_t base, vm_size_t size, vm_paddr_t *result)
{
    if (result == 0)
        return EINVAL;
    if (size > VM_PADDR_MAX - base)
        return EOVERFLOW;
    *result = base + size;
    return 0;
}

int
vm_size_add(vm_size_t left, vm_size_t right, vm_size_t *result)
{
    if (result == 0)
        return EINVAL;
    if (right > VM_SIZE_MAX - left)
        return EOVERFLOW;
    *result = left + right;
    return 0;
}

int
vm_ooffset_add(vm_ooffset_t base, vm_size_t size, vm_ooffset_t *result)
{
    if (result == 0)
        return EINVAL;
    if ((vm_ooffset_t)size > UINT64_MAX - base)
        return EOVERFLOW;
    *result = base + size;
    return 0;
}

int
vm_vaddr_round_page(vm_vaddr_t value, vm_vaddr_t *result)
{
    if (result == 0)
        return EINVAL;
    if (value > VM_VADDR_MAX - VM_PAGE_MASK)
        return EOVERFLOW;
    *result = (value + VM_PAGE_MASK) & ~VM_PAGE_MASK;
    return 0;
}

int
vm_paddr_round_page(vm_paddr_t value, vm_paddr_t *result)
{
    if (result == 0)
        return EINVAL;
    if (value > VM_PADDR_MAX - VM_PAGE_MASK)
        return EOVERFLOW;
    *result = (value + VM_PAGE_MASK) & ~VM_PAGE_MASK;
    return 0;
}

int
vm_size_round_page(vm_size_t value, vm_size_t *result)
{
    if (result == 0)
        return EINVAL;
    if (value > VM_SIZE_MAX - VM_PAGE_MASK)
        return EOVERFLOW;
    *result = (value + VM_PAGE_MASK) & ~VM_PAGE_MASK;
    return 0;
}

vm_vaddr_t
vm_vaddr_trunc_page(vm_vaddr_t value)
{
    return value & ~VM_PAGE_MASK;
}

vm_paddr_t
vm_paddr_trunc_page(vm_paddr_t value)
{
    return value & ~VM_PAGE_MASK;
}

int
vm_vaddr_page_aligned(vm_vaddr_t value)
{
    return (value & VM_PAGE_MASK) == 0;
}

int
vm_paddr_page_aligned(vm_paddr_t value)
{
    return (value & VM_PAGE_MASK) == 0;
}

int
vm_size_page_aligned(vm_size_t value)
{
    return (value & VM_PAGE_MASK) == 0;
}

int
vm_paddr_to_pfn(vm_paddr_t paddr, vm_pfn_t *pfn)
{
    if (pfn == 0)
        return EINVAL;
    if (!vm_paddr_page_aligned(paddr))
        return EINVAL;
    *pfn = paddr >> VM_PAGE_SHIFT;
    return 0;
}

int
vm_pfn_to_paddr(vm_pfn_t pfn, vm_paddr_t *paddr)
{
    if (paddr == 0)
        return EINVAL;
    if (pfn > (VM_PADDR_MAX >> VM_PAGE_SHIFT))
        return EOVERFLOW;
    *paddr = pfn << VM_PAGE_SHIFT;
    return 0;
}

int
vm_size_to_pages(vm_size_t size, vm_pfn_t *pages)
{
    vm_size_t rounded;
    int error;

    if (pages == 0)
        return EINVAL;
    if (size == 0) {
        *pages = 0;
        return 0;
    }
    error = vm_size_round_page(size, &rounded);
    if (error != 0)
        return error;
    *pages = rounded >> VM_PAGE_SHIFT;
    return 0;
}
