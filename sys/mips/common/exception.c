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
#include <machine/io.h>
#include <machine/fpu.h>
#ifdef N64
#include <machine/console.h>
#include <machine/n64.h>
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
#define MIPS_CAUSE_IP7   0x00008000u

#ifdef N64
#define MIPS_TIMER_COUNT_KHZ    N64_COUNT_KHZ
#else
#define MIPS_TIMER_COUNT_KHZ    MIPS_COUNT_KHZ
#endif

static int last_user_icache_pid = -1;
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
volatile unsigned int ct_ticks = 0;

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
    ct_ticks++;
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
mips_check_user_stack(int *frame)
{
    unsigned sp = frame[FRAME_SP];

    if (sp < u.u_procp->p_daddr + u.u_dsize || sp > USER_DATA_END)
        return SIGSEGV;
    if (u.u_procp->p_ssize < USER_DATA_END - sp) {
        u.u_procp->p_ssize = USER_DATA_END - sp;
        u.u_procp->p_saddr = sp;
        u.u_ssize = u.u_procp->p_ssize;
    }
    return 0;
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
    int opc = frame[FRAME_PC];
    int code;

    frame[FRAME_PC] = opc + 3 * NBPW;
    code = (*(u_int *)opc >> 6) & 0377;
    if (code < nsysent)
        callp += code;
#if defined(N64_TRACE) || defined(MIPS_TRACE)
    {
        static int syscall_trace_count;
        if (syscall_trace_count < 12) {
            printf("n64sys: code=%d pc=%x sp=%x a0=%x a1=%x\n",
                code, opc, frame[FRAME_SP], frame[FRAME_R4],
                frame[FRAME_R5]);
            syscall_trace_count++;
        }
    }
#endif

    if (callp->sy_narg) {
        u.u_arg[0] = frame[FRAME_R4];
        u.u_arg[1] = frame[FRAME_R5];
        u.u_arg[2] = frame[FRAME_R6];
        u.u_arg[3] = frame[FRAME_R7];
        if (callp->sy_narg > 4) {
            unsigned addr = (frame[FRAME_SP] + 16) & ~3;
            if (!baduaddr((caddr_t)addr))
                u.u_arg[4] = *(unsigned *)addr;
        }
        if (callp->sy_narg > 5) {
            unsigned addr = (frame[FRAME_SP] + 20) & ~3;
            if (!baduaddr((caddr_t)addr))
                u.u_arg[5] = *(unsigned *)addr;
        }
    }

    u.u_rval = 0;
    u.u_rval2 = 0;
    if (setjmp(&u.u_qsave) == 0)
        (*callp->sy_call)();

    switch (u.u_error) {
    case 0:
        frame[FRAME_R2] = u.u_rval;
        frame[FRAME_R3] = u.u_rval2;
        if (code == 11 || code == 59)
            mips_sync_user_icache();
        break;
    case ERESTART:
        frame[FRAME_PC] = opc;
        break;
    case EJUSTRETURN:
        break;
    default:
        frame[FRAME_PC] = opc + NBPW;
        frame[FRAME_R2] = -1;
        frame[FRAME_R3] = -1;
        frame[FRAME_R8] = u.u_error;
        break;
    }
}

void
exception(int *frame)
{
    time_t syst;
    unsigned rawcause, cause, status, badvaddr;
    int psig = 0;

    led_control(LED_KERNEL, 1);
    if ((unsigned)frame < (unsigned)&u + sizeof(u)) {
        dumpregs(frame);
        panic("stack overflow");
    }

    status = frame[FRAME_STATUS];
    rawcause = mips_read_c0_register(C0_CAUSE, 0);
    badvaddr = mips_read_c0_register(C0_BADVADDR, 0);
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
        psig = SIGEMT;
        mips_intr_enable();
        break;

    case CA_Mod:
    case CA_TLBS:
        if (pmap_fault_active(badvaddr, VM_PROT_WRITE, 0) == 0)
            goto ret;
        dumpregs(frame);
        panic("kernel pmap write fault");

    case CA_TLBL:
        if (pmap_fault_active(badvaddr, VM_PROT_READ, 0) == 0)
            goto ret;
        dumpregs(frame);
        panic("kernel pmap read fault");

    case CA_Mod + USER:
    case CA_TLBS + USER:
        if (pmap_fault_active(badvaddr, VM_PROT_WRITE, 1) == 0)
            goto ret;
        psig = SIGSEGV;
        mips_intr_enable();
        break;

    case CA_TLBL + USER:
        if (pmap_fault_active(badvaddr, VM_PROT_READ, 1) == 0)
            goto ret;
        psig = SIGSEGV;
        mips_intr_enable();
        break;

    default:
#ifdef UCB_METER
        cnt.v_trap++;
#endif
        switch (cause) {
        default:
            dumpregs(frame);
            panic("unexpected exception");
        case CA_AdEL + USER:
        case CA_AdES + USER:
            printf("*** 0x%08x: %s: bad address 0x%08x\n",
                frame[FRAME_PC], u.u_comm, badvaddr);
            printf("*** user exception: exc=%u cause=%08x sp=%08x ra=%08x\n",
                rawcause >> 2 & 31, rawcause, frame[FRAME_SP],
                frame[FRAME_RA]);
            psig = SIGBUS;
            break;
        case CA_IBE + USER:
        case CA_DBE + USER:
            printf("*** 0x%08x: %s: bus error\n",
                frame[FRAME_PC], u.u_comm);
            psig = SIGBUS;
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
#if defined(N64_PCC_FPE_TRACE) || defined(MIPS_TRACE)
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
        if (last_user_icache_pid != u.u_procp->p_pid) {
            mips_sync_user_icache();
            last_user_icache_pid = u.u_procp->p_pid;
        }
        mips_restore_user_fpu(frame[FRAME_STATUS]);
    }
    mips_intr_disable();
    mips_ehb();
    led_control(LED_KERNEL, 0);
}
