/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <vm/vmspace_internal.h>

int
vmspace_fault(struct vmspace *vmspace, vm_vaddr_t address,
    vm_prot_t access)
{
    return vmspace_fault_context(vmspace, address, access,
        VM_FAULT_USER | VM_FAULT_CAN_SLEEP);
}
