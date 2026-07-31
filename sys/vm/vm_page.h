/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _VM_VM_PAGE_H_
#define _VM_VM_PAGE_H_

#include <vm/vm_phys.h>

enum vm_page_state {
    VM_PAGE_FREE = 1,
    VM_PAGE_WIRED,
    VM_PAGE_ACTIVE,
    VM_PAGE_INACTIVE,
    VM_PAGE_CACHED,
    VM_PAGE_BUSY,
    VM_PAGE_LAUNDRY,
    VM_PAGE_BAD,
    VM_PAGE_RESERVED
};

enum vm_page_counter {
    VM_PAGE_COUNTER_WIRE = 1,
    VM_PAGE_COUNTER_HOLD,
    VM_PAGE_COUNTER_REFERENCE,
    VM_PAGE_COUNTER_DIRTY,
    VM_PAGE_COUNTER_BUSY
};

#define VM_PAGE_FLAG_POISONED       0x01u
#define VM_PAGE_FLAG_DEVICE         0x02u
#define VM_PAGE_FREE_POISON         0xddu

/*
 * There is exactly one vm_page for each physical RAM page.  CPU
 * cacheability belongs to a mapping, not to this ownership record; cached
 * and uncached aliases must therefore resolve to the same vm_page.
 */
struct vm_page {
    vm_paddr_t         vmp_paddr;
    uint16_t           vmp_wire_count;
    uint16_t           vmp_hold_count;
    uint16_t           vmp_reference_count;
    uint16_t           vmp_dirty_count;
    uint16_t           vmp_busy_count;
    uint8_t            vmp_state;
    uint8_t            vmp_flags;
};

typedef char vm_assert_page_metadata_is_16[
    (sizeof(struct vm_page) == 16) ? 1 : -1];

typedef int (*vm_page_poison_fn)(void *, vm_paddr_t, uint8_t, int);

struct vm_page_allocator {
    struct vm_page    *vpa_pages;
    vm_pfn_t           vpa_page_count;
    vm_pfn_t           vpa_free_count;
    vm_pfn_t           vpa_reserved_count;
    vm_pfn_t           vpa_bad_count;
    vm_pfn_t           vpa_allocations;
    vm_pfn_t           vpa_frees;
    vm_pfn_t           vpa_allocation_failures;
    vm_pfn_t           vpa_poison_failures;
    vm_pfn_t           vpa_free_hint;
    vm_page_poison_fn  vpa_poison;
    void              *vpa_poison_arg;
    unsigned           vpa_initialized;
};

struct vm_page_request {
    vm_pfn_t           vpr_npages;
    vm_size_t          vpr_alignment;
    vm_size_t          vpr_boundary;
    vm_paddr_t         vpr_max_address;
    vm_paddr_t         vpr_color_mask;
    vm_paddr_t         vpr_color;
    enum vm_page_state vpr_state;
};

struct vm_page_stats {
    vm_pfn_t vps_total;
    vm_pfn_t vps_free;
    vm_pfn_t vps_reserved;
    vm_pfn_t vps_bad;
    vm_pfn_t vps_allocations;
    vm_pfn_t vps_frees;
    vm_pfn_t vps_allocation_failures;
    vm_pfn_t vps_poison_failures;
};

int vm_page_metadata_reserve(struct vm_phys_map *, vm_paddr_t *,
    vm_size_t *, vm_pfn_t *);
int vm_page_allocator_init(struct vm_page_allocator *,
    const struct vm_phys_map *, void *, vm_size_t);
void vm_page_allocator_set_poison(struct vm_page_allocator *,
    vm_page_poison_fn, void *);
void vm_page_request_init(struct vm_page_request *);
int vm_page_alloc(struct vm_page_allocator *, const struct vm_page_request *,
    struct vm_page **);
int vm_page_free(struct vm_page_allocator *, struct vm_page *, vm_pfn_t);
int vm_page_free_paddr(struct vm_page_allocator *, vm_paddr_t, vm_pfn_t);
struct vm_page *vm_page_lookup(struct vm_page_allocator *, vm_paddr_t);
int vm_page_set_state(struct vm_page_allocator *, struct vm_page *,
    enum vm_page_state);
int vm_page_mark_bad(struct vm_page_allocator *, struct vm_page *);
int vm_page_counter_inc(struct vm_page_allocator *, struct vm_page *,
    enum vm_page_counter);
int vm_page_counter_dec(struct vm_page_allocator *, struct vm_page *,
    enum vm_page_counter);
int vm_page_device_claim(struct vm_page_allocator *, struct vm_page *,
    vm_pfn_t);
int vm_page_device_release(struct vm_page_allocator *, struct vm_page *,
    vm_pfn_t);
int vm_page_allocator_validate(const struct vm_page_allocator *,
    const struct vm_phys_map *);
int vm_page_allocator_stats(const struct vm_page_allocator *,
    struct vm_page_stats *);

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
extern struct vm_page_allocator vm_page_boot_allocator;

int vm_page_bootstrap_init(const struct vm_phys_map *, vm_paddr_t,
    vm_size_t);
int vm_page_bootstrap_selftest(void);
void vm_page_bootstrap_summary(void);
int vm_page_bootstrap_stats(struct vm_page_stats *);
#endif

#endif /* _VM_VM_PAGE_H_ */
