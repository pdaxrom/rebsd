#ifndef _I386_PAGING_H_
#define _I386_PAGING_H_

#include "boot.h"

int i386_paging_init(void);
int i386_paging_enabled(void);
int i386_paging_write_protect_enabled(void);
int i386_paging_kernel_readonly(void);
i386_u32 i386_paging_directory(void);
void i386_paging_page_fault_selftest(void);

#endif
