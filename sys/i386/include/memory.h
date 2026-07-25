#ifndef _I386_MEMORY_H_
#define _I386_MEMORY_H_

#include "boot.h"

#define I386_PAGE_SIZE         4096u
#define I386_PAGE_MASK         (I386_PAGE_SIZE - 1u)
#define I386_KERNEL_BASE       0xc0000000u
#define I386_DIRECT_MAP_SIZE   0x40000000u
#define I386_PHYS_LIMIT        I386_DIRECT_MAP_SIZE
#define I386_PHYS_MAX_RANGES   256u

struct i386_phys_range {
    i386_u32 start;
    i386_u32 end;
};

int i386_memory_init(i386_u32 boot_params_phys);
unsigned i386_memory_range_count(void);
const struct i386_phys_range *i386_memory_range(unsigned index);
i386_u32 i386_memory_total_pages(void);
i386_u32 i386_memory_free_pages(void);
i386_u32 i386_memory_allocated_end(unsigned index);
i386_u32 i386_phys_alloc_page(void);
void i386_memory_handoff(void);

#endif
