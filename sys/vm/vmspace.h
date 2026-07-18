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

struct vm_sysv_shm;

#define VM_FAULT_USER           0x01u
#define VM_FAULT_COPY           0x02u
#define VM_FAULT_KERNEL         0x03u
#define VM_FAULT_INTERRUPT      0x04u
#define VM_FAULT_CONTEXT_MASK   0x0fu
#define VM_FAULT_CAN_SLEEP      0x10u

struct vmspace {
    struct vm_map vms_map;
    struct pmap  *vms_pmap;
    struct vmspace_sysv_attachment {
        struct vm_sysv_shm *vsa_segment;
        vm_vaddr_t          vsa_start;
        vm_size_t           vsa_size;
    } vms_sysv_attachments[8];
    unsigned      vms_sysv_attachment_count;
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
int vmspace_map_object(struct vmspace *, vm_vaddr_t, vm_size_t,
    vm_prot_t, vm_prot_t, unsigned, struct vm_object *, vm_ooffset_t);
int vmspace_map_object_any(struct vmspace *, vm_vaddr_t, vm_size_t,
    vm_prot_t, vm_prot_t, unsigned, struct vm_object *, vm_ooffset_t,
    vm_vaddr_t *);
int vmspace_map_object_fixed(struct vmspace *, vm_vaddr_t, vm_size_t,
    vm_prot_t, vm_prot_t, unsigned, struct vm_object *, vm_ooffset_t);
int vmspace_map_device(struct vmspace *, vm_vaddr_t, vm_size_t,
    vm_prot_t, vm_prot_t, vm_paddr_t, enum pmap_cache);
int vmspace_map_device_any(struct vmspace *, vm_vaddr_t, vm_size_t,
    vm_prot_t, vm_prot_t, vm_paddr_t, enum pmap_cache, vm_vaddr_t *);
int vmspace_map_device_fixed(struct vmspace *, vm_vaddr_t, vm_size_t,
    vm_prot_t, vm_prot_t, vm_paddr_t, enum pmap_cache);
int vmspace_contains_object(const struct vmspace *, struct vm_object *);
int vmspace_map_anon(struct vmspace *, vm_vaddr_t, vm_size_t, vm_prot_t,
    unsigned);
int vmspace_grow_anon(struct vmspace *, vm_vaddr_t, vm_size_t, vm_prot_t,
    unsigned);
int vmspace_map_anon_any(struct vmspace *, vm_vaddr_t, vm_size_t,
    vm_prot_t, unsigned, vm_vaddr_t *);
int vmspace_map_anon_fixed(struct vmspace *, vm_vaddr_t, vm_size_t,
    vm_prot_t, unsigned);
int vmspace_unmap(struct vmspace *, vm_vaddr_t, vm_size_t);
int vmspace_sysv_attach(struct vmspace *, struct vm_sysv_shm *,
    vm_vaddr_t, vm_size_t, int, long);
int vmspace_sysv_detach(struct vmspace *, vm_vaddr_t, int, long);
int vmspace_protect(struct vmspace *, vm_vaddr_t, vm_size_t, vm_prot_t);
int vmspace_wire(struct vmspace *, vm_vaddr_t, vm_size_t, int);
int vmspace_mincore(const struct vmspace *, vm_vaddr_t, int *);
int vmspace_sync(struct vmspace *, vm_vaddr_t, vm_size_t, unsigned);
int vmspace_check(const struct vmspace *, vm_vaddr_t, vm_size_t, vm_prot_t);
unsigned vmspace_shared_mapping_count(const struct vmspace *);
int vmspace_fault(struct vmspace *, vm_vaddr_t, vm_prot_t);
int vmspace_fault_context(struct vmspace *, vm_vaddr_t, vm_prot_t,
    unsigned);
int vmspace_read(const struct vmspace *, vm_vaddr_t, void *, vm_size_t);
int vmspace_write(struct vmspace *, vm_vaddr_t, const void *, vm_size_t);
int vmspace_read_context(const struct vmspace *, vm_vaddr_t, void *,
    vm_size_t, unsigned);
int vmspace_write_context(struct vmspace *, vm_vaddr_t, const void *,
    vm_size_t, unsigned);
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
