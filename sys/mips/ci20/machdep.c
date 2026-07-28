#include <sys/param.h>
#include <sys/buf.h>
#include <sys/fs.h>
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <machine/console.h>
#include <machine/io.h>
#include <vm/vmspace.h>
#include <machine/layout.h>

dev_t pipedev;
extern int boothowto;
extern int waittime;

extern char _end[];
extern char _mips_exception_vector[];
extern char _mips_exception_vector_end[];
extern char _mips_tlb_refill_vector[];
extern char _mips_tlb_refill_vector_end[];

#define CI20_TLB_ENTRIES        32
#define MIPS_USER_TLB_INDEX     0
#define CI20_HIGH_TLB_INDEX     MIPS_USER_TLB_PAIRS
#define CI20_HIGH_TLB_ENTRIES   2
#define CI20_WIRED_ENTRIES      (MIPS_USER_TLB_PAIRS + \
                                  CI20_HIGH_TLB_ENTRIES)
#define MIPS_VECTOR_TLB_REFILL  0x00000000u
#define MIPS_VECTOR_XTLB_REFILL 0x00000080u
#define MIPS_VECTOR_CACHE_ERROR 0x00000100u
#define MIPS_VECTOR_GENERAL     0x00000180u
#define MIPS_VECTOR_INTERRUPT   0x00000200u
#define MIPS_DCACHE_LINE        32u
#define MIPS_ICACHE_LINE        32u

static void
early_puts(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n')
            mips_console_putc('\r');
        mips_console_putc(*s++);
    }
}

static void
early_put_hex32(unsigned value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    early_puts("0x");
    for (shift = 28; shift >= 0; shift -= 4)
        mips_console_putc(digits[(value >> (unsigned)shift) & 0x0f]);
}

static void
mips_sync_memory(void)
{
    asm volatile ("sync" ::: "memory");
}

static void
mips_cache_hit_writeback_invalidate_d(unsigned addr)
{
    asm volatile ("cache 0x15, 0(%0)" :: "r" (addr) : "memory");
}

static void
mips_cache_hit_invalidate_i(unsigned addr)
{
    asm volatile ("cache 0x10, 0(%0)" :: "r" (addr) : "memory");
}

void
mips_sync_user_icache(void)
{
    unsigned addr;

    mips_sync_memory();
    for (addr = USER_DATA_START & ~(MIPS_DCACHE_LINE - 1);
        addr < USER_DATA_END; addr += MIPS_DCACHE_LINE)
        mips_cache_hit_writeback_invalidate_d(addr);
    mips_sync_memory();
    for (addr = USER_DATA_START & ~(MIPS_ICACHE_LINE - 1);
        addr < USER_DATA_END; addr += MIPS_ICACHE_LINE)
        mips_cache_hit_invalidate_i(addr);
    mips_sync_memory();
}

static void
mips_sync_instruction_range(unsigned start, unsigned end)
{
    unsigned addr;

    mips_sync_memory();
    for (addr = start & ~(MIPS_ICACHE_LINE - 1); addr < end;
        addr += MIPS_ICACHE_LINE)
        mips_cache_hit_invalidate_i(addr);
    mips_sync_memory();
}

static void
mips_install_vector(unsigned phys, const unsigned *src, unsigned bytes)
{
    volatile unsigned *dst = (volatile unsigned *)MIPS_PHYS_TO_KSEG1(phys);
    unsigned words = (bytes + sizeof(unsigned) - 1) / sizeof(unsigned);
    unsigned i;

    for (i = 0; i < words; ++i)
        dst[i] = src[i];
    mips_sync_instruction_range(MIPS_KSEG0_BASE + phys,
        MIPS_KSEG0_BASE + phys + words * sizeof(unsigned));
}

static void
mips_install_exception_vectors(void)
{
    mips_install_vector(MIPS_VECTOR_TLB_REFILL,
        (const unsigned *)_mips_tlb_refill_vector,
        _mips_tlb_refill_vector_end - _mips_tlb_refill_vector);
    mips_install_vector(MIPS_VECTOR_XTLB_REFILL,
        (const unsigned *)_mips_exception_vector,
        _mips_exception_vector_end - _mips_exception_vector);
    mips_install_vector(MIPS_VECTOR_CACHE_ERROR,
        (const unsigned *)_mips_exception_vector,
        _mips_exception_vector_end - _mips_exception_vector);
    mips_install_vector(MIPS_VECTOR_GENERAL,
        (const unsigned *)_mips_exception_vector,
        _mips_exception_vector_end - _mips_exception_vector);
    mips_install_vector(MIPS_VECTOR_INTERRUPT,
        (const unsigned *)_mips_exception_vector,
        _mips_exception_vector_end - _mips_exception_vector);
}

static unsigned
mips_tlb_entrylo_cache(unsigned phys, unsigned cache)
{
    return (phys >> 6) |
        (cache << TLB_ENTRYLO_C_SHIFT) |
        TLB_ENTRYLO_D | TLB_ENTRYLO_V | TLB_ENTRYLO_G;
}

static unsigned
mips_tlb_entrylo(unsigned phys)
{
    return mips_tlb_entrylo_cache(phys, TLB_CACHE_CNC);
}

static void
mips_tlb_init(void)
{
    unsigned i;

    mips_write_c0_register(C0_WIRED, 0, 0);
    for (i = 0; i < CI20_TLB_ENTRIES; ++i) {
        mips_tlb_write_indexed(i, TLB_PAGEMASK_4K,
            mips_tlb_invalid_entryhi(i), 0, 0);
    }

    mips_tlb_write_indexed(MIPS_USER_TLB_INDEX, TLB_PAGEMASK_1M,
        USER_DATA_START,
        mips_tlb_entrylo(MIPS_USER_PHYS_START),
        mips_tlb_entrylo(MIPS_USER_PHYS_START + MIPS_USER_TLB_PAGE_SIZE));
    mips_tlb_write_indexed(MIPS_USER_TLB_INDEX + 1, TLB_PAGEMASK_1M,
        USER_DATA_START + MIPS_USER_TLB_PAIR_SIZE,
        mips_tlb_entrylo(MIPS_USER_PHYS_START + MIPS_USER_TLB_PAIR_SIZE),
        mips_tlb_entrylo(MIPS_USER_PHYS_START + MIPS_USER_TLB_PAIR_SIZE +
            MIPS_USER_TLB_PAGE_SIZE));
    /*
     * XBurst1 only directly maps the first 512 MiB through KSEG0/KSEG1.
     * Keep the discontiguous 768 MiB Ci20 high bank permanently cached in
     * KSEG2 using 256 MiB pages.  The second half of the last pair is
     * deliberately invalid because the board ends at physical 0x60000000.
     */
    mips_tlb_write_indexed(CI20_HIGH_TLB_INDEX, TLB_PAGEMASK_256M,
        CI20_HIGH_RAM_VADDR_START,
        mips_tlb_entrylo_cache(CI20_HIGH_RAM_PHYS_START, TLB_CACHE_CNC),
        mips_tlb_entrylo_cache(CI20_HIGH_RAM_PHYS_START + CI20_SIZE_256M,
            TLB_CACHE_CNC));
    mips_tlb_write_indexed(CI20_HIGH_TLB_INDEX + 1, TLB_PAGEMASK_256M,
        CI20_HIGH_RAM_VADDR_START + 2 * CI20_SIZE_256M,
        mips_tlb_entrylo_cache(CI20_HIGH_RAM_PHYS_START +
            2 * CI20_SIZE_256M, TLB_CACHE_CNC), 0);
    mips_write_c0_register(C0_WIRED, 0, CI20_WIRED_ENTRIES);
}

unsigned
pmap_md_legacy_user_entries(void)
{
    return MIPS_USER_TLB_PAIRS;
}

void
startup(void)
{
    unsigned status;

    early_puts("ReBSD Ci20 kernel entry\n");
    mips_install_exception_vectors();
    mips_tlb_init();

    status = mips_read_c0_register(C0_STATUS, 0);
    status &= ~(ST_IE | ST_EXL | ST_ERL | ST_KSU | ST_IM | ST_BEV);
    status |= ST_IM2;
    mips_write_c0_register(C0_STATUS, 0, status);

    physmem = CI20_RAM_SIZE;
    early_puts("ram size=");
    early_put_hex32(physmem);
    early_puts("\n");
    boothowto = RB_RDONLY;
}

void
idle(void)
{
    int s;

    noproc = 1;
    /*
     * Do not execute WAIT on JZ4780/XBurst1 here.  The scheduler enters
     * idle with IE clear, and an interrupt which becomes pending around the
     * spl0()/WAIT transition can leave the core asleep with Cause.IP2 set.
     * Re-entering this short interrupt-enabled window from swtch() is less
     * power-efficient, but cannot lose the TCU wakeup which drives all
     * scheduling and deferred USB work.
     */
    s = spl0();
    asm volatile ("nop" ::: "memory");
    splx(s);
}

void
led_control(int mask, int on)
{
    (void)mask;
    (void)on;
}

int
baduaddr(caddr_t addr)
{
    struct vmspace *vmspace = vmspace_current();

    return vmspace == 0 || vmspace_check(vmspace, (vm_vaddr_t)addr, 1,
        VM_PROT_NONE) != 0;
}

int
badkaddr(caddr_t addr)
{
    unsigned a = (unsigned)addr;

    return a < KERNEL_DATA_START || a >= (unsigned)&_end;
}

int
copyout(caddr_t from, caddr_t to, u_int nbytes)
{
    struct vmspace *vmspace;
    unsigned context;

    if (nbytes == 0)
        return 0;
    vmspace = vmspace_current();
    if (vmspace == 0)
        return EFAULT;
    context = VM_FAULT_COPY;
    if (!mips_in_interrupt() && mips_intr_enabled())
        context |= VM_FAULT_CAN_SLEEP;
    return vmspace_write_context(vmspace, (vm_vaddr_t)to, from, nbytes,
        context);
}

int
copyin(caddr_t from, caddr_t to, u_int nbytes)
{
    struct vmspace *vmspace;
    unsigned context;

    if (nbytes == 0)
        return 0;
    vmspace = vmspace_current();
    if (vmspace == 0)
        return EFAULT;
    context = VM_FAULT_COPY;
    if (!mips_in_interrupt() && mips_intr_enabled())
        context |= VM_FAULT_CAN_SLEEP;
    return vmspace_read_context(vmspace, (vm_vaddr_t)from, to, nbytes,
        context);
}

void
boot(dev_t dev, int howto)
{
    if ((howto & RB_NOSYNC) == 0 && waittime < 0 && bfreelist[0].b_forw) {
        struct fs *fp;
        struct buf *bp;
        int iter, nbusy;

        fp = getfs(rootdev);
        if (fp && !fp->fs_ronly)
            fp->fs_fmod = 1;
        waittime = 0;
        printf("syncing disks... ");
        (void)splnet();
        sync();
        for (iter = 0; iter < 20; iter++) {
            nbusy = 0;
            for (bp = &buf[NBUF]; --bp >= buf; )
                if (bp->b_flags & B_BUSY)
                    nbusy++;
            if (nbusy == 0)
                break;
            printf("%d ", nbusy);
            udelay(40000L * iter);
        }
        printf("done\n");
    }

    (void)splhigh();
    if (howto & RB_HALT)
        printf("halted\n");
    else
        printf("reboot requested, halted\n");
    for (;;)
        asm volatile ("wait");
}
