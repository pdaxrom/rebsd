#include "memory.h"
#include "paging.h"

#define I386_PAGE_PRESENT       0x001u
#define I386_PAGE_WRITABLE      0x002u
#define I386_PAGE_USER          0x004u
#define I386_PAGE_FRAME         0xfffff000u
#define I386_PAGE_ENTRIES       1024u
#define I386_BOOT_IDENTITY_END  0x00400000u
#define I386_CR0_WRITE_PROTECT  0x00010000u
#define I386_CR0_PAGING         0x80000000u
#define I386_PAGING_TEST_VADDR  0xff800000u

extern char __kernel_start[];
extern char __kernel_rw_start[];
extern char __kernel_end[];

const i386_u32 i386_readonly_probe
    __attribute__((aligned(I386_PAGE_SIZE), used)) = 0x52454253u;

static i386_u32 *i386_page_directory;
static i386_u32 i386_page_directory_phys;
static volatile i386_u32 i386_tlb_invalidations;

static int
i386_paging_protection_valid(unsigned protection)
{
    return (protection & ~I386_PAGE_PROT_ALL) == 0 &&
        (protection & I386_PAGE_PROT_READ) != 0;
}

static i386_u32 *
i386_paging_table(i386_u32 address, int create, int user)
{
    i386_u32 entry;
    i386_u32 table_phys;
    unsigned directory_index;

    if (i386_page_directory == (i386_u32 *)0)
        return (i386_u32 *)0;
    directory_index = address >> 22;
    entry = i386_page_directory[directory_index];
    if ((entry & I386_PAGE_PRESENT) != 0) {
        if (user && (entry & I386_PAGE_USER) == 0)
            i386_page_directory[directory_index] |= I386_PAGE_USER;
        return (i386_u32 *)(entry & I386_PAGE_FRAME);
    }
    if (!create)
        return (i386_u32 *)0;

    table_phys = i386_phys_alloc_page();
    if (table_phys == 0)
        return (i386_u32 *)0;
    i386_page_directory[directory_index] =
        table_phys | I386_PAGE_PRESENT | I386_PAGE_WRITABLE;
    if (user)
        i386_page_directory[directory_index] |= I386_PAGE_USER;
    return (i386_u32 *)table_phys;
}

static int
i386_paging_map_page(i386_u32 address)
{
    i386_u32 *table;
    unsigned table_index;

    table = i386_paging_table(address, 1, 0);
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

    table = i386_paging_table(address, 0, 0);
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

static void
i386_paging_invalidate(i386_u32 vaddr)
{
    __asm__ volatile ("invlpg (%0)" : : "r" (vaddr) : "memory");
    ++i386_tlb_invalidations;
}

int
i386_paging_map(i386_u32 vaddr, i386_u32 paddr, unsigned protection)
{
    i386_u32 *table;
    i386_u32 entry;
    unsigned table_index;

    if ((vaddr & I386_PAGE_MASK) != 0 ||
        (paddr & I386_PAGE_MASK) != 0 ||
        !i386_paging_protection_valid(protection))
        return 0;

    table = i386_paging_table(vaddr, 1,
        (protection & I386_PAGE_PROT_USER) != 0);
    if (table == (i386_u32 *)0)
        return 0;
    table_index = (vaddr >> 12) & 0x3ffu;
    if ((table[table_index] & I386_PAGE_PRESENT) != 0)
        return 0;

    entry = (paddr & I386_PAGE_FRAME) | I386_PAGE_PRESENT;
    if ((protection & I386_PAGE_PROT_WRITE) != 0)
        entry |= I386_PAGE_WRITABLE;
    if ((protection & I386_PAGE_PROT_USER) != 0)
        entry |= I386_PAGE_USER;
    table[table_index] = entry;
    i386_paging_invalidate(vaddr);
    return 1;
}

int
i386_paging_unmap(i386_u32 vaddr)
{
    i386_u32 *entry;

    if ((vaddr & I386_PAGE_MASK) != 0)
        return 0;
    entry = i386_paging_entry(vaddr);
    if (entry == (i386_u32 *)0 ||
        (*entry & I386_PAGE_PRESENT) == 0)
        return 0;
    *entry = 0;
    i386_paging_invalidate(vaddr);
    return 1;
}

int
i386_paging_protect(i386_u32 vaddr, unsigned protection)
{
    i386_u32 *entry;

    if ((vaddr & I386_PAGE_MASK) != 0 ||
        !i386_paging_protection_valid(protection))
        return 0;
    entry = i386_paging_entry(vaddr);
    if (entry == (i386_u32 *)0 ||
        (*entry & I386_PAGE_PRESENT) == 0)
        return 0;

    *entry &= ~(I386_PAGE_WRITABLE | I386_PAGE_USER);
    if ((protection & I386_PAGE_PROT_WRITE) != 0)
        *entry |= I386_PAGE_WRITABLE;
    if ((protection & I386_PAGE_PROT_USER) != 0) {
        *entry |= I386_PAGE_USER;
        i386_page_directory[vaddr >> 22] |= I386_PAGE_USER;
    }
    i386_paging_invalidate(vaddr);
    return 1;
}

int
i386_paging_query(i386_u32 vaddr, i386_u32 *paddr, unsigned *protection)
{
    i386_u32 *entry;
    unsigned result;

    entry = i386_paging_entry(vaddr);
    if (entry == (i386_u32 *)0 ||
        (*entry & I386_PAGE_PRESENT) == 0)
        return 0;

    if (paddr != (i386_u32 *)0)
        *paddr = (*entry & I386_PAGE_FRAME) |
            (vaddr & I386_PAGE_MASK);
    if (protection != (unsigned *)0) {
        result = I386_PAGE_PROT_READ;
        if ((*entry & I386_PAGE_WRITABLE) != 0)
            result |= I386_PAGE_PROT_WRITE;
        if ((*entry & I386_PAGE_USER) != 0)
            result |= I386_PAGE_PROT_USER;
        *protection = result;
    }
    return 1;
}

int
i386_paging_extract(i386_u32 vaddr, i386_u32 *paddr)
{
    if (paddr == (i386_u32 *)0)
        return 0;
    return i386_paging_query(vaddr, paddr, (unsigned *)0);
}

i386_u32
i386_paging_invalidation_count(void)
{
    return i386_tlb_invalidations;
}

int
i386_paging_primitives_selftest(void)
{
    volatile i386_u32 *mapped;
    volatile i386_u32 *physical;
    i386_u32 first;
    i386_u32 second;
    i386_u32 extracted;
    i386_u32 invalidations;
    unsigned protection;

    first = i386_phys_alloc_page();
    second = i386_phys_alloc_page();
    if (first == 0 || second == 0)
        return 0;
    *(volatile i386_u32 *)first = 0x11223344u;
    *(volatile i386_u32 *)second = 0x55667788u;
    invalidations = i386_tlb_invalidations;

    if (!i386_paging_map(I386_PAGING_TEST_VADDR, first,
        I386_PAGE_PROT_READ | I386_PAGE_PROT_WRITE))
        return 0;
    mapped = (volatile i386_u32 *)I386_PAGING_TEST_VADDR;
    if (*mapped != 0x11223344u ||
        !i386_paging_extract(I386_PAGING_TEST_VADDR + 37u, &extracted) ||
        extracted != first + 37u)
        return 0;

    if (!i386_paging_protect(I386_PAGING_TEST_VADDR,
        I386_PAGE_PROT_READ) ||
        !i386_paging_query(I386_PAGING_TEST_VADDR, &extracted,
        &protection) ||
        extracted != first || protection != I386_PAGE_PROT_READ)
        return 0;
    if (!i386_paging_unmap(I386_PAGING_TEST_VADDR) ||
        i386_paging_extract(I386_PAGING_TEST_VADDR, &extracted))
        return 0;

    if (!i386_paging_map(I386_PAGING_TEST_VADDR, second,
        I386_PAGE_PROT_READ | I386_PAGE_PROT_WRITE) ||
        *mapped != 0x55667788u)
        return 0;
    *mapped = 0xa5a55a5au;
    physical = (volatile i386_u32 *)second;
    if (*physical != 0xa5a55a5au ||
        !i386_paging_unmap(I386_PAGING_TEST_VADDR))
        return 0;

    if (!i386_paging_map(I386_PAGING_TEST_VADDR, second,
        I386_PAGE_PROT_READ | I386_PAGE_PROT_USER) ||
        !i386_paging_query(I386_PAGING_TEST_VADDR, &extracted,
        &protection) ||
        extracted != second ||
        protection != (I386_PAGE_PROT_READ | I386_PAGE_PROT_USER) ||
        !i386_paging_unmap(I386_PAGING_TEST_VADDR))
        return 0;

    return i386_tlb_invalidations >= invalidations + 7u;
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
