/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _VM_VM_MAP_H_
#define _VM_VM_MAP_H_

#include <vm/vm_param.h>

#define VM_MAP_MAX_ENTRIES      32

#define VM_MAP_ANON             0x0001u
#define VM_MAP_STACK            0x0002u
#define VM_MAP_EXECUTABLE       0x0004u
#define VM_MAP_SHARED           0x0008u
#define VM_MAP_COW              0x0010u
#define VM_MAP_WIRED            0x0020u
#define VM_MAP_DEVICE           0x0040u
#define VM_MAP_UNCACHED         0x0080u

struct vm_object;

struct vm_map_entry {
    vm_vaddr_t vme_start;
    vm_vaddr_t vme_end;
    vm_prot_t  vme_protection;
    vm_prot_t  vme_max_protection;
    unsigned   vme_flags;
    struct vm_object *vme_object;
    vm_ooffset_t vme_offset;
};

struct vm_map {
    struct vm_map_entry vmm_entries[VM_MAP_MAX_ENTRIES];
    vm_vaddr_t          vmm_min;
    vm_vaddr_t          vmm_max;
    unsigned            vmm_count;
};

int vm_map_init(struct vm_map *, vm_vaddr_t, vm_vaddr_t);
int vm_map_insert(struct vm_map *, vm_vaddr_t, vm_vaddr_t, vm_prot_t,
    vm_prot_t, unsigned);
int vm_map_insert_object(struct vm_map *, vm_vaddr_t, vm_vaddr_t,
    vm_prot_t, vm_prot_t, unsigned, struct vm_object *, vm_ooffset_t);
int vm_map_remove(struct vm_map *, vm_vaddr_t, vm_vaddr_t);
int vm_map_protect(struct vm_map *, vm_vaddr_t, vm_vaddr_t, vm_prot_t);
int vm_map_set_flags(struct vm_map *, vm_vaddr_t, vm_vaddr_t, unsigned,
    unsigned);
int vm_map_findspace(const struct vm_map *, vm_vaddr_t, vm_size_t,
    vm_vaddr_t *);
const struct vm_map_entry *vm_map_lookup(const struct vm_map *, vm_vaddr_t);
int vm_map_check(const struct vm_map *, vm_vaddr_t, vm_size_t, vm_prot_t);
int vm_map_validate(const struct vm_map *);

#endif /* _VM_VM_MAP_H_ */
