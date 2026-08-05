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
#ifdef CI20
#include <machine/layout.h>
#endif
#include <vm/pmap.h>

#define PMAP_MD_KSEG0_BASE      0x80000000u
#define PMAP_MD_KSEG1_BASE      0xa0000000u
#define PMAP_MD_PHYS_MASK       0x1fffffffu
#define PMAP_MD_ASID_MASK       0x000000ffu
#define PMAP_MD_ENTRYHI_MASK    0xffffe0ffu
#ifdef CI20
#define PMAP_MD_PERMANENT_WIRED 4u
#else
#define PMAP_MD_PERMANENT_WIRED 0u
#endif

extern unsigned pmap_md_legacy_user_entries(void);

#ifdef CI20
#define PMAP_MD_DCACHE_LINE     32u
#else
#define PMAP_MD_DCACHE_LINE     16u
#endif
#define PMAP_MD_ICACHE_LINE     32u
#ifdef N64
#define PMAP_MD_ICACHE_SIZE     (16u * 1024u)
#endif

#ifdef CI20
static unsigned pmap_md_icache_line;
static unsigned pmap_md_icache_sets;
static unsigned pmap_md_icache_ways;
static unsigned pmap_md_icache_way_size;

static void pmap_md_sync(void);

static void
pmap_md_icache_index_invalidate(void)
{
    unsigned address;
    unsigned end;
    unsigned way;

    for (way = 0; way < pmap_md_icache_ways; ++way) {
        address = PMAP_MD_KSEG0_BASE + way * pmap_md_icache_way_size;
        end = address + pmap_md_icache_way_size;
        for (; address < end; address += pmap_md_icache_line) {
            asm volatile ("cache 0x00, 0(%0)" :: "r" (address) :
                "memory");
        }
    }
    pmap_md_sync();
}
#endif

int
pmap_md_cache_init(void)
{
#ifdef CI20
    unsigned config1;
    unsigned line_field;
    unsigned sets_field;
    unsigned ways_field;

    config1 = mips_read_c0_register(C0_CONFIG, 1);
    line_field = (config1 >> C0_CONFIG1_IL_SHIFT) &
        C0_CONFIG1_CACHE_MASK;
    sets_field = (config1 >> C0_CONFIG1_IS_SHIFT) &
        C0_CONFIG1_CACHE_MASK;
    ways_field = (config1 >> C0_CONFIG1_IA_SHIFT) &
        C0_CONFIG1_CACHE_MASK;
    if (line_field == 0)
        return ENXIO;
    pmap_md_icache_line = 2u << line_field;
    pmap_md_icache_sets = 64u << sets_field;
    pmap_md_icache_ways = ways_field + 1u;
    pmap_md_icache_way_size = pmap_md_icache_line *
        pmap_md_icache_sets;
#endif
    return 0;
}

void
pmap_md_cache_activate(void)
{
#ifdef CI20
    /* XBurst has a virtually tagged I-cache: discard the old VA context. */
    pmap_md_icache_index_invalidate();
#endif
}

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
            mips_tlb_invalid_entryhi(index), 0, 0);
    }
    mips_write_c0_register(C0_PAGEMASK, 0, saved_pagemask);
    mips_write_c0_register(C0_ENTRYHI, 0, saved_entryhi);
    mips_intr_restore(status);
    return found;
}

int
pmap_md_tlb_lookup(unsigned entryhi, unsigned *result_index,
    unsigned *result_pagemask, unsigned *result_entryhi,
    unsigned *result_entrylo0, unsigned *result_entrylo1)
{
    unsigned saved_index;
    unsigned saved_entryhi;
    unsigned saved_entrylo0;
    unsigned saved_entrylo1;
    unsigned saved_pagemask;
    unsigned index;
    unsigned wired;
    int status;
    int error;

    if (result_index == 0 || result_pagemask == 0 ||
        result_entryhi == 0 || result_entrylo0 == 0 ||
        result_entrylo1 == 0)
        return EINVAL;

    status = mips_intr_disable();
    saved_index = mips_read_c0_register(C0_INDEX, 0);
    saved_entryhi = mips_read_c0_register(C0_ENTRYHI, 0);
    saved_entrylo0 = mips_read_c0_register(C0_ENTRYLO0, 0);
    saved_entrylo1 = mips_read_c0_register(C0_ENTRYLO1, 0);
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
    if ((index & 0x80000000u) != 0 || index < wired ||
        index >= pmap_md_tlb_entries()) {
        error = ENOENT;
    } else {
        asm volatile (
            "tlbr\n"
            "nop\n"
            "nop\n"
            "nop"
            : : : "memory");
        *result_index = index;
        *result_pagemask = mips_read_c0_register(C0_PAGEMASK, 0);
        *result_entryhi = mips_read_c0_register(C0_ENTRYHI, 0);
        *result_entrylo0 = mips_read_c0_register(C0_ENTRYLO0, 0);
        *result_entrylo1 = mips_read_c0_register(C0_ENTRYLO1, 0);
        error = 0;
    }
    mips_write_c0_register(C0_INDEX, 0, saved_index);
    mips_write_c0_register(C0_PAGEMASK, 0, saved_pagemask);
    mips_write_c0_register(C0_ENTRYLO0, 0, saved_entrylo0);
    mips_write_c0_register(C0_ENTRYLO1, 0, saved_entrylo1);
    mips_write_c0_register(C0_ENTRYHI, 0, saved_entryhi);
    mips_intr_restore(status);
    return error;
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
            mips_tlb_invalid_entryhi(index), 0, 0);
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
            mips_tlb_invalid_entryhi(index), 0, 0);
    }
    mips_write_c0_register(C0_WIRED, 0, PMAP_MD_PERMANENT_WIRED);
    mips_write_c0_register(C0_PAGEMASK, 0, saved_pagemask);
    mips_write_c0_register(C0_ENTRYHI, 0, saved_entryhi);
    mips_intr_restore(status);
}

void *
pmap_md_direct_map(vm_paddr_t paddr, vm_size_t size,
    enum pmap_cache cache)
{
    unsigned base;

#ifdef CI20
    if (size != 0 && cache == PMAP_CACHE_CACHED &&
        paddr >= CI20_HIGH_RAM_PHYS_START &&
        paddr < CI20_HIGH_RAM_PHYS_START + CI20_HIGH_RAM_BYTES &&
        size - 1 < CI20_HIGH_RAM_PHYS_START + CI20_HIGH_RAM_BYTES - paddr)
        return (void *)(CI20_HIGH_RAM_VADDR_START +
            (paddr - CI20_HIGH_RAM_PHYS_START));
#endif
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

#ifndef CI20
static void
pmap_md_icache_invalidate(unsigned address)
{
    asm volatile ("cache 0x10, 0(%0)" :: "r" (address) : "memory");
}
#endif

#ifdef N64
int
pmap_md_icache_tag_diagnostics(vm_vaddr_t vaddr, unsigned *index_address,
    unsigned *taglo, unsigned *taghi)
{
    unsigned address;
    unsigned saved_taghi;
    unsigned saved_taglo;
    int status;

    if (index_address == 0 || taglo == 0 || taghi == 0)
        return EINVAL;

    address = PMAP_MD_KSEG0_BASE |
        (vaddr & (PMAP_MD_ICACHE_SIZE - 1));
    address &= ~(PMAP_MD_ICACHE_LINE - 1);
    status = mips_intr_disable();
    saved_taglo = mips_read_c0_register(C0_TAGLO, 0);
    saved_taghi = mips_read_c0_register(C0_TAGHI, 0);
    asm volatile (
        "cache 0x04, 0(%0)\n"
        "nop"
        : : "r" (address) : "memory");
    *taglo = mips_read_c0_register(C0_TAGLO, 0);
    *taghi = mips_read_c0_register(C0_TAGHI, 0);
    mips_write_c0_register(C0_TAGLO, 0, saved_taglo);
    mips_write_c0_register(C0_TAGHI, 0, saved_taghi);
    mips_intr_restore(status);
    *index_address = address;
    return 0;
}
#endif

int
pmap_md_range_sync(vm_paddr_t paddr, vm_size_t size, unsigned operations)
{
    unsigned address;
    unsigned end;
    void *mapping;

    mapping = pmap_md_direct_map(paddr, size, PMAP_CACHE_CACHED);
    if (mapping == 0 || operations == 0 ||
        (operations & ~(PMAP_SYNC_DATA | PMAP_SYNC_INSTRUCTION |
        PMAP_INVALIDATE_DATA)) != 0 ||
        (operations & (PMAP_SYNC_DATA | PMAP_INVALIDATE_DATA)) ==
        (PMAP_SYNC_DATA | PMAP_INVALIDATE_DATA))
        return EINVAL;
    pmap_md_sync();
    if ((operations & PMAP_SYNC_DATA) != 0) {
        /*
         * Cached MIPS user mappings obey pmap_cache_alias_mask(), so their
         * virtual cache index is identical to this KSEG0 direct-map index.
         * Hit operations therefore synchronize exactly the requested
         * physical range without sweeping unrelated cache contents.
         */
        address = (unsigned)mapping & ~(PMAP_MD_DCACHE_LINE - 1);
        end = ((unsigned)mapping + size + PMAP_MD_DCACHE_LINE - 1) &
            ~(PMAP_MD_DCACHE_LINE - 1);
        for (; address < end; address += PMAP_MD_DCACHE_LINE)
            pmap_md_dcache_writeback_invalidate(address);
        pmap_md_sync();
    }
    if ((operations & PMAP_INVALIDATE_DATA) != 0) {
        address = (unsigned)mapping & ~(PMAP_MD_DCACHE_LINE - 1);
        end = ((unsigned)mapping + size + PMAP_MD_DCACHE_LINE - 1) &
            ~(PMAP_MD_DCACHE_LINE - 1);
        for (; address < end; address += PMAP_MD_DCACHE_LINE)
            pmap_md_dcache_invalidate(address);
        pmap_md_sync();
    }
    if ((operations & PMAP_SYNC_INSTRUCTION) != 0) {
#ifdef CI20
        /* A KSEG0 hit cannot match an XBurst user-VA I-cache tag. */
        pmap_md_icache_index_invalidate();
#else
        address = (unsigned)mapping & ~(PMAP_MD_ICACHE_LINE - 1);
        end = ((unsigned)mapping + size + PMAP_MD_ICACHE_LINE - 1) &
            ~(PMAP_MD_ICACHE_LINE - 1);
        for (; address < end; address += PMAP_MD_ICACHE_LINE)
            pmap_md_icache_invalidate(address);
        pmap_md_sync();
#endif
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
