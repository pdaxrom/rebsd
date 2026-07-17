/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _VM_VMSPACE_H_
#define _VM_VMSPACE_H_

#include <vm/vm_map.h>
#include <vm/pmap.h>

struct vmspace {
    struct vm_map vms_map;
    struct pmap  *vms_pmap;
    unsigned      vms_in_use;
};

int vmspace_system_init(struct vm_page_allocator *);
#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
struct vmspace *vmspace_current(void);
#endif
int vmspace_create(struct vmspace **);
int vmspace_clone(struct vmspace *, struct vmspace **);
int vmspace_destroy(struct vmspace *);
int vmspace_activate(struct vmspace *);
int vmspace_map_anon(struct vmspace *, vm_vaddr_t, vm_size_t, vm_prot_t,
    unsigned);
int vmspace_map_anon_any(struct vmspace *, vm_vaddr_t, vm_size_t,
    vm_prot_t, unsigned, vm_vaddr_t *);
int vmspace_unmap(struct vmspace *, vm_vaddr_t, vm_size_t);
int vmspace_protect(struct vmspace *, vm_vaddr_t, vm_size_t, vm_prot_t);
int vmspace_check(const struct vmspace *, vm_vaddr_t, vm_size_t, vm_prot_t);
int vmspace_fault(struct vmspace *, vm_vaddr_t, vm_prot_t);
int vmspace_read(const struct vmspace *, vm_vaddr_t, void *, vm_size_t);
int vmspace_write(struct vmspace *, vm_vaddr_t, const void *, vm_size_t);
int vmspace_zero(struct vmspace *, vm_vaddr_t, vm_size_t);
int vmspace_grow_stack(struct vmspace *, vm_vaddr_t, vm_vaddr_t,
    vm_vaddr_t);
int vmspace_validate(const struct vmspace *);

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
struct inode;
int vmspace_read_inode(struct vmspace *, struct inode *, vm_vaddr_t,
    vm_size_t, off_t);
int vmspace_write_inode(const struct vmspace *, struct inode *, vm_vaddr_t,
    vm_size_t, off_t);
#endif

#endif /* _VM_VMSPACE_H_ */
