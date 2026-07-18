/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _VM_VM_SYSV_SHM_H_
#define _VM_VM_SYSV_SHM_H_

#include <vm/vm_param.h>

#define VM_SYSV_SHM_MAX_SEGMENTS       32u
#define VM_SYSV_SHM_MAX_SEGMENT_BYTES  (64u * 1024u * 1024u)

struct vm_object;
struct vm_sysv_shm;

struct vm_sysv_shm_info {
    int       vssi_id;
    int       vssi_key;
    vm_size_t vssi_size;
    unsigned  vssi_owner;
    unsigned  vssi_group;
    unsigned  vssi_creator;
    unsigned  vssi_creator_group;
    unsigned  vssi_mode;
    unsigned  vssi_sequence;
    unsigned  vssi_attach_count;
    int       vssi_creator_pid;
    int       vssi_last_pid;
    long      vssi_attach_time;
    long      vssi_detach_time;
    long      vssi_change_time;
    int       vssi_removed;
};

struct vm_sysv_shm_stats {
    unsigned  vsss_segments;
    unsigned  vsss_removed_segments;
    unsigned  vsss_attachments;
    vm_pfn_t  vsss_pages;
    vm_size_t vsss_bytes;
};

int vm_sysv_shm_system_init(void);
int vm_sysv_shm_create(int, vm_size_t, unsigned, unsigned, unsigned,
    int, long, struct vm_sysv_shm **);
int vm_sysv_shm_lookup_key(int, struct vm_sysv_shm **);
int vm_sysv_shm_lookup_id(int, struct vm_sysv_shm **);
int vm_sysv_shm_get_info(const struct vm_sysv_shm *,
    struct vm_sysv_shm_info *);
int vm_sysv_shm_set_permissions(struct vm_sysv_shm *, unsigned, unsigned,
    unsigned, long);
int vm_sysv_shm_mark_remove(struct vm_sysv_shm *, int, long);
int vm_sysv_shm_object_reference(struct vm_sysv_shm *, struct vm_object **);
int vm_sysv_shm_attach(struct vm_sysv_shm *, int, long);
int vm_sysv_shm_detach(struct vm_sysv_shm *, int, long);
int vm_sysv_shm_get_stats(struct vm_sysv_shm_stats *);

#endif /* _VM_VM_SYSV_SHM_H_ */
