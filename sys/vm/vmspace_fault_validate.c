/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <vm/vmspace_internal.h>

int
vmspace_fault_context_valid(unsigned context)
{
    unsigned kind;

    if ((context & ~(VM_FAULT_CONTEXT_MASK | VM_FAULT_CAN_SLEEP)) != 0)
        return 0;
    kind = context & VM_FAULT_CONTEXT_MASK;
    return kind >= VM_FAULT_USER && kind <= VM_FAULT_INTERRUPT;
}
