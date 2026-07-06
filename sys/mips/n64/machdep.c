#include <sys/param.h>
#include <sys/buf.h>
#include <sys/fs.h>
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <machine/io.h>
#include <machine/console.h>
#include <machine/n64.h>
#include <machine/n64int.h>
#ifdef N64CART_ENABLED
#include <machine/n64cart_flash.h>
#endif
#include <machine/video.h>

dev_t pipedev;
extern int boothowto;
extern int waittime;

extern char _end[];
extern char _mips_exception_vector[];
extern char _mips_exception_vector_end[];

#define N64_TLB_ENTRIES         32
#define N64_USER_TLB_INDEX      0
#define N64_FB_TLB_INDEX        (N64_USER_TLB_INDEX + N64_USER_TLB_PAIRS)
#define N64_VECTOR_TLB_REFILL   0x00000000u
#define N64_VECTOR_XTLB_REFILL  0x00000080u
#define N64_VECTOR_CACHE_ERROR  0x00000100u
#define N64_VECTOR_GENERAL      0x00000180u
#define N64_VECTOR_INTERRUPT    0x00000200u
#define N64_DCACHE_LINE         16u
#define N64_ICACHE_LINE         32u

static unsigned
n64_user_tlb_pairs_for_rdram(unsigned rdram)
{
    return rdram >= N64_RDRAM_SIZE_8M ?
        N64_USER_TLB_PAIRS_8M : N64_USER_TLB_PAIRS_4M;
}

unsigned
n64_user_maxmem(void)
{
    return n64_user_tlb_pairs_for_rdram(n64_rdram_size()) *
        N64_USER_TLB_PAIR_SIZE;
}

unsigned
n64_user_data_end(void)
{
    return USER_DATA_START + n64_user_maxmem();
}

static void
early_puts(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n')
            n64_console_putc('\r');
        n64_console_putc(*s++);
    }
}

static void
early_put_hex32(unsigned value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    early_puts("0x");
    for (shift = 28; shift >= 0; shift -= 4)
        n64_console_putc(digits[(value >> (unsigned)shift) & 0x0f]);
}

static void
n64_sync_memory(void)
{
    asm volatile ("sync" ::: "memory");
}

static void
n64_cache_hit_writeback_invalidate_d(unsigned addr)
{
    asm volatile ("cache 0x15, 0(%0)" :: "r" (addr) : "memory");
}

static void
n64_cache_hit_invalidate_i(unsigned addr)
{
    asm volatile ("cache 0x10, 0(%0)" :: "r" (addr) : "memory");
}

void
mips_sync_user_icache(void)
{
    unsigned addr;
    unsigned end = USER_DATA_END;

    n64_sync_memory();
    for (addr = USER_DATA_START & ~(N64_DCACHE_LINE - 1);
        addr < end; addr += N64_DCACHE_LINE)
        n64_cache_hit_writeback_invalidate_d(addr);
    n64_sync_memory();
    for (addr = USER_DATA_START & ~(N64_ICACHE_LINE - 1);
        addr < end; addr += N64_ICACHE_LINE)
        n64_cache_hit_invalidate_i(addr);
    n64_sync_memory();
}

static void
n64_sync_instruction_range(unsigned start, unsigned end)
{
    unsigned addr;

    n64_sync_memory();
    for (addr = start & ~(N64_ICACHE_LINE - 1); addr < end;
        addr += N64_ICACHE_LINE)
        n64_cache_hit_invalidate_i(addr);
    n64_sync_memory();
}

static void
n64_install_vector(unsigned phys)
{
    volatile unsigned *dst = (volatile unsigned *)N64_PHYS_TO_KSEG1(phys);
    const unsigned *src = (const unsigned *)_mips_exception_vector;
    unsigned bytes = _mips_exception_vector_end - _mips_exception_vector;
    unsigned words = (bytes + sizeof(unsigned) - 1) / sizeof(unsigned);
    unsigned i;

    for (i = 0; i < words; ++i)
        dst[i] = src[i];
    n64_sync_instruction_range(N64_KSEG0_BASE + phys,
        N64_KSEG0_BASE + phys + words * sizeof(unsigned));
}

static void
n64_install_exception_vectors(void)
{
    n64_install_vector(N64_VECTOR_TLB_REFILL);
    n64_install_vector(N64_VECTOR_XTLB_REFILL);
    n64_install_vector(N64_VECTOR_CACHE_ERROR);
    n64_install_vector(N64_VECTOR_GENERAL);
    n64_install_vector(N64_VECTOR_INTERRUPT);
}

static unsigned
n64_tlb_entrylo_cache(unsigned phys, unsigned cache)
{
    return (phys >> 6) |
        (cache << TLB_ENTRYLO_C_SHIFT) |
        TLB_ENTRYLO_D | TLB_ENTRYLO_V | TLB_ENTRYLO_G;
}

static unsigned
n64_tlb_entrylo(unsigned phys)
{
    return n64_tlb_entrylo_cache(phys, TLB_CACHE_CNC);
}

static unsigned
n64_fb_tlb_entries(unsigned rdram)
{
#ifndef VIDEO_ENABLED
    (void)rdram;
    return 0;
#else
    unsigned bytes = rdram >= N64_RDRAM_SIZE_8M ?
        N64_EXPANSION_FB_RESERVED_BYTES : N64_BASE_FB_RESERVED_BYTES;

    return bytes / N64_VIDEO_TLB_PAIR_SIZE;
#endif
}

static unsigned
n64_fb_tlb_phys(unsigned rdram)
{
    return rdram >= N64_RDRAM_SIZE_8M ?
        N64_EXPANSION_FB_PHYS_START : N64_BASE_FB_PHYS_START;
}

static void
n64_tlb_init(void)
{
    unsigned rdram = n64_rdram_size();
    unsigned user_pairs = n64_user_tlb_pairs_for_rdram(rdram);
    unsigned fb_phys = n64_fb_tlb_phys(rdram);
    unsigned fb_entries = n64_fb_tlb_entries(rdram);
    unsigned i;

    mips_write_c0_register(C0_WIRED, 0, 0);
    for (i = 0; i < N64_TLB_ENTRIES; ++i) {
        mips_tlb_write_indexed(i, TLB_PAGEMASK_4K,
            0x40000000u + i * 0x2000u, 0, 0);
    }

    for (i = 0; i < user_pairs; ++i) {
        unsigned vaddr = USER_DATA_START + i * N64_USER_TLB_PAIR_SIZE;
        unsigned phys = N64_USER_PHYS_START + i * N64_USER_TLB_PAIR_SIZE;

        mips_tlb_write_indexed(N64_USER_TLB_INDEX + i, TLB_PAGEMASK_1M,
            vaddr, n64_tlb_entrylo(phys),
            n64_tlb_entrylo(phys + N64_USER_TLB_PAGE_SIZE));
    }
    for (i = 0; i < fb_entries; ++i) {
        unsigned vaddr = N64_FB_USER_VADDR_START +
            i * N64_VIDEO_TLB_PAIR_SIZE;
        unsigned phys = fb_phys + i * N64_VIDEO_TLB_PAIR_SIZE;

        mips_tlb_write_indexed(N64_FB_TLB_INDEX + i,
            TLB_PAGEMASK_64K, vaddr,
            n64_tlb_entrylo_cache(phys, TLB_CACHE_UNCACHED),
            n64_tlb_entrylo_cache(phys + N64_VIDEO_TLB_PAGE_SIZE,
                TLB_CACHE_UNCACHED));
    }
    mips_write_c0_register(C0_WIRED, 0, N64_FB_TLB_INDEX + fb_entries);
}

void
startup(void)
{
    unsigned status;

    early_puts("ReBSD N64 kernel entry\n");
    n64_install_exception_vectors();
    n64_tlb_init();
    n64_interrupt_init();
#ifdef VIDEO_ENABLED
    n64_video_intr_enable();
#endif

    status = mips_read_c0_register(C0_STATUS, 0);
    status &= ~(ST_IE | ST_EXL | ST_ERL | ST_KSU | ST_BEV);
    status |= ST_IM2 | ST_IM7;
    mips_write_c0_register(C0_STATUS, 0, status);

    physmem = n64_rdram_size();
    early_puts("rdram size=");
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
    unsigned ticks = (N64_COUNT_KHZ * usec + 999u) / 1000u;

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

    if (a >= USER_DATA_START && a < USER_DATA_END)
        return 0;
#ifdef VIDEO_ENABLED
    if (n64_video_useraddr_valid(addr))
        return 0;
#endif
    return 1;
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
    void (*stage0)(void) = (void (*)(void))N64_STAGE0_VADDR;

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
#ifdef N64CART_ENABLED
    n64cart_flash_shutdown();
#endif
    n64_interrupt_shutdown();

    if (howto & RB_HALT) {
        printf("halted\n");
        for (;;)
            asm volatile ("wait");
    }

    printf("restarting through stage0\n");
    n64_sync_memory();
    stage0();
    for (;;)
        asm volatile ("wait");
}
