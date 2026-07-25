#include "memory.h"
#include "paging.h"

#define I386_PAGE_PRESENT       0x001u
#define I386_PAGE_WRITABLE      0x002u
#define I386_PAGE_FRAME         0xfffff000u
#define I386_PAGE_ENTRIES       1024u
#define I386_BOOT_IDENTITY_END  0x00400000u
#define I386_CR0_WRITE_PROTECT  0x00010000u
#define I386_CR0_PAGING         0x80000000u

extern char __kernel_start[];
extern char __kernel_rw_start[];
extern char __kernel_end[];

const i386_u32 i386_readonly_probe
    __attribute__((aligned(I386_PAGE_SIZE), used)) = 0x52454253u;

static i386_u32 *i386_page_directory;
static i386_u32 i386_page_directory_phys;

static i386_u32 *
i386_paging_table(i386_u32 address, int create)
{
    i386_u32 entry;
    i386_u32 table_phys;
    unsigned directory_index;

    directory_index = address >> 22;
    entry = i386_page_directory[directory_index];
    if ((entry & I386_PAGE_PRESENT) != 0)
        return (i386_u32 *)(entry & I386_PAGE_FRAME);
    if (!create)
        return (i386_u32 *)0;

    table_phys = i386_phys_alloc_page();
    if (table_phys == 0)
        return (i386_u32 *)0;
    i386_page_directory[directory_index] =
        table_phys | I386_PAGE_PRESENT | I386_PAGE_WRITABLE;
    return (i386_u32 *)table_phys;
}

static int
i386_paging_map_page(i386_u32 address)
{
    i386_u32 *table;
    unsigned table_index;

    table = i386_paging_table(address, 1);
    if (table == (i386_u32 *)0)
        return 0;
    table_index = (address >> 12) & 0x3ffu;
    table[table_index] =
        (address & I386_PAGE_FRAME) |
        I386_PAGE_PRESENT | I386_PAGE_WRITABLE;
    return 1;
}

static int
i386_paging_map_range(i386_u32 start, i386_u32 end)
{
    i386_u32 address;

    start &= I386_PAGE_FRAME;
    end = (end + I386_PAGE_MASK) & I386_PAGE_FRAME;
    for (address = start; address < end; address += I386_PAGE_SIZE) {
        if (!i386_paging_map_page(address))
            return 0;
    }
    return 1;
}

static i386_u32 *
i386_paging_entry(i386_u32 address)
{
    i386_u32 *table;

    table = i386_paging_table(address, 0);
    if (table == (i386_u32 *)0)
        return (i386_u32 *)0;
    return &table[(address >> 12) & 0x3ffu];
}

static int
i386_paging_protect_range(i386_u32 start, i386_u32 end)
{
    i386_u32 *entry;
    i386_u32 address;

    start &= I386_PAGE_FRAME;
    end = (end + I386_PAGE_MASK) & I386_PAGE_FRAME;
    for (address = start; address < end; address += I386_PAGE_SIZE) {
        entry = i386_paging_entry(address);
        if (entry == (i386_u32 *)0 ||
            (*entry & I386_PAGE_PRESENT) == 0)
            return 0;
        *entry &= ~I386_PAGE_WRITABLE;
    }
    return 1;
}

int
i386_paging_init(void)
{
    const struct i386_phys_range *range;
    i386_u32 cr0;
    unsigned index;

    i386_page_directory_phys = i386_phys_alloc_page();
    if (i386_page_directory_phys == 0)
        return 0;
    i386_page_directory = (i386_u32 *)i386_page_directory_phys;

    if (!i386_paging_map_range(0, I386_BOOT_IDENTITY_END))
        return 0;
    if (!i386_paging_map_range(
        (i386_u32)(unsigned long)__kernel_start,
        (i386_u32)(unsigned long)__kernel_end))
        return 0;

    for (index = 0; index < i386_memory_range_count(); ++index) {
        range = i386_memory_range(index);
        if (range == (const struct i386_phys_range *)0 ||
            !i386_paging_map_range(range->start, range->end))
            return 0;
    }

    if (!i386_paging_protect_range(
        (i386_u32)(unsigned long)__kernel_start,
        (i386_u32)(unsigned long)__kernel_rw_start))
        return 0;

    __asm__ volatile (
        "movl %1, %%cr3\n"
        "movl %%cr0, %0\n"
        "orl %2, %0\n"
        "movl %0, %%cr0\n"
        "jmp 1f\n"
        "1:"
        : "=&r" (cr0)
        : "r" (i386_page_directory_phys),
          "i" (I386_CR0_PAGING | I386_CR0_WRITE_PROTECT)
        : "cc", "memory");
    return i386_paging_enabled() &&
        i386_paging_write_protect_enabled() &&
        i386_paging_kernel_readonly();
}

int
i386_paging_enabled(void)
{
    i386_u32 cr0;

    __asm__ volatile ("movl %%cr0, %0" : "=r" (cr0));
    return (cr0 & I386_CR0_PAGING) != 0;
}

int
i386_paging_write_protect_enabled(void)
{
    i386_u32 cr0;

    __asm__ volatile ("movl %%cr0, %0" : "=r" (cr0));
    return (cr0 & I386_CR0_WRITE_PROTECT) != 0;
}

int
i386_paging_kernel_readonly(void)
{
    i386_u32 *entry;
    i386_u32 address;
    i386_u32 end;

    address = (i386_u32)(unsigned long)__kernel_start;
    end = (i386_u32)(unsigned long)__kernel_rw_start;
    while (address < end) {
        entry = i386_paging_entry(address);
        if (entry == (i386_u32 *)0 ||
            (*entry & (I386_PAGE_PRESENT | I386_PAGE_WRITABLE)) !=
            I386_PAGE_PRESENT)
            return 0;
        address += I386_PAGE_SIZE;
    }
    return 1;
}

i386_u32
i386_paging_directory(void)
{
    return i386_page_directory_phys;
}

void
i386_paging_page_fault_selftest(void)
{
    __asm__ volatile (
        "movl $0, (%0)"
        :
        : "r" (&i386_readonly_probe)
        : "memory");
}
