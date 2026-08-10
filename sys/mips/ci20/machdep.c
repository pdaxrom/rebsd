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

extern int boothowto;

extern char _end[];
extern char _mips_exception_vector[];
extern char _mips_exception_vector_end[];
extern char _mips_tlb_refill_vector[];
extern char _mips_tlb_refill_vector_end[];

#define CI20_TLB_ENTRIES        32
#define MIPS_USER_TLB_INDEX     0
#define MIPS_VECTOR_TLB_REFILL  0x00000000u
#define MIPS_VECTOR_XTLB_REFILL 0x00000080u
#define MIPS_VECTOR_CACHE_ERROR 0x00000100u
#define MIPS_VECTOR_GENERAL     0x00000180u
#define MIPS_VECTOR_INTERRUPT   0x00000200u
#define MIPS_DCACHE_LINE        32u
#define MIPS_ICACHE_LINE        32u

#define CI20_TCU_BASE           0xb0002000u
#define CI20_WDT_TDR            0x00u
#define CI20_WDT_TCER           0x04u
#define CI20_WDT_TCNT           0x08u
#define CI20_WDT_TCSR           0x0cu
#define CI20_TCU_TSCR           0x3cu
#define CI20_WDT_TCSR_EXT_EN    (1u << 2)
#define CI20_WDT_TCSR_DIV4      (1u << 3)
#define CI20_WDT_CLOCK_STOP     (1u << 16)
#define CI20_WDT_ENABLE         1u
#define CI20_WDT_4MS_TICKS      48000u

extern int ci20_rtc_poweroff(void);

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
ci20_watchdog_reboot(void)
{
    volatile unsigned char *wdt_enable;
    volatile unsigned short *wdt_count;
    volatile unsigned short *wdt_data;
    volatile unsigned short *wdt_status;
    volatile unsigned *tcu_stop_clear;

    wdt_data = (volatile unsigned short *)(CI20_TCU_BASE + CI20_WDT_TDR);
    wdt_enable = (volatile unsigned char *)(CI20_TCU_BASE + CI20_WDT_TCER);
    wdt_count = (volatile unsigned short *)(CI20_TCU_BASE + CI20_WDT_TCNT);
    wdt_status = (volatile unsigned short *)(CI20_TCU_BASE + CI20_WDT_TCSR);
    tcu_stop_clear = (volatile unsigned *)(CI20_TCU_BASE + CI20_TCU_TSCR);

    /* Official Ci20 U-Boot contract: 48 MHz EXTAL / 4, reset after 4 ms. */
    *wdt_status = CI20_WDT_TCSR_DIV4 | CI20_WDT_TCSR_EXT_EN;
    *wdt_count = 0;
    *wdt_data = CI20_WDT_4MS_TICKS;
    *tcu_stop_clear = CI20_WDT_CLOCK_STOP;
    *wdt_enable = CI20_WDT_ENABLE;
    mips_sync_memory();
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
    unsigned entrylo0;
    unsigned entrylo1;
    unsigned i;
    unsigned offset;
    unsigned remaining;

#if CI20_WIRED_ENTRIES >= CI20_TLB_ENTRIES
#error Ci20 wired mappings leave no replaceable TLB entries
#endif

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
     * XBurst1 implements the MIPS 4KE TLB contract, whose largest legal
     * page is 16 MiB.  Map the discontiguous high bank with 32 MiB pairs;
     * larger PageMask values have undefined hardware behaviour.
     */
    for (i = 0; i < CI20_HIGH_TLB_ENTRIES; ++i) {
        offset = i * CI20_HIGH_TLB_PAIR_SIZE;
        remaining = CI20_HIGH_RAM_BYTES - offset;
        entrylo0 = mips_tlb_entrylo_cache(CI20_HIGH_RAM_PHYS_START +
            offset, TLB_CACHE_CNC);
        entrylo1 = remaining > CI20_HIGH_TLB_PAGE_SIZE ?
            mips_tlb_entrylo_cache(CI20_HIGH_RAM_PHYS_START + offset +
                CI20_HIGH_TLB_PAGE_SIZE, TLB_CACHE_CNC) : 0;
        mips_tlb_write_indexed(CI20_HIGH_TLB_INDEX + i,
            TLB_PAGEMASK_16M, CI20_HIGH_RAM_VADDR_START + offset,
            entrylo0, entrylo1);
    }
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
    int error;

    boot_sync_filesystems(howto);

    (void)splhigh();
    if (howto & RB_POWEROFF) {
        printf("powering off\n");
        error = ci20_rtc_poweroff();
        if (error != 0)
            printf("poweroff failed, error=%d; halted\n", error);
    } else if (howto & RB_HALT) {
        printf("halted\n");
    } else {
        printf("rebooting\n");
        ci20_watchdog_reboot();
    }
    for (;;)
        asm volatile ("wait");
}
