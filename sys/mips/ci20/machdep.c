#include <sys/param.h>
#include <sys/buf.h>
#include <sys/fs.h>
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <machine/console.h>
#include <machine/io.h>
#include <machine/layout.h>

dev_t pipedev;
extern int boothowto;
extern int waittime;

extern char _end[];
extern char _mips_exception_vector[];
extern char _mips_exception_vector_end[];

#define CI20_TLB_ENTRIES        32
#define MIPS_USER_TLB_INDEX     0
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
mips_install_vector(unsigned phys)
{
    volatile unsigned *dst = (volatile unsigned *)MIPS_PHYS_TO_KSEG1(phys);
    const unsigned *src = (const unsigned *)_mips_exception_vector;
    unsigned bytes = _mips_exception_vector_end - _mips_exception_vector;
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
    mips_install_vector(MIPS_VECTOR_TLB_REFILL);
    mips_install_vector(MIPS_VECTOR_XTLB_REFILL);
    mips_install_vector(MIPS_VECTOR_CACHE_ERROR);
    mips_install_vector(MIPS_VECTOR_GENERAL);
    mips_install_vector(MIPS_VECTOR_INTERRUPT);
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
            0x40000000u + i * 0x2000u, 0, 0);
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
    mips_write_c0_register(C0_WIRED, 0, MIPS_USER_TLB_PAIRS);
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
    s = spl0();
    asm volatile ("wait");
    splx(s);
}

void
udelay(unsigned usec)
{
    unsigned start = mips_read_c0_register(C0_COUNT, 0);
    unsigned ticks = (MIPS_COUNT_KHZ * usec + 999u) / 1000u;

    while ((unsigned)(mips_read_c0_register(C0_COUNT, 0) - start) < ticks)
        ;
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
    unsigned a = (unsigned)addr;

    return a < USER_DATA_START || a >= USER_DATA_END;
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
    unsigned start = (unsigned)to;
    unsigned end = start + nbytes - 1;

    if (nbytes == 0)
        return 0;
    if (end < start || baduaddr((caddr_t)start) || baduaddr((caddr_t)end))
        return EFAULT;
    bcopy(from, to, nbytes);
    return 0;
}

int
copyin(caddr_t from, caddr_t to, u_int nbytes)
{
    unsigned start = (unsigned)from;
    unsigned end = start + nbytes - 1;

    if (nbytes == 0)
        return 0;
    if (end < start || baduaddr((caddr_t)start) || baduaddr((caddr_t)end))
        return EFAULT;
    bcopy(from, to, nbytes);
    return 0;
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
