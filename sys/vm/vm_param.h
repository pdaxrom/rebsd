/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _VM_VM_PARAM_H_
#define _VM_VM_PARAM_H_

#include <vm/vm_types.h>

/* Independent of the legacy NBPG == 1024 accounting unit. */
#define VM_PAGE_SHIFT           12
#define VM_PAGE_SIZE            ((vm_size_t)1u << VM_PAGE_SHIFT)
#define VM_PAGE_MASK            (VM_PAGE_SIZE - 1u)

#define VM_PROT_NONE            ((vm_prot_t)0u)
#define VM_PROT_READ            ((vm_prot_t)0x01u)
#define VM_PROT_WRITE           ((vm_prot_t)0x02u)
#define VM_PROT_EXECUTE         ((vm_prot_t)0x04u)
#define VM_PROT_ALL             (VM_PROT_READ | VM_PROT_WRITE | \
                                 VM_PROT_EXECUTE)

int vm_vaddr_add(vm_vaddr_t, vm_size_t, vm_vaddr_t *);
int vm_paddr_add(vm_paddr_t, vm_size_t, vm_paddr_t *);
int vm_size_add(vm_size_t, vm_size_t, vm_size_t *);

int vm_vaddr_round_page(vm_vaddr_t, vm_vaddr_t *);
int vm_paddr_round_page(vm_paddr_t, vm_paddr_t *);
int vm_size_round_page(vm_size_t, vm_size_t *);

vm_vaddr_t vm_vaddr_trunc_page(vm_vaddr_t);
vm_paddr_t vm_paddr_trunc_page(vm_paddr_t);
int vm_vaddr_page_aligned(vm_vaddr_t);
int vm_paddr_page_aligned(vm_paddr_t);
int vm_size_page_aligned(vm_size_t);

int vm_paddr_to_pfn(vm_paddr_t, vm_pfn_t *);
int vm_pfn_to_paddr(vm_pfn_t, vm_paddr_t *);
int vm_size_to_pages(vm_size_t, vm_pfn_t *);

#endif /* _VM_VM_PARAM_H_ */
