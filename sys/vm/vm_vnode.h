/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _VM_VM_VNODE_H_
#define _VM_VM_VNODE_H_

#include <vm/vm_object.h>

struct inode;

int vm_vnode_object_create(struct inode *, vm_ooffset_t, vm_size_t,
    struct vm_object **);

#endif /* _VM_VM_VNODE_H_ */
