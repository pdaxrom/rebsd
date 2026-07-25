#ifndef _I386_VM_BOOTSTRAP_H_
#define _I386_VM_BOOTSTRAP_H_

#include "boot.h"

int i386_vm_bootstrap_init(void);
int i386_vm_bootstrap_selftest(void);
i386_u32 i386_vm_total_pages(void);
i386_u32 i386_vm_free_pages(void);
i386_u32 i386_vm_reserved_pages(void);

#endif
