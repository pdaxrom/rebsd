/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _VM_VMSPACE_INTERNAL_H_
#define _VM_VMSPACE_INTERNAL_H_

#include <vm/vmspace.h>

/*
 * Private interfaces shared by the vmspace translation units.  Keeping the
 * access/fault path separate makes that hot path independently testable by
 * the N64 mixed-compiler diagnostic builds.
 */
extern struct vm_page_allocator *vmspace_allocator;

int vmspace_valid(const struct vmspace *);
int vmspace_fault_context_valid(unsigned);

#endif /* _VM_VMSPACE_INTERNAL_H_ */
