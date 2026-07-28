/*
 * Common MIPS exception handling.
 */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vm.h>
#include <vm/pmap.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vmspace.h>
#include <machine/io.h>
#include <machine/fpu.h>
#ifdef N64
#include <machine/console.h>
#include <machine/n64.h>
#include <machine/ramswap.h>
#include <mips/common/zswap.h>
#ifdef N64_USB_GDB
#include <machine/n64gdb.h>
#endif
#ifdef N64_RESET_DUMP
#include <machine/n64reset.h>
#endif
#ifdef N64CART_ENABLED
#include <machine/n64cart_uart.h>
#endif
#ifdef INPUT_ENABLED
#include <machine/joybus.h>
#endif
#include <machine/n64int.h>
#else
#include <machine/layout.h>
#endif

#define USER            1
#define MIPS_CAUSE_CE1   0x10000000u
#define MIPS_CAUSE_IP2   0x00000400u
#define MIPS_CAUSE_IP3   0x00000800u
#define MIPS_CAUSE_IP4   0x00001000u
#define MIPS_CAUSE_IP7   0x00008000u

#ifdef N64
#define MIPS_TIMER_COUNT_KHZ    N64_COUNT_KHZ
#else
#define MIPS_TIMER_COUNT_KHZ    MIPS_COUNT_KHZ
#endif

static volatile unsigned mips_last_clock_count;
static volatile int mips_last_clock_count_valid;
static volatile unsigned long mips_timer_irq_count;
static volatile unsigned long mips_timer_late_count;
static volatile unsigned long mips_timer_late_last_us;
static volatile unsigned long mips_timer_late_max_us;
static volatile unsigned long mips_timer_late_last_tick;
static volatile unsigned long mips_timer_late_max_tick;
static volatile unsigned long mips_timer_clock_last_us;
static volatile unsigned long mips_timer_clock_max_us;
static volatile unsigned mips_interrupt_depth;
static unsigned long mips_systrace_sequence;

#if defined(N64_USB_GDB) || defined(N64_RESET_DUMP)
extern char n64_gdb_emergency_stack[];
extern char n64_gdb_emergency_stack_top[];

static int
n64_emergency_frame(const int *frame)
{
    return (unsigned)frame >= (unsigned)n64_gdb_emergency_stack &&
        (unsigned)frame < (unsigned)n64_gdb_emergency_stack_top;
}
#endif

#if defined(N64_TRACE) || defined(MIPS_TRACE)
static unsigned mips_user_fault_trace_count;

static void
mips_trace_user_fault(const char *kind, int *frame, unsigned badvaddr)
{
    if (mips_user_fault_trace_count >= 32)
        return;
    ++mips_user_fault_trace_count;
    printf("user fault: %s pc=%08x address=%08x pid=%d comm=%s\n",
        kind, frame[FRAME_PC], badvaddr,
        u.u_procp ? u.u_procp->p_pid : -1, u.u_comm);
}
#endif

#ifdef N64
static int
n64_user_fault_read_word(unsigned paddr, unsigned *cached, unsigned *uncached)
{
    unsigned rdram;

    if (cached == 0 || uncached == 0 || (paddr & 3u) != 0)
        return EINVAL;
    rdram = n64_rdram_size();
    if (paddr >= rdram || sizeof(unsigned) > rdram - paddr)
        return EFAULT;
    *cached = *(volatile unsigned *)N64_PHYS_TO_KSEG0(paddr);
    *uncached = *(volatile unsigned *)N64_PHYS_TO_KSEG1(paddr);
    return 0;
}

static int
n64_user_fault_tlb_paddr(const struct pmap_tlb_diagnostics *tlb,
    unsigned vaddr, unsigned *paddr)
{
    unsigned entrylo;
    unsigned page;

    if (tlb == 0 || paddr == 0 || !tlb->ptd_hardware_found ||
        tlb->ptd_hardware_pagemask != TLB_PAGEMASK_4K)
        return EINVAL;
    entrylo = (vaddr & VM_PAGE_SIZE) != 0 ?
        tlb->ptd_hardware_entrylo1 : tlb->ptd_hardware_entrylo0;
    if ((entrylo & TLB_ENTRYLO_V) == 0)
        return ENOENT;
    page = (entrylo & ~0x3fu) << 6;
    *paddr = page | (vaddr & VM_PAGE_MASK);
    return 0;
}

static void
n64_dump_user_mapping(const char *label, unsigned vaddr)
{
    struct pmap_tlb_diagnostics tlb;
    unsigned cached_word;
    unsigned expected_paddr;
    unsigned ignored_cached;
    unsigned tlb_paddr;
    unsigned tlb_word;
    unsigned uncached_word;
    int expected_word_error;
    int pmap_error;
    int tlb_word_error;

    pmap_error = pmap_get_tlb_diagnostics(vaddr, &tlb);
    expected_paddr = 0;
    cached_word = 0;
    uncached_word = 0;
    expected_word_error = EFAULT;
    tlb_paddr = 0;
    tlb_word = 0;
    tlb_word_error = EFAULT;
    if (pmap_error == 0 &&
        (tlb.ptd_query_pte & 0x001u) != 0) {
        expected_paddr = (tlb.ptd_query_pte & ~VM_PAGE_MASK) |
            (vaddr & VM_PAGE_MASK);
        expected_word_error = n64_user_fault_read_word(expected_paddr,
            &cached_word, &uncached_word);
    }
    if (pmap_error == 0 &&
        n64_user_fault_tlb_paddr(&tlb, vaddr, &tlb_paddr) == 0)
        tlb_word_error = n64_user_fault_read_word(tlb_paddr,
            &ignored_cached, &tlb_word);

    printf("N64_USER_FAULT map=%s vaddr=%08x error=%d pmap=%08x "
        "asid=%u generation=%u/%u next=%u pte=%08x paddr=%08x\n",
        label, vaddr, pmap_error,
        pmap_error == 0 ? tlb.ptd_active_pmap : 0,
        pmap_error == 0 ? tlb.ptd_active_asid : 0,
        pmap_error == 0 ? tlb.ptd_active_generation : 0,
        pmap_error == 0 ? tlb.ptd_asid_generation : 0,
        pmap_error == 0 ? tlb.ptd_next_asid : 0,
        pmap_error == 0 ? tlb.ptd_query_pte : 0, expected_paddr);
    printf("N64_USER_FAULT expected hi=%08x lo0=%08x lo1=%08x "
        "word_error=%d cached=%08x uncached=%08x\n",
        pmap_error == 0 ? tlb.ptd_query_entryhi : 0,
        pmap_error == 0 ? tlb.ptd_query_entrylo0 : 0,
        pmap_error == 0 ? tlb.ptd_query_entrylo1 : 0,
        expected_word_error, cached_word, uncached_word);
    printf("N64_USER_FAULT hardware found=%u index=%u mask=%08x "
        "hi=%08x lo0=%08x lo1=%08x paddr=%08x "
        "word_error=%d word=%08x\n",
        pmap_error == 0 ? tlb.ptd_hardware_found : 0,
        pmap_error == 0 ? tlb.ptd_hardware_index : 0,
        pmap_error == 0 ? tlb.ptd_hardware_pagemask : 0,
        pmap_error == 0 ? tlb.ptd_hardware_entryhi : 0,
        pmap_error == 0 ? tlb.ptd_hardware_entrylo0 : 0,
        pmap_error == 0 ? tlb.ptd_hardware_entrylo1 : 0,
        tlb_paddr, tlb_word_error, tlb_word);
    printf("N64_USER_FAULT refill count=%lu last_pmap=%08x "
        "last_vaddr=%08x repeat=%u fast_pc=%08x fast_vaddr=%08x "
        "fast_repeat=%u directory=%08x/%08x\n",
        pmap_error == 0 ? (unsigned long)tlb.ptd_refills : 0,
        pmap_error == 0 ? tlb.ptd_last_pmap : 0,
        pmap_error == 0 ? tlb.ptd_last_vaddr : 0,
        pmap_error == 0 ? tlb.ptd_repeat : 0,
        pmap_error == 0 ? tlb.ptd_fast_last_epc : 0,
        pmap_error == 0 ? tlb.ptd_fast_last_vaddr : 0,
        pmap_error == 0 ? tlb.ptd_fast_repeat : 0,
        pmap_error == 0 ? tlb.ptd_active_directory : 0,
        pmap_error == 0 ? tlb.ptd_fast_directory : 0);
}

static void
n64_dump_user_fault(const char *kind, int *frame, unsigned rawcause,
    unsigned badvaddr, int signal, int vm_error)
{
    struct mips_zswap_stats zswap;
    struct vm_object_stats object;
    struct pmap_stats pmap;
    unsigned entryhi;
    unsigned faultpc;
    unsigned wired;
    int object_error;
    int pmap_error;
    int zswap_error;

    faultpc = frame[FRAME_PC] + ((rawcause & CA_BD) != 0 ? NBPW : 0);
    entryhi = mips_read_c0_register(C0_ENTRYHI, 0);
    wired = mips_read_c0_register(C0_WIRED, 0);
    pmap_error = pmap_get_stats(&pmap);
    object_error = vm_object_get_stats(&object);
#ifdef MIPS_ZSWAP_ENABLED
    zswap_error = n64ramswap_get_zswap_stats(&zswap);
#else
    bzero((caddr_t)&zswap, sizeof(zswap));
    zswap_error = ENXIO;
#endif

    printf("\nN64_USER_FAULT kind=%s signal=%d vm_error=%d pid=%d "
        "comm=%s epc=%08x pc=%08x cause=%08x code=%u ce=%u "
        "status=%08x badvaddr=%08x\n",
        kind, signal, vm_error, u.u_procp ? u.u_procp->p_pid : -1,
        u.u_comm, frame[FRAME_PC], faultpc, rawcause,
        (rawcause & CA_EXC_CODE) >> 2, (rawcause & CA_CE) >> 28,
        frame[FRAME_STATUS], badvaddr);
    printf("N64_USER_FAULT sp=%08x ra=%08x entryhi=%08x wired=%u\n",
        frame[FRAME_SP], frame[FRAME_RA], entryhi, wired);
    printf("N64_USER_FAULT vm stats_error=%d pageins=%lu pageouts=%lu "
        "swap_failures=%lu resident=%lu swapped=%lu\n",
        object_error,
        object_error == 0 ? (unsigned long)object.vos_pageins : 0,
        object_error == 0 ? (unsigned long)object.vos_pageouts : 0,
        object_error == 0 ? (unsigned long)object.vos_swap_failures : 0,
        object_error == 0 ? (unsigned long)object.vos_resident_pages : 0,
        object_error == 0 ? (unsigned long)object.vos_swapped_pages : 0);
    printf("N64_USER_FAULT pmap stats_error=%d refills=%lu "
        "flushes=%lu rollovers=%lu invalidations=%lu\n",
        pmap_error,
        pmap_error == 0 ? (unsigned long)pmap.pms_tlb_refills : 0,
        pmap_error == 0 ? (unsigned long)pmap.pms_full_flushes : 0,
        pmap_error == 0 ? (unsigned long)pmap.pms_asid_rollovers : 0,
        pmap_error == 0 ?
        (unsigned long)pmap.pms_targeted_invalidations : 0);
    printf("N64_USER_FAULT zswap stats_error=%d read_errors=%u "
        "last=%u block=%u unit=%u units=%u length=%u flags=%04x "
        "valid=%u raw=%u compressed=%u used_units=%u/%u\n",
        zswap_error,
        zswap_error == 0 ? zswap.mzs_read_errors : 0,
        zswap_error == 0 ? zswap.mzs_last_error : 0,
        zswap_error == 0 ? zswap.mzs_last_error_block : 0,
        zswap_error == 0 ? zswap.mzs_last_error_unit : 0,
        zswap_error == 0 ? zswap.mzs_last_error_units : 0,
        zswap_error == 0 ? zswap.mzs_last_error_length : 0,
        zswap_error == 0 ? zswap.mzs_last_error_flags : 0,
        zswap_error == 0 ? zswap.mzs_valid_blocks : 0,
        zswap_error == 0 ? zswap.mzs_raw_blocks : 0,
        zswap_error == 0 ? zswap.mzs_compressed_blocks : 0,
        zswap_error == 0 ? zswap.mzs_used_units : 0,
        zswap_error == 0 ? zswap.mzs_phys_units : 0);
    n64_dump_user_mapping("pc", faultpc);
    if (badvaddr != faultpc)
        n64_dump_user_mapping("badvaddr", badvaddr);
}
#endif

extern char mips_exception_entry[];
extern char mips_exception_entry_end[];
extern char mips_exception_restore_start[];
extern char mips_exception_restore_end[];
extern void cnintr(void);
#ifdef INET
extern int netisr;
extern void netintr(void);
#endif
#ifdef USBNET_ENABLED
extern void usbnpoll(void);
#endif
#ifdef N64
#ifdef N64CART_ENABLED
extern void n64cart_uart_intr(void);
#endif
#else
void mips_board_intr(int *frame, unsigned status) __attribute__((weak));
void mips_board_timer_intr(void) __attribute__((weak));
int mips_board_microtime(struct timeval *tv, u_int tick_usec)
    __attribute__((weak));
#endif

struct mips_exception_snapshot {
    int valid;
    unsigned frame;
    unsigned pc;
    unsigned sp;
    unsigned ra;
    unsigned status;
    unsigned cause;
    unsigned badvaddr;
    int pid;
    char comm[MAXCOMLEN + 1];
};

static struct mips_exception_snapshot last_exception;
static int exception_panic_prepared;

int
mips_in_interrupt(void)
{
    return mips_interrupt_depth != 0;
}

static int
mips_user_vm_fault(unsigned address, vm_prot_t access)
{
    struct proc *p;
    int status;
    int error;

    error = pmap_fault_active(address, access, 1);
    if (error == 0)
        return 0;
    p = u.u_procp;
    if (p == 0 || p->p_vmspace == 0)
        return error;
    status = mips_intr_enable();
    error = vmspace_fault_context(p->p_vmspace, address, access,
        VM_FAULT_USER | VM_FAULT_CAN_SLEEP);
    mips_intr_restore(status);
    if (error != 0)
        return error;
    return pmap_fault_active(address, access, 1);
}

static int
mips_exception_entry_pc(unsigned pc)
{
    return pc >= (unsigned)mips_exception_entry &&
        pc < (unsigned)mips_exception_entry_end;
}

static int
mips_exception_restore_pc(unsigned pc)
{
    return pc >= (unsigned)mips_exception_restore_start &&
        pc < (unsigned)mips_exception_restore_end;
}

static void
exception_dump_entry_word(unsigned pc)
{
    unsigned instr = *(volatile unsigned *)pc;
    unsigned uncached = *(volatile unsigned *)(0xa0000000u |
        (pc & 0x1fffffffu));

    printf("*** entry word: pc=%08x instr=%08x uncached=%08x\n",
        pc, instr, uncached);
    printf("*** entry range: entry=%08x restore=%08x-%08x end=%08x\n",
        (unsigned)mips_exception_entry,
        (unsigned)mips_exception_restore_start,
        (unsigned)mips_exception_restore_end,
        (unsigned)mips_exception_entry_end);
}

static void
exception_prepare_panic_console(void)
{
    if (exception_panic_prepared)
        return;
    exception_panic_prepared = 1;
#ifdef N64
    n64_console_panic_mode();
#endif
}

static void
exception_save_snapshot(int *frame, unsigned rawcause, unsigned badvaddr)
{
    struct proc *p;
    int i;

    p = u.u_procp;
    last_exception.valid = 1;
    last_exception.frame = (unsigned)frame;
    last_exception.pc = frame[FRAME_PC];
    last_exception.sp = frame[FRAME_SP];
    last_exception.ra = frame[FRAME_RA];
    last_exception.status = frame[FRAME_STATUS];
    last_exception.cause = rawcause;
    last_exception.badvaddr = badvaddr;
    last_exception.pid = p ? p->p_pid : -1;
    for (i = 0; i < MAXCOMLEN && u.u_comm[i]; ++i)
        last_exception.comm[i] = u.u_comm[i];
    last_exception.comm[i] = 0;
}

static void
exception_dump_snapshot(char *tag, struct mips_exception_snapshot *snap)
{
    printf("*** %s: frame=%08x pc=%08x sp=%08x ra=%08x\n",
        tag, snap->frame, snap->pc, snap->sp, snap->ra);
    printf("*** %s: status=%08x cause=%08x badvaddr=%08x pid=%d comm=%s\n",
        tag, snap->status, snap->cause, snap->badvaddr, snap->pid,
        snap->comm);
}

static void
dumpregs(int *frame)
{
    unsigned cause = mips_read_c0_register(C0_CAUSE, 0);
    unsigned badvaddr = mips_read_c0_register(C0_BADVADDR, 0);
    const char *code = 0;

    exception_prepare_panic_console();
    printf("\n*** 0x%08x: exception ", frame[FRAME_PC]);
    switch (cause & CA_EXC_CODE) {
    case CA_Int:    code = "Interrupt"; break;
    case CA_Mod:    code = "TLB modified"; break;
    case CA_TLBL:   code = "TLB load/fetch"; break;
    case CA_TLBS:   code = "TLB store"; break;
    case CA_AdEL:   code = "Address Load"; break;
    case CA_AdES:   code = "Address Save"; break;
    case CA_IBE:    code = "Bus fetch"; break;
    case CA_DBE:    code = "Bus load/store"; break;
    case CA_Sys:    code = "Syscall"; break;
    case CA_Bp:     code = "Breakpoint"; break;
    case CA_RI:     code = "Reserved Instruction"; break;
    case CA_CPU:    code = "Coprocessor Unusable"; break;
    case CA_Ov:     code = "Arithmetic Overflow"; break;
    case CA_Tr:     code = "Trap"; break;
    case CA_FPE:    code = "Floating Point"; break;
    }
    if (code)
        printf("'%s'\n", code);
    else
        printf("%d\n", cause >> 2 & 31);

    switch (cause & CA_EXC_CODE) {
    case CA_Mod:
    case CA_TLBL:
    case CA_TLBS:
    case CA_AdEL:
    case CA_AdES:
        printf("*** badvaddr = 0x%08x\n", badvaddr);
    }
    printf("*** frame=%08x saved_sp=%08x current pid=%d comm=%s\n",
        (unsigned)frame, frame[FRAME_SP],
        u.u_procp ? u.u_procp->p_pid : -1, u.u_comm);
    printf("*** uarea=%08x-%08x\n", (unsigned)md_curuser,
        (unsigned)md_curuser + USIZE);
    if (mips_exception_entry_pc(frame[FRAME_PC])) {
        printf("*** exception occurred inside mips_exception_entry\n");
        if (mips_exception_restore_pc(frame[FRAME_PC]))
            printf("*** exception occurred inside restore path\n");
        exception_dump_entry_word(frame[FRAME_PC]);
    }

    printf("*** registers:\n");
    printf("                t0 = %8x   s0 = %8x   t8 = %8x   lo = %8x\n",
        frame[FRAME_R8], frame[FRAME_R16], frame[FRAME_R24],
        frame[FRAME_LO]);
    printf("at = %8x   t1 = %8x   s1 = %8x   t9 = %8x   hi = %8x\n",
        frame[FRAME_R1], frame[FRAME_R9], frame[FRAME_R17],
        frame[FRAME_R25], frame[FRAME_HI]);
    printf("v0 = %8x   t2 = %8x   s2 = %8x               status = %8x\n",
        frame[FRAME_R2], frame[FRAME_R10], frame[FRAME_R18],
        frame[FRAME_STATUS]);
    printf("v1 = %8x   t3 = %8x   s3 = %8x                cause = %8x\n",
        frame[FRAME_R3], frame[FRAME_R11], frame[FRAME_R19], cause);
    printf("a0 = %8x   t4 = %8x   s4 = %8x   gp = %8x  epc = %8x\n",
        frame[FRAME_R4], frame[FRAME_R12], frame[FRAME_R20],
        frame[FRAME_GP], frame[FRAME_PC]);
    printf("a1 = %8x   t5 = %8x   s5 = %8x   sp = %8x\n",
        frame[FRAME_R5], frame[FRAME_R13], frame[FRAME_R21],
        frame[FRAME_SP]);
    printf("a2 = %8x   t6 = %8x   s6 = %8x   fp = %8x\n",
        frame[FRAME_R6], frame[FRAME_R14], frame[FRAME_R22],
        frame[FRAME_FP]);
    printf("a3 = %8x   t7 = %8x   s7 = %8x   ra = %8x\n",
        frame[FRAME_R7], frame[FRAME_R15], frame[FRAME_R23],
        frame[FRAME_RA]);
#ifdef N64_RESET_DUMP
    n64_reset_dump_backtrace(frame);
#endif
}

static void
mips_reprime_timer(void)
{
    unsigned delta = (MIPS_TIMER_COUNT_KHZ * 1000u + HZ - 1) / HZ;
    unsigned compare = mips_read_c0_register(C0_COMPARE, 0);

    do {
        compare += delta;
        mips_write_c0_register(C0_COMPARE, 0, compare);
    } while ((int)(compare - mips_read_c0_register(C0_COUNT, 0)) < 0);
}

unsigned long
mips_timer_count_to_usec(unsigned count)
{
    unsigned long usec;

    usec = (count / MIPS_TIMER_COUNT_KHZ) * 1000u;
    usec += ((count % MIPS_TIMER_COUNT_KHZ) * 1000u) /
        MIPS_TIMER_COUNT_KHZ;
    return usec;
}

static void
mips_timer_note_late_usec(unsigned long usec)
{
    if (usec < 1000)
        return;

    mips_timer_late_count++;
    mips_timer_late_last_us = usec;
    mips_timer_late_last_tick = ct_ticks;
    if (usec > mips_timer_late_max_us) {
        mips_timer_late_max_us = usec;
        mips_timer_late_max_tick = ct_ticks;
    }
}

void
mips_timer_record(unsigned long late_us, unsigned long clock_us)
{
    mips_timer_irq_count++;
    mips_timer_note_late_usec(late_us);

    mips_timer_clock_last_us = clock_us;
    if (clock_us > mips_timer_clock_max_us)
        mips_timer_clock_max_us = clock_us;
}

static unsigned long
mips_timer_late_usec(void)
{
    unsigned now;
    unsigned compare;
    unsigned late;

    now = mips_read_c0_register(C0_COUNT, 0);
    compare = mips_read_c0_register(C0_COMPARE, 0);
    late = now - compare;
    if ((int)late <= 0)
        return 0;

    return mips_timer_count_to_usec(late);
}

static char *
mips_timer_stats_puts(char *p, char *end, const char *s)
{
    while (p < end && *s)
        *p++ = *s++;
    return p;
}

static char *
mips_timer_stats_putul(char *p, char *end, unsigned long value)
{
    char tmp[10 * sizeof(unsigned long)];
    int n = 0;

    do {
        tmp[n++] = '0' + value % 10;
        value /= 10;
    } while (value && n < sizeof(tmp));

    while (p < end && n > 0)
        *p++ = tmp[--n];
    return p;
}

static char *
mips_timer_stats_putkv(char *p, char *end, const char *key,
    unsigned long value)
{
    p = mips_timer_stats_puts(p, end, key);
    if (p < end)
        *p++ = '=';
    p = mips_timer_stats_putul(p, end, value);
    if (p < end)
        *p++ = ' ';
    return p;
}

int
mips_timer_stats(char *buf, int len)
{
    char *p, *end;
    int s;

    if (len <= 0)
        return 0;

    p = buf;
    end = buf + len - 1;

    s = splhigh();
    p = mips_timer_stats_putkv(p, end, "timer_irq",
        mips_timer_irq_count);
    p = mips_timer_stats_putkv(p, end, "timer_late",
        mips_timer_late_count);
    p = mips_timer_stats_putkv(p, end, "late_last_us",
        mips_timer_late_last_us);
    p = mips_timer_stats_putkv(p, end, "late_max_us",
        mips_timer_late_max_us);
    p = mips_timer_stats_putkv(p, end, "late_last_tick",
        mips_timer_late_last_tick);
    p = mips_timer_stats_putkv(p, end, "late_max_tick",
        mips_timer_late_max_tick);
    p = mips_timer_stats_putkv(p, end, "clock_last_us",
        mips_timer_clock_last_us);
    p = mips_timer_stats_putkv(p, end, "clock_max_us",
        mips_timer_clock_max_us);
    splx(s);

    if (p > buf && p[-1] == ' ')
        p--;
    *p = 0;
    return 0;
}

void
mips_microtime(struct timeval *tv, u_int tick_usec)
{
    unsigned now;
    unsigned last;
    unsigned delta;
    unsigned usec;

    if (tick_usec == 0)
        return;

#ifndef N64
    if (mips_board_microtime && mips_board_microtime(tv, tick_usec))
        return;
#endif

    if (!mips_last_clock_count_valid)
        return;

    last = mips_last_clock_count;
    now = mips_read_c0_register(C0_COUNT, 0);
    delta = now - last;

    usec = mips_timer_count_to_usec(delta);
    if (usec >= tick_usec)
        usec = tick_usec - 1;

    tv->tv_usec += usec;
    if (tv->tv_usec >= 1000000L) {
        tv->tv_sec += tv->tv_usec / 1000000L;
        tv->tv_usec %= 1000000L;
    }
}

void
mips_clock_intr(int *frame, unsigned status)
{
#ifdef N64
#ifdef N64CART_ENABLED
    n64cart_uart_intr();
#endif
#ifdef INPUT_ENABLED
    n64keyboard_console_intr();
#endif
#else
    if (mips_board_timer_intr)
        mips_board_timer_intr();
#endif
    cnintr();
    hardclock((caddr_t)frame[FRAME_PC], status);
    mips_last_clock_count = mips_read_c0_register(C0_COUNT, 0);
    mips_last_clock_count_valid = 1;
#ifdef INET
    if (netisr)
        netintr();
#endif
}

static int
mips_grow_user_stack(vm_vaddr_t address, int from_fault)
{
    struct proc *p = u.u_procp;
    vm_vaddr_t guard_end;
    vm_vaddr_t old_page;

    if (p == 0 || vm_vaddr_round_page(p->p_daddr + p->p_dsize,
        &guard_end) != 0 || guard_end > VM_VADDR_MAX - VM_PAGE_SIZE)
        return EFAULT;
    guard_end += VM_PAGE_SIZE;
    if (address < guard_end || address > USER_DATA_END)
        return EFAULT;
    old_page = vm_vaddr_trunc_page(p->p_saddr);
    if (from_fault && vm_vaddr_trunc_page(address) >= old_page)
        return EFAULT;
    if (vmspace_grow_stack(p->p_vmspace, p->p_saddr, address,
        guard_end) != 0)
        return EFAULT;
    if (p->p_ssize < USER_DATA_END - address) {
        p->p_ssize = USER_DATA_END - address;
        p->p_saddr = address;
        u.u_ssize = p->p_ssize;
    }
    return 0;
}

static int
mips_check_user_stack(int *frame)
{
    return mips_grow_user_stack(frame[FRAME_SP], 0) == 0 ? 0 : SIGSEGV;
}

static void
mips_save_user_fpu(int status)
{
    if (status & ST_CU1)
        mips_fpu_save(&u.u_fpu);
}

static void
mips_restore_user_fpu(int status)
{
    if (status & ST_CU1) {
        int s = mips_intr_disable();

        mips_fpu_enable();
        mips_fpu_restore(&u.u_fpu);
        mips_intr_restore(s);
    }
}

static void
mips_syscall(int *frame)
{
    const struct sysent *callp = &sysent[0];
    unsigned arg;
    int opc = frame[FRAME_PC];
    int code;
    int arg_error = 0;
    int systrace;
    const char *name;
    unsigned long systrace_sequence;
    systrace = u.u_procp != 0 &&
        (u.u_procp->p_flag & P_SYSTRACE) != 0;
    systrace_sequence = 0;
    frame[FRAME_PC] = opc + 3 * NBPW;
    {
        u_int instruction;

        if (copyin((caddr_t)opc, (caddr_t)&instruction,
            sizeof(instruction)) != 0) {
            if (systrace)
                uprintf("STRACE signal pid=%d sig=%d reason=fetch "
                    "pc=%08x sp=%08x\n", u.u_procp->p_pid, SIGSEGV,
                    opc, frame[FRAME_SP]);
            psignal(u.u_procp, SIGSEGV);
            return;
        }
        code = (instruction >> 6) & 0377;
    }
    if (code < nsysent)
        callp += code;
    name = code < nsysent ? syscallnames[code] : "out-of-range";
    if (systrace) {
        systrace_sequence = ++mips_systrace_sequence;
        uprintf("STRACE enter seq=%lu pid=%d call=%s(%d) pc=%08x "
            "a0=%08x a1=%08x a2=%08x a3=%08x\n",
            systrace_sequence, u.u_procp->p_pid, name, code, opc,
            frame[FRAME_R4], frame[FRAME_R5], frame[FRAME_R6],
            frame[FRAME_R7]);
    }
#if defined(N64_TRACE) || defined(MIPS_TRACE)
    {
        static int syscall_trace_count;
        if (syscall_trace_count < 100) {
            printf("mipssys: pid=%d code=%d pc=%x sp=%x a0=%x a1=%x\n",
                u.u_procp ? u.u_procp->p_pid : -1,
                code, opc, frame[FRAME_SP], frame[FRAME_R4],
                frame[FRAME_R5]);
            syscall_trace_count++;
        }
    }
#endif

    if (callp->sy_narg > (int)(sizeof(u.u_arg) / sizeof(u.u_arg[0])))
        arg_error = EINVAL;
    else if (callp->sy_narg) {
        u.u_arg[0] = frame[FRAME_R4];
        if (callp->sy_narg > 1)
            u.u_arg[1] = frame[FRAME_R5];
        if (callp->sy_narg > 2)
            u.u_arg[2] = frame[FRAME_R6];
        if (callp->sy_narg > 3)
            u.u_arg[3] = frame[FRAME_R7];
        for (arg = 4; arg < (unsigned)callp->sy_narg; ++arg) {
            unsigned addr = (frame[FRAME_SP] + 16 +
                (arg - 4) * sizeof(int)) & ~3u;

            if (copyin((caddr_t)addr, (caddr_t)&u.u_arg[arg],
                sizeof(u.u_arg[arg])) != 0) {
                arg_error = EFAULT;
                break;
            }
        }
    }

    u.u_rval = 0;
    u.u_rval2 = 0;
    u.u_error = arg_error;
    if (arg_error == 0 && setjmp(&u.u_qsave) == 0)
        (*callp->sy_call)();

    switch (u.u_error) {
    case 0:
        mips_frame_set_gpr(frame, FRAME_R2, u.u_rval);
        mips_frame_set_gpr(frame, FRAME_R3, u.u_rval2);
        break;
    case ERESTART:
        frame[FRAME_PC] = opc;
        break;
    case EJUSTRETURN:
        break;
    default:
        frame[FRAME_PC] = opc + NBPW;
        mips_frame_set_gpr(frame, FRAME_R2, -1);
        mips_frame_set_gpr(frame, FRAME_R3, -1);
        mips_frame_set_gpr(frame, FRAME_R8, u.u_error);
        break;
    }
    if (systrace)
        uprintf("STRACE exit seq=%lu pid=%d call=%s(%d) error=%d "
            "r0=%08x r1=%08x pc=%08x\n", systrace_sequence,
            u.u_procp->p_pid, name, code, u.u_error,
            (unsigned)u.u_rval, (unsigned)u.u_rval2,
            frame[FRAME_PC]);
}

void
exception(int *frame)
{
    time_t syst;
    unsigned rawcause, cause, status, badvaddr;
    int psig = 0;
    int vm_error;

#if defined(N64_USB_GDB) || defined(N64_RESET_DUMP)
    status = frame[FRAME_STATUS];
    rawcause = mips_read_c0_register(C0_CAUSE, 0);
    badvaddr = mips_read_c0_register(C0_BADVADDR, 0);
#ifdef N64_RESET_DUMP
    if ((rawcause & status & MIPS_CAUSE_IP4) != 0 &&
        n64_reset_dump_interrupt(frame, rawcause, badvaddr))
        return;
#endif
#ifdef N64_USB_GDB
    if (n64_gdb_exception(frame, rawcause, badvaddr))
        return;
#endif
#endif
    led_control(LED_KERNEL, 1);
    md_uarea_guard_check(md_curuser);
    if (
#if defined(N64_USB_GDB) || defined(N64_RESET_DUMP)
        !n64_emergency_frame(frame) &&
#endif
        (unsigned)frame < (unsigned)&u + sizeof(u)) {
        dumpregs(frame);
        panic("stack overflow");
    }

#if !defined(N64_USB_GDB) && !defined(N64_RESET_DUMP)
    status = frame[FRAME_STATUS];
    rawcause = mips_read_c0_register(C0_CAUSE, 0);
    badvaddr = mips_read_c0_register(C0_BADVADDR, 0);
#endif
#if defined(N64_TRACE) || defined(MIPS_TRACE)
    {
        static int exception_trace_count;
        if (exception_trace_count < 32) {
            printf("mipstrap: status=%08x cause=%08x pc=%08x sp=%08x\n",
                status, rawcause, frame[FRAME_PC], frame[FRAME_SP]);
            exception_trace_count++;
        }
    }
#endif
    if (mips_exception_entry_pc(frame[FRAME_PC]) &&
        (rawcause & CA_EXC_CODE) != CA_Int) {
        exception_prepare_panic_console();
        printf("*** exception inside mips_exception_entry\n");
        if (mips_exception_restore_pc(frame[FRAME_PC]))
            printf("*** exception inside restore path\n");
        printf("*** current exception: frame=%08x pc=%08x sp=%08x ra=%08x\n",
            (unsigned)frame, frame[FRAME_PC], frame[FRAME_SP],
            frame[FRAME_RA]);
        printf("*** current exception: status=%08x cause=%08x badvaddr=%08x pid=%d comm=%s\n",
            status, rawcause, badvaddr,
            u.u_procp ? u.u_procp->p_pid : -1, u.u_comm);
        exception_dump_entry_word(frame[FRAME_PC]);
        if (last_exception.valid)
            exception_dump_snapshot("previous exception", &last_exception);
    }
    exception_save_snapshot(frame, rawcause, badvaddr);

    mips_write_c0_register(C0_STATUS, 0,
        status & ~(ST_KSU | ST_EXL | ST_ERL | ST_IE));
    if (USERMODE(status))
        mips_save_user_fpu(status);

    cause = rawcause & CA_EXC_CODE;
    if (USERMODE(status))
        cause |= USER;

    syst = u.u_ru.ru_stime;

    switch (cause) {
    case CA_Int:
    case CA_Int + USER:
#ifdef UCB_METER
        cnt.v_intr++;
#endif
        ++mips_interrupt_depth;
#ifdef N64
        if (rawcause & MIPS_CAUSE_IP2)
            n64_interrupt_handle_mi();
#ifdef USBNET_ENABLED
        if (rawcause & MIPS_CAUSE_IP3)
            usbnpoll();
#endif
#else
        if (rawcause & MIPS_CAUSE_IP2) {
            if (mips_board_intr)
                mips_board_intr(frame, status);
            cnintr();
        }
#endif
        if (rawcause & MIPS_CAUSE_IP7) {
            unsigned clock_start;
            unsigned long late_us;

            late_us = mips_timer_late_usec();
            clock_start = mips_read_c0_register(C0_COUNT, 0);
            mips_reprime_timer();
            mips_clock_intr(frame, status);
            mips_timer_record(late_us, mips_timer_count_to_usec(
                mips_read_c0_register(C0_COUNT, 0) - clock_start));
        }
        --mips_interrupt_depth;
        if ((cause & USER) && runrun) {
            u.u_frame = frame;
            u.u_code = frame[FRAME_PC];
            psig = mips_check_user_stack(frame);
            if (psig)
                break;
            mips_intr_enable();
            goto out;
        }
        goto ret;

    case CA_Sys + USER:
#ifdef UCB_METER
        cnt.v_syscall++;
#endif
        mips_intr_enable();
        u.u_error = 0;
        u.u_frame = frame;
        u.u_code = frame[FRAME_PC];
        psig = mips_check_user_stack(frame);
        if (psig)
            break;
        mips_syscall(frame);
        goto out;

    case CA_CPU + USER:
        if ((rawcause & CA_CE) == MIPS_CAUSE_CE1) {
            frame[FRAME_STATUS] = (frame[FRAME_STATUS] | ST_CU1) & ~ST_FR;
            goto ret;
        }
#ifdef N64
        n64_dump_user_fault("coprocessor", frame, rawcause, badvaddr,
            SIGEMT, -1);
#endif
        psig = SIGEMT;
        mips_intr_enable();
        break;

    case CA_Mod:
    case CA_TLBS:
        if (pmap_fault_active(badvaddr, VM_PROT_WRITE, 0) == 0)
            goto ret;
#ifdef N64_USB_GDB
        n64_gdb_panic(frame, rawcause, badvaddr);
#endif
        dumpregs(frame);
        panic("kernel pmap write fault");

    case CA_TLBL:
        if (pmap_fault_active(badvaddr, VM_PROT_READ, 0) == 0)
            goto ret;
#ifdef N64_USB_GDB
        n64_gdb_panic(frame, rawcause, badvaddr);
#endif
        dumpregs(frame);
        panic("kernel pmap read fault");

    case CA_Mod + USER:
    case CA_TLBS + USER:
        vm_error = mips_user_vm_fault(badvaddr, VM_PROT_WRITE);
        if (vm_error == 0)
            goto ret;
        if (mips_grow_user_stack(badvaddr, 1) == 0) {
            vm_error = mips_user_vm_fault(badvaddr, VM_PROT_WRITE);
            if (vm_error == 0)
                goto ret;
        }
        psig = vm_error == ENXIO || vm_error == EIO ? SIGBUS : SIGSEGV;
#ifdef N64
        if (psig == SIGBUS)
            n64_dump_user_fault("vm-write", frame, rawcause, badvaddr,
                psig, vm_error);
#endif
        mips_intr_enable();
        break;

    case CA_TLBL + USER:
        vm_error = mips_user_vm_fault(badvaddr, VM_PROT_READ);
        if (vm_error == 0)
            goto ret;
        if (mips_grow_user_stack(badvaddr, 1) == 0) {
            vm_error = mips_user_vm_fault(badvaddr, VM_PROT_READ);
            if (vm_error == 0)
                goto ret;
        }
        psig = vm_error == ENXIO || vm_error == EIO ? SIGBUS : SIGSEGV;
#ifdef N64
        if (psig == SIGBUS)
            n64_dump_user_fault("vm-read", frame, rawcause, badvaddr,
                psig, vm_error);
#endif
        mips_intr_enable();
        break;

    default:
#ifdef UCB_METER
        cnt.v_trap++;
#endif
        switch (cause) {
        default:
#ifdef N64_USB_GDB
            n64_gdb_panic(frame, rawcause, badvaddr);
#endif
            dumpregs(frame);
            panic("unexpected exception");
        case CA_AdEL + USER:
        case CA_AdES + USER:
#if defined(N64_TRACE) || defined(MIPS_TRACE)
            mips_trace_user_fault("address", frame, badvaddr);
#endif
            psig = SIGBUS;
#ifdef N64
            n64_dump_user_fault("address", frame, rawcause, badvaddr,
                psig, -1);
#endif
            break;
        case CA_IBE + USER:
        case CA_DBE + USER:
#if defined(N64_TRACE) || defined(MIPS_TRACE)
            mips_trace_user_fault("bus", frame, badvaddr);
#endif
            psig = SIGBUS;
#ifdef N64
            n64_dump_user_fault("bus", frame, rawcause, badvaddr,
                psig, -1);
#endif
            break;
        case CA_RI + USER:
            psig = SIGILL;
            break;
        case CA_Bp + USER:
            psig = SIGTRAP;
            break;
        case CA_Tr + USER:
            psig = SIGIOT;
            break;
        case CA_Ov + USER:
        case CA_FPE + USER:
#ifdef MIPS_TRACE
            printf("*** user arithmetic exception: pc=%08x status=%08x "
                "cause=%08x", frame[FRAME_PC], status, rawcause);
            if ((cause & ~USER) == CA_FPE)
                printf(" fcsr=%08x", u.u_fpu.fcsr);
            printf(" sp=%08x ra=%08x pid=%d comm=%s\n",
                frame[FRAME_SP], frame[FRAME_RA],
                u.u_procp ? u.u_procp->p_pid : -1, u.u_comm);
#endif
            psig = SIGFPE;
            break;
        }
        mips_intr_enable();
        break;
    }

    if (psig != 0 && u.u_procp != 0 &&
        (u.u_procp->p_flag & P_SYSTRACE) != 0)
        uprintf("STRACE signal pid=%d sig=%d cause=%08x pc=%08x "
            "badvaddr=%08x\n", u.u_procp->p_pid, psig, rawcause,
            frame[FRAME_PC], badvaddr);
    psignal(u.u_procp, psig);
out:
    for (;;) {
        psig = CURSIG(u.u_procp);
        if (psig <= 0)
            break;
        postsig(psig);
    }
    curpri = setpri(u.u_procp);

    if (runrun) {
        setrq(u.u_procp);
        u.u_ru.ru_nivcsw++;
        swtch();
    }

    if (u.u_prof.pr_scale)
        addupc((caddr_t)frame[FRAME_PC], &u.u_prof,
            (int)(u.u_ru.ru_stime - syst));
ret:
    if (USERMODE(frame[FRAME_STATUS])) {
        frame[FRAME_STATUS] &= ~ST_FR;
        mips_restore_user_fpu(frame[FRAME_STATUS]);
    }
    mips_intr_disable();
    mips_ehb();
    led_control(LED_KERNEL, 0);
}
