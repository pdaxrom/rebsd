/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/* MIPS CP0, TLB, direct-map, and cache operations for the shared pmap. */

#include <sys/param.h>
#include <sys/errno.h>
#include <machine/io.h>
#include <vm/pmap.h>

#define PMAP_MD_KSEG0_BASE      0x80000000u
#define PMAP_MD_KSEG1_BASE      0xa0000000u
#define PMAP_MD_PHYS_MASK       0x1fffffffu
#define PMAP_MD_ASID_MASK       0x000000ffu
#define PMAP_MD_ENTRYHI_MASK    0xffffe0ffu
#define PMAP_MD_INVALID_BASE    0x40000000u
#define PMAP_MD_TLB_PAIR_SIZE   0x00002000u

extern unsigned pmap_md_legacy_user_entries(void);

#ifdef CI20
#define PMAP_MD_DCACHE_LINE     32u
#else
#define PMAP_MD_DCACHE_LINE     16u
#endif
#define PMAP_MD_ICACHE_LINE     32u

vm_paddr_t
pmap_md_cache_alias_mask(void)
{
#if defined(N64) || defined(MALTA_N64_8M_PROFILE)
    /* Four 4 KiB colours cover the direct-mapped 16 KiB I-cache. */
    return 3u * VM_PAGE_SIZE;
#else
    return 0;
#endif
}

unsigned
pmap_md_tlb_entries(void)
{
#if defined(MALTA64)
    return 48;
#elif defined(N64) || defined(CI20)
    return 32;
#else
    return 16;
#endif
}

unsigned
pmap_md_wired_entries(void)
{
    return mips_read_c0_register(C0_WIRED, 0);
}

void
pmap_md_activate(unsigned asid)
{
    unsigned entryhi;

    entryhi = mips_read_c0_register(C0_ENTRYHI, 0);
    entryhi = (entryhi & ~PMAP_MD_ASID_MASK) |
        (asid & PMAP_MD_ASID_MASK);
    mips_write_c0_register(C0_ENTRYHI, 0, entryhi);
}

void
pmap_md_tlb_update(unsigned entryhi, unsigned entrylo0, unsigned entrylo1)
{
    unsigned index;
    unsigned wired;
    int status;

    status = mips_intr_disable();
    mips_write_c0_register(C0_ENTRYHI, 0,
        entryhi & PMAP_MD_ENTRYHI_MASK);
    asm volatile (
        "nop\n"
        "nop\n"
        "nop\n"
        "tlbp\n"
        "nop\n"
        "nop\n"
        "nop"
        : : : "memory");
    index = mips_read_c0_register(C0_INDEX, 0);
    wired = mips_read_c0_register(C0_WIRED, 0);
    if ((index & 0x80000000u) == 0 && index >= wired &&
        index < pmap_md_tlb_entries()) {
        mips_tlb_write_indexed(index, TLB_PAGEMASK_4K, entryhi,
            entrylo0, entrylo1);
        mips_intr_restore(status);
        return;
    }
    asm volatile (
        "mtc0   %0, $5\n"
        "mtc0   %1, $10\n"
        "mtc0   %2, $2\n"
        "mtc0   %3, $3\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "tlbwr\n"
        "nop\n"
        "nop\n"
        "nop"
        : : "r" (TLB_PAGEMASK_4K), "r" (entryhi),
            "r" (entrylo0), "r" (entrylo1)
        : "memory");
    mips_intr_restore(status);
}

int
pmap_md_tlb_invalidate(unsigned entryhi)
{
    unsigned saved_entryhi;
    unsigned saved_pagemask;
    unsigned index;
    unsigned wired;
    int status;
    int found;

    status = mips_intr_disable();
    saved_entryhi = mips_read_c0_register(C0_ENTRYHI, 0);
    saved_pagemask = mips_read_c0_register(C0_PAGEMASK, 0);
    mips_write_c0_register(C0_ENTRYHI, 0,
        entryhi & PMAP_MD_ENTRYHI_MASK);
    asm volatile (
        "nop\n"
        "nop\n"
        "nop\n"
        "tlbp\n"
        "nop\n"
        "nop\n"
        "nop"
        : : : "memory");
    index = mips_read_c0_register(C0_INDEX, 0);
    wired = mips_read_c0_register(C0_WIRED, 0);
    found = (index & 0x80000000u) == 0 && index >= wired &&
        index < pmap_md_tlb_entries();
    if (found) {
        mips_tlb_write_indexed(index, TLB_PAGEMASK_4K,
            PMAP_MD_INVALID_BASE + index * PMAP_MD_TLB_PAIR_SIZE,
            0, 0);
    }
    mips_write_c0_register(C0_PAGEMASK, 0, saved_pagemask);
    mips_write_c0_register(C0_ENTRYHI, 0, saved_entryhi);
    mips_intr_restore(status);
    return found;
}

void
pmap_md_tlb_flush(void)
{
    unsigned saved_entryhi;
    unsigned saved_pagemask;
    unsigned entries;
    unsigned wired;
    unsigned index;
    int status;

    status = mips_intr_disable();
    saved_entryhi = mips_read_c0_register(C0_ENTRYHI, 0);
    saved_pagemask = mips_read_c0_register(C0_PAGEMASK, 0);
    entries = pmap_md_tlb_entries();
    wired = mips_read_c0_register(C0_WIRED, 0);
    for (index = wired; index < entries; ++index) {
        mips_tlb_write_indexed(index, TLB_PAGEMASK_4K,
            PMAP_MD_INVALID_BASE + index * PMAP_MD_TLB_PAIR_SIZE,
            0, 0);
    }
    mips_write_c0_register(C0_PAGEMASK, 0, saved_pagemask);
    mips_write_c0_register(C0_ENTRYHI, 0, saved_entryhi);
    mips_intr_restore(status);
}

void
pmap_md_legacy_user_disable(void)
{
    unsigned saved_entryhi;
    unsigned saved_pagemask;
    unsigned entries;
    unsigned index;
    int status;

    status = mips_intr_disable();
    saved_entryhi = mips_read_c0_register(C0_ENTRYHI, 0);
    saved_pagemask = mips_read_c0_register(C0_PAGEMASK, 0);
    entries = pmap_md_legacy_user_entries();
    for (index = 0; index < entries; ++index) {
        mips_tlb_write_indexed(index, TLB_PAGEMASK_4K,
            PMAP_MD_INVALID_BASE + index * PMAP_MD_TLB_PAIR_SIZE,
            0, 0);
    }
    mips_write_c0_register(C0_WIRED, 0, 0);
    mips_write_c0_register(C0_PAGEMASK, 0, saved_pagemask);
    mips_write_c0_register(C0_ENTRYHI, 0, saved_entryhi);
    mips_intr_restore(status);
}

void *
pmap_md_direct_map(vm_paddr_t paddr, vm_size_t size,
    enum pmap_cache cache)
{
    unsigned base;

    if (size == 0 || paddr > PMAP_MD_PHYS_MASK ||
        size - 1 > PMAP_MD_PHYS_MASK - paddr)
        return 0;
    if (cache == PMAP_CACHE_CACHED)
        base = PMAP_MD_KSEG0_BASE;
    else if (cache == PMAP_CACHE_UNCACHED)
        base = PMAP_MD_KSEG1_BASE;
    else
        return 0;
    return (void *)(base | paddr);
}

static void
pmap_md_sync(void)
{
    asm volatile ("sync" ::: "memory");
}

static void
pmap_md_dcache_writeback_invalidate(unsigned address)
{
    asm volatile ("cache 0x15, 0(%0)" :: "r" (address) : "memory");
}

static void
pmap_md_dcache_invalidate(unsigned address)
{
    asm volatile ("cache 0x11, 0(%0)" :: "r" (address) : "memory");
}

static void
pmap_md_icache_invalidate(unsigned address)
{
    asm volatile ("cache 0x10, 0(%0)" :: "r" (address) : "memory");
}

int
pmap_md_range_sync(vm_paddr_t paddr, vm_size_t size, unsigned operations)
{
    unsigned address;
    unsigned end;

    if (size == 0 || paddr > PMAP_MD_PHYS_MASK ||
        size - 1 > PMAP_MD_PHYS_MASK - paddr || operations == 0 ||
        (operations & ~(PMAP_SYNC_DATA | PMAP_SYNC_INSTRUCTION |
        PMAP_INVALIDATE_DATA)) != 0 ||
        (operations & (PMAP_SYNC_DATA | PMAP_INVALIDATE_DATA)) ==
        (PMAP_SYNC_DATA | PMAP_INVALIDATE_DATA))
        return EINVAL;
    pmap_md_sync();
    if ((operations & PMAP_SYNC_DATA) != 0) {
        address = paddr & ~(PMAP_MD_DCACHE_LINE - 1);
        end = (paddr + size + PMAP_MD_DCACHE_LINE - 1) &
            ~(PMAP_MD_DCACHE_LINE - 1);
        for (; address < end; address += PMAP_MD_DCACHE_LINE)
            pmap_md_dcache_writeback_invalidate(
                PMAP_MD_KSEG0_BASE | address);
        pmap_md_sync();
    }
    if ((operations & PMAP_INVALIDATE_DATA) != 0) {
        address = paddr & ~(PMAP_MD_DCACHE_LINE - 1);
        end = (paddr + size + PMAP_MD_DCACHE_LINE - 1) &
            ~(PMAP_MD_DCACHE_LINE - 1);
        for (; address < end; address += PMAP_MD_DCACHE_LINE)
            pmap_md_dcache_invalidate(PMAP_MD_KSEG0_BASE | address);
        pmap_md_sync();
    }
    if ((operations & PMAP_SYNC_INSTRUCTION) != 0) {
        address = paddr & ~(PMAP_MD_ICACHE_LINE - 1);
        end = (paddr + size + PMAP_MD_ICACHE_LINE - 1) &
            ~(PMAP_MD_ICACHE_LINE - 1);
        for (; address < end; address += PMAP_MD_ICACHE_LINE)
            pmap_md_icache_invalidate(PMAP_MD_KSEG0_BASE | address);
        pmap_md_sync();
    }
    return 0;
}

int
pmap_md_page_sync(vm_paddr_t paddr, unsigned operations)
{
    if (!vm_paddr_page_aligned(paddr))
        return EINVAL;
    return pmap_md_range_sync(paddr, VM_PAGE_SIZE, operations);
}
