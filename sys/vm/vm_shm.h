/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _VM_VM_SHM_H_
#define _VM_VM_SHM_H_

#include <vm/vm_param.h>

#define VM_SHM_NAME_MAX        31u
#define VM_SHM_MAX_OBJECTS     32u

struct vm_object;
struct vm_shm;

struct vm_shm_info {
    vm_size_t vsi_size;
    unsigned  vsi_owner;
    unsigned  vsi_group;
    unsigned  vsi_mode;
    unsigned  vsi_open_count;
    int       vsi_linked;
};

struct vm_shm_stats {
    unsigned  vss_objects;
    unsigned  vss_named_objects;
    unsigned  vss_open_files;
    vm_size_t vss_bytes;
};

int vm_shm_system_init(void);
int vm_shm_create(const char *, unsigned, unsigned, unsigned,
    struct vm_shm **);
int vm_shm_lookup(const char *, struct vm_shm **);
int vm_shm_retain(struct vm_shm *);
int vm_shm_close(struct vm_shm *);
int vm_shm_unlink(struct vm_shm *);
int vm_shm_get_info(const struct vm_shm *, struct vm_shm_info *);
int vm_shm_truncate(struct vm_shm *, vm_size_t);
int vm_shm_object_reference(struct vm_shm *, struct vm_object **);
int vm_shm_object_clone(struct vm_shm *, struct vm_object **);
int vm_shm_get_stats(struct vm_shm_stats *);

#endif /* _VM_VM_SHM_H_ */
