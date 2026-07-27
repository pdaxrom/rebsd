/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _VM_PMAP_H_
#define _VM_PMAP_H_

#include <vm/vm_page.h>

struct pmap;

enum pmap_cache {
    PMAP_CACHE_CACHED = 0,
    PMAP_CACHE_UNCACHED = 1
};

#define PMAP_SYNC_DATA          0x01u
#define PMAP_SYNC_INSTRUCTION   0x02u
#define PMAP_INVALIDATE_DATA    0x04u

struct pmap_stats {
    vm_pfn_t pms_mappings;
    vm_pfn_t pms_resident_pages;
    vm_pfn_t pms_tlb_refills;
    vm_pfn_t pms_tlb_modified;
    vm_pfn_t pms_protection_faults;
    vm_pfn_t pms_targeted_invalidations;
    vm_pfn_t pms_full_flushes;
    vm_pfn_t pms_asid_rollovers;
};

/*
 * Last successful software-to-hardware TLB refill.  This is diagnostic
 * state only: consumers use it to tell an ordinary miss from a refill loop
 * without changing page residency or replacement policy.
 */
struct pmap_tlb_diagnostics {
    vm_pfn_t   ptd_refills;
    unsigned   ptd_last_pmap;
    vm_vaddr_t ptd_last_vaddr;
    vm_prot_t  ptd_last_access;
    unsigned   ptd_repeat;
    unsigned   ptd_last_entryhi;
    unsigned   ptd_last_entrylo0;
    unsigned   ptd_last_entrylo1;
    unsigned   ptd_active_pmap;
    unsigned   ptd_active_asid;
    uint32_t   ptd_query_pte;
    unsigned   ptd_query_entryhi;
    unsigned   ptd_query_entrylo0;
    unsigned   ptd_query_entrylo1;
    unsigned   ptd_fast_last_epc;
    unsigned   ptd_fast_last_vaddr;
    unsigned   ptd_fast_repeat;
    unsigned   ptd_active_directory;
    unsigned   ptd_fast_directory;
};

/*
 * Machine-independent pmap contract.  MIPS implements software TLB refill
 * and ASIDs; i386 implements non-PAE hardware page-table walk and CR3
 * activation behind the same interface.
 */
int pmap_system_init(struct vm_page_allocator *);
int pmap_create(struct pmap **);
int pmap_destroy(struct pmap *);
int pmap_prepare(struct pmap *, vm_vaddr_t);
int pmap_enter(struct pmap *, vm_vaddr_t, struct vm_page *, vm_prot_t,
    enum pmap_cache);
int pmap_enter_device(struct pmap *, vm_vaddr_t, vm_paddr_t, vm_prot_t,
    enum pmap_cache);
int pmap_remove(struct pmap *, vm_vaddr_t, vm_vaddr_t);
int pmap_protect(struct pmap *, vm_vaddr_t, vm_vaddr_t, vm_prot_t);
int pmap_extract(struct pmap *, vm_vaddr_t, vm_paddr_t *);
int pmap_fault(struct pmap *, vm_vaddr_t, vm_prot_t, int);
int pmap_fault_active(vm_vaddr_t, vm_prot_t, int);
int pmap_activate(struct pmap *);
void pmap_deactivate(struct pmap *);
int pmap_is_referenced(struct pmap *, vm_vaddr_t);
int pmap_clear_reference(struct pmap *, vm_vaddr_t);
int pmap_is_modified(struct pmap *, vm_vaddr_t);
int pmap_clear_modify(struct pmap *, vm_vaddr_t);
int pmap_remove_page(struct vm_page *);
int pmap_clear_page_reference(struct vm_page *);
int pmap_clear_page_modify(struct vm_page *);
vm_paddr_t pmap_cache_alias_mask(void);
int pmap_page_sync(struct vm_page *, unsigned);
int pmap_sync_phys_range(vm_paddr_t, vm_size_t, unsigned);
void *pmap_pages_direct_map(struct vm_page *, vm_pfn_t, enum pmap_cache);
void *pmap_page_direct_map(struct vm_page *, enum pmap_cache);
void *pmap_device_direct_map(vm_paddr_t, enum pmap_cache);
int pmap_get_stats(struct pmap_stats *);
int pmap_get_tlb_diagnostics(vm_vaddr_t, struct pmap_tlb_diagnostics *);
int pmap_validate(struct pmap *);

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
int pmap_bootstrap_selftest(void);
int pmap_bootstrap_stats(struct pmap_stats *);
void pmap_md_legacy_user_disable(void);
#endif

#ifdef REBSD_VM_HOST_TEST
void pmap_debug_force_asid_rollover(void);
#endif

#endif /* _VM_PMAP_H_ */
