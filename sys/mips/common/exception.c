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
#include <machine/io.h>
#include <machine/fpu.h>
#ifdef N64
#include <machine/n64.h>
#include <machine/joybus.h>
#include <machine/n64int.h>
#else
#include <machine/layout.h>
#endif

#define USER            1
#define MIPS_CAUSE_CE1   0x10000000u
#define MIPS_CAUSE_IP2   0x00000400u
#define MIPS_CAUSE_IP7   0x00008000u

#ifdef N64
#define MIPS_TIMER_COUNT_KHZ    N64_COUNT_KHZ
#else
#define MIPS_TIMER_COUNT_KHZ    MIPS_COUNT_KHZ
#endif

static int last_user_icache_pid = -1;
volatile unsigned int ct_ticks = 0;

extern void cnintr(void);
#ifdef INET
extern int netisr;
extern void netintr(void);
#endif
#ifdef N64
#ifdef N64CART_ENABLED
extern void n64cart_uart_intr(void);
#endif
#else
extern void malta_uart_intr(void);
#endif

static void
dumpregs(int *frame)
{
    unsigned cause = mips_read_c0_register(C0_CAUSE, 0);
    const char *code = 0;

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
        printf("*** badvaddr = 0x%08x\n",
            mips_read_c0_register(C0_BADVADDR, 0));
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
#ifdef N64_TRACE
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
    if (setjmp(&u.u_qsave) == 0)
        (*callp->sy_call)();

    switch (u.u_error) {
    case 0:
        frame[FRAME_R2] = u.u_rval;
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
        frame[FRAME_R8] = u.u_error;
        break;
    }
}

void
exception(int *frame)
{
    time_t syst;
    unsigned rawcause, cause, status;
    int psig = 0;

    led_control(LED_KERNEL, 1);
    if ((unsigned)frame < (unsigned)&u + sizeof(u)) {
        dumpregs(frame);
        panic("stack overflow");
    }

    status = frame[FRAME_STATUS];
    mips_write_c0_register(C0_STATUS, 0,
        status & ~(ST_KSU | ST_EXL | ST_ERL | ST_IE));
    if (USERMODE(status))
        mips_save_user_fpu(status);

    rawcause = mips_read_c0_register(C0_CAUSE, 0);
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
#endif
        if (rawcause & MIPS_CAUSE_IP7) {
            mips_reprime_timer();
            ct_ticks++;
#ifdef N64
#ifdef N64CART_ENABLED
            n64cart_uart_intr();
#endif
            n64keyboard_console_intr();
#else
            malta_uart_intr();
#endif
            cnintr();
            hardclock((caddr_t)frame[FRAME_PC], status);
#ifdef INET
            if (netisr)
                netintr();
#endif
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

    default:
#ifdef UCB_METER
        cnt.v_trap++;
#endif
        switch (cause) {
        default:
            dumpregs(frame);
            panic("unexpected exception");
        case CA_Mod + USER:
        case CA_TLBL + USER:
        case CA_TLBS + USER:
        case CA_AdEL + USER:
        case CA_AdES + USER:
            printf("*** 0x%08x: %s: bad address 0x%08x\n",
                frame[FRAME_PC], u.u_comm,
                mips_read_c0_register(C0_BADVADDR, 0));
            psig = SIGSEGV;
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
    led_control(LED_KERNEL, 0);
}
