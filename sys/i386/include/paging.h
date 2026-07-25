#ifndef _I386_PAGING_H_
#define _I386_PAGING_H_

#include "boot.h"

#define I386_PAGE_PROT_READ    0x01u
#define I386_PAGE_PROT_WRITE   0x02u
#define I386_PAGE_PROT_USER    0x04u
#define I386_PAGE_PROT_ALL     (I386_PAGE_PROT_READ | \
                                I386_PAGE_PROT_WRITE | \
                                I386_PAGE_PROT_USER)

int i386_paging_init(void);
int i386_paging_enabled(void);
int i386_paging_write_protect_enabled(void);
int i386_paging_kernel_readonly(void);
i386_u32 i386_paging_directory(void);
int i386_paging_activate_directory(i386_u32 directory);
void i386_paging_invalidate_page(i386_u32 vaddr);
int i386_paging_map(i386_u32 vaddr, i386_u32 paddr, unsigned protection);
int i386_paging_unmap(i386_u32 vaddr);
int i386_paging_protect(i386_u32 vaddr, unsigned protection);
int i386_paging_extract(i386_u32 vaddr, i386_u32 *paddr);
int i386_paging_query(i386_u32 vaddr, i386_u32 *paddr,
    unsigned *protection);
i386_u32 i386_paging_invalidation_count(void);
int i386_paging_primitives_selftest(void);
void i386_paging_page_fault_selftest(void);

#endif
