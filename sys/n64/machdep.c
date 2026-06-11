#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <machine/io.h>
#include <machine/n64.h>
#include <machine/n64cart_uart.h>

extern dev_t swapdev;
dev_t pipedev;
extern int boothowto;

extern char _end[];
extern char _n64_exception_vector[];
extern char _n64_exception_vector_end[];

#define N64_TLB_ENTRIES         32
#define N64_USER_TLB_INDEX      0
#define N64_USER_TLB_WIRED      1
#define N64_VECTOR_TLB_REFILL   0x00000000u
#define N64_VECTOR_XTLB_REFILL  0x00000080u
#define N64_VECTOR_CACHE_ERROR  0x00000100u
#define N64_VECTOR_GENERAL      0x00000180u
#define N64_VECTOR_INTERRUPT    0x00000200u
#define N64_DCACHE_LINE         16u
#define N64_ICACHE_LINE         32u

static void
early_puts(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n')
            n64cart_uart_putc('\r');
        n64cart_uart_putc(*s++);
    }
}

static void
early_put_hex32(unsigned value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    early_puts("0x");
    for (shift = 28; shift >= 0; shift -= 4)
        n64cart_uart_putc(digits[(value >> (unsigned)shift) & 0x0f]);
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
n64_sync_user_icache(void)
{
    unsigned addr;

    n64_sync_memory();
    for (addr = USER_DATA_START & ~(N64_DCACHE_LINE - 1);
        addr < USER_DATA_END; addr += N64_DCACHE_LINE)
        n64_cache_hit_writeback_invalidate_d(addr);
    n64_sync_memory();
    for (addr = USER_DATA_START & ~(N64_ICACHE_LINE - 1);
        addr < USER_DATA_END; addr += N64_ICACHE_LINE)
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
    const unsigned *src = (const unsigned *)_n64_exception_vector;
    unsigned bytes = _n64_exception_vector_end - _n64_exception_vector;
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
n64_tlb_entrylo(unsigned phys)
{
    return (phys >> 6) |
        (TLB_CACHE_CNC << TLB_ENTRYLO_C_SHIFT) |
        TLB_ENTRYLO_D | TLB_ENTRYLO_V | TLB_ENTRYLO_G;
}

static void
n64_tlb_init(void)
{
    unsigned i;

    mips_write_c0_register(C0_WIRED, 0, 0);
    for (i = 0; i < N64_TLB_ENTRIES; ++i) {
        mips_tlb_write_indexed(i, TLB_PAGEMASK_4K,
            0x40000000u + i * 0x2000u, 0, 0);
    }

    mips_tlb_write_indexed(N64_USER_TLB_INDEX, TLB_PAGEMASK_1M,
        USER_DATA_START,
        n64_tlb_entrylo(N64_USER_PHYS_START),
        n64_tlb_entrylo(N64_USER_PHYS_START + 0x100000u));
    mips_write_c0_register(C0_WIRED, 0, N64_USER_TLB_WIRED);
}

void
startup(void)
{
    unsigned status;

    early_puts("RetroBSD N64 kernel entry\n");
    n64_install_exception_vectors();
    n64_tlb_init();

    status = mips_read_c0_register(C0_STATUS, 0);
    status &= ~(ST_IE | ST_EXL | ST_ERL | ST_KSU | ST_BEV);
    status |= ST_IM7;
    mips_write_c0_register(C0_STATUS, 0, status);

    physmem = n64_rdram_size();
    early_puts("rdram size=");
    early_put_hex32(physmem);
    early_puts("\n");
    pipedev = swapdev;
    boothowto = RB_RDONLY;
}

void
idle(void)
{
    for (;;)
        asm volatile ("wait");
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
    printf("reboot requested: dev=%d,%d howto=%#x\n",
        major(dev), minor(dev), howto);
    for (;;)
        ;
}
