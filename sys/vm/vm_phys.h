/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _VM_VM_PHYS_H_
#define _VM_VM_PHYS_H_

#include <vm/vm_param.h>

#ifndef VM_PHYS_MAX_REGIONS
#define VM_PHYS_MAX_REGIONS     32
#endif

enum vm_phys_region_kind {
    VM_PHYS_AVAILABLE = 1,
    VM_PHYS_RESERVED = 2
};

struct vm_phys_region {
    vm_paddr_t                 vpr_start;
    vm_paddr_t                 vpr_end;
    enum vm_phys_region_kind   vpr_kind;
    const char                *vpr_name;
};

struct vm_phys_map {
    struct vm_phys_region vpm_regions[VM_PHYS_MAX_REGIONS];
    unsigned              vpm_count;
    unsigned              vpm_finalized;
};

/* Region names are retained by pointer and must have static storage duration. */
void vm_phys_map_init(struct vm_phys_map *);
int vm_phys_map_add_ram(struct vm_phys_map *, vm_paddr_t, vm_size_t,
    const char *);
int vm_phys_map_reserve(struct vm_phys_map *, vm_paddr_t, vm_size_t,
    const char *);
int vm_phys_map_clip(const struct vm_phys_map *, vm_paddr_t, vm_size_t,
    vm_paddr_t *, vm_size_t *);
int vm_phys_map_validate(const struct vm_phys_map *);
int vm_phys_map_finalize(struct vm_phys_map *);

const struct vm_phys_region *vm_phys_map_region(const struct vm_phys_map *,
    unsigned);
unsigned vm_phys_map_count(const struct vm_phys_map *);
int vm_phys_map_total(const struct vm_phys_map *, enum vm_phys_region_kind,
    vm_size_t *);

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
extern struct vm_phys_map vm_phys_boot_map;

int vm_phys_bootstrap(vm_size_t);
void vm_phys_bootstrap_summary(void);

/* Implemented by each board; it must only describe physical ownership. */
int vm_phys_board_register(struct vm_phys_map *, vm_size_t);
#endif

#endif /* _VM_VM_PHYS_H_ */
