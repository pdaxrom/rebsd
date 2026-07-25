#include <sys/errno.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <vm/vmspace.h>

#include "interrupt.h"
#include "privilege.h"
#include "signal_machdep.h"
#include "syscall.h"
#include "trap.h"
#include "tss.h"
#include "user_return.h"
#include "vmspace_bootstrap.h"

#define I386_TRAP_TEST_CODE       0x53900000u
#define I386_TRAP_TEST_STACK      0x53a00000u
#define I386_TRAP_TEST_STACK_TOP  (I386_TRAP_TEST_STACK + VM_PAGE_SIZE)
#define I386_TRAP_TEST_ILL_HANDLER (I386_TRAP_TEST_CODE + 0x40u)
#define I386_TRAP_TEST_SEGV_HANDLER (I386_TRAP_TEST_CODE + 0x80u)
#define I386_TRAP_TEST_TRAMPOLINE (I386_TRAP_TEST_CODE + 0xc0u)
#define I386_TRAP_TEST_ILL_MAGIC  0x494c4c21u
#define I386_TRAP_TEST_SEGV_MAGIC 0x50414745u
#define I386_TRAP_TEST_BAD_ADDRESS 0x60000000u
#define I386_TRAP_TEST_RESULT     0x54524150u

/*
 * User entry first executes UD2 and then reads an unmapped address.  Separate
 * SIGILL and SIGSEGV handlers verify signum/u_code, advance sc_eip by the
 * faulting instruction length, store a phase magic in sc_eax, and return
 * through the same int 0x80 sigreturn trampoline.
 */
static const unsigned char i386_trap_test_code[0xca] = {
    [0x00] = 0x0f, 0x0b,
    [0x02] = 0x3d, 0x21, 0x4c, 0x4c, 0x49,
    [0x07] = 0x75, 0x0e,
    [0x09] = 0xa1, 0x00, 0x00, 0x00, 0x60,
    [0x0e] = 0x3d, 0x45, 0x47, 0x41, 0x50,
    [0x13] = 0x75, 0x02,
    [0x15] = 0xcd, 0x30,
    [0x17] = 0x0f, 0x0b,

    [0x40] = 0x83, 0x7c, 0x24, 0x04, SIGILL,
    [0x45] = 0x75, 0xd0,
    [0x47] = 0x81, 0x7c, 0x24, 0x08, 0x00, 0x00, 0x90, 0x53,
    [0x4f] = 0x75, 0xc6,
    [0x51] = 0x8b, 0x44, 0x24, 0x0c,
    [0x55] = 0x83, 0x40, 0x34, 0x02,
    [0x59] = 0xc7, 0x40, 0x30, 0x21, 0x4c, 0x4c, 0x49,
    [0x60] = 0xc3,

    [0x80] = 0x83, 0x7c, 0x24, 0x04, SIGSEGV,
    [0x85] = 0x75, 0x90,
    [0x87] = 0x81, 0x7c, 0x24, 0x08, 0x00, 0x00, 0x00, 0x60,
    [0x8f] = 0x75, 0x86,
    [0x91] = 0x8b, 0x44, 0x24, 0x0c,
    [0x95] = 0x83, 0x40, 0x34, 0x05,
    [0x99] = 0xc7, 0x40, 0x30, 0x45, 0x47, 0x41, 0x50,
    [0xa0] = 0xc3,

    [0xc0] = 0x8b, 0x5c, 0x24, 0x08,
    [0xc4] = 0x31, 0xc0,
    [0xc6] = 0xcd, 0x80,
    [0xc8] = 0x0f, 0x0b
};

typedef char i386_assert_trap_sc_eax[
    __builtin_offsetof(struct sigcontext, sc_eax) == 0x30 ? 1 : -1];
typedef char i386_assert_trap_sc_eip[
    __builtin_offsetof(struct sigcontext, sc_eip) == 0x34 ? 1 : -1];

static struct user *i386_trap_uarea;
static struct vmspace *i386_trap_vmspace;
static volatile int i386_trap_active;
static volatile int i386_trap_result;
static volatile int i386_trap_reschedule;
static int i386_trap_priority;
static unsigned i386_trap_next_count;
static unsigned i386_trap_deliver_count;
static unsigned i386_trap_priority_count;

static int
i386_trap_test_next_signal(struct proc *process)
{
    ++i386_trap_next_count;
    if ((process->p_sig & sigmask(SIGILL)) != 0)
        return SIGILL;
    if ((process->p_sig & sigmask(SIGSEGV)) != 0)
        return SIGSEGV;
    return 0;
}

static void
i386_trap_test_deliver_signal(int signum)
{
    struct proc *process;
    long mask;
    long return_mask;

    process = u.u_procp;
    mask = sigmask(signum);
    return_mask = process->p_sigmask;
    process->p_sig &= ~mask;
    process->p_sigmask |= u.u_sigmask[signum] | mask;
    u.u_ru.ru_nsignals++;
    ++i386_trap_deliver_count;
    sendsig(u.u_signal[signum], signum, return_mask);
}

static int
i386_trap_test_set_priority(struct proc *process)
{
    ++i386_trap_priority_count;
    process->p_pri = 43;
    return process->p_pri;
}

static void
i386_trap_test_enqueue(struct proc *process)
{
    (void)process;
    i386_trap_result = EFAULT;
}

static void
i386_trap_test_switch(void)
{
    i386_trap_result = EFAULT;
}

static const struct i386_user_return_ops i386_trap_return_ops = {
    i386_trap_test_next_signal,
    i386_trap_test_deliver_signal,
    i386_trap_test_set_priority,
    i386_trap_test_enqueue,
    i386_trap_test_switch,
    &i386_trap_reschedule,
    &i386_trap_priority
};

static const struct sysent i386_trap_sysent[] = {
    { 1, sigreturn }
};

static void
i386_trap_kernel_resume(void)
{
    volatile unsigned stack_probe;
    unsigned stack_address;
    unsigned stack_start;

    stack_address = (unsigned)(unsigned long)&stack_probe;
    stack_start = (unsigned)(unsigned long)i386_trap_uarea;
    if (stack_address >= stack_start &&
        stack_address < stack_start + USIZE &&
        i386_trap_result == 0)
        i386_trap_result = I386_TRAP_TEST_RESULT;
    else
        i386_trap_result = EFAULT;
    longjmp((size_t)i386_trap_uarea, &i386_trap_uarea->u_rsave);
    for (;;)
        __asm__ volatile ("cli; hlt");
}

int
i386_trap_handle_test(struct i386_trapframe *frame)
{
    if (!i386_trap_active)
        return 0;
    if (frame->tf_vector == I386_USER_RETURN_VECTOR &&
        frame->tf_cs == I386_USER_CODE_SELECTOR &&
        frame->tf_ss == I386_USER_DATA_SELECTOR &&
        frame->tf_eip == I386_TRAP_TEST_CODE + 23u &&
        frame->tf_useresp == I386_TRAP_TEST_STACK_TOP &&
        frame->tf_eax == I386_TRAP_TEST_SEGV_MAGIC &&
        i386_trap_uarea->u_procp->p_sig == 0 &&
        i386_trap_uarea->u_procp->p_sigmask == sigmask(SIGUSR2) &&
        i386_trap_next_count == 6u &&
        i386_trap_deliver_count == 2u &&
        i386_trap_priority_count == 4u &&
        i386_trap_priority == 43 &&
        i386_trap_uarea->u_ru.ru_nsignals == 2) {
        i386_trap_result = 0;
        i386_privilege_return_to_kernel(frame,
            (unsigned)(unsigned long)i386_trap_kernel_resume);
        return 1;
    }

    i386_trap_result = EFAULT;
    i386_privilege_return_to_kernel(frame,
        (unsigned)(unsigned long)i386_trap_kernel_resume);
    return 1;
}

int
i386_trap_selftest(void)
{
    struct proc process;
    const struct sysent *saved_table;
    unsigned saved_table_count;
    vm_pfn_t free_before;
    int resumed;
    volatile int error;

    if (i386_trap_signal(0) != SIGFPE ||
        i386_trap_signal(1) != SIGTRAP ||
        i386_trap_signal(3) != SIGTRAP ||
        i386_trap_signal(6) != SIGILL ||
        i386_trap_signal(10) != SIGBUS ||
        i386_trap_signal(13) != SIGSEGV ||
        i386_trap_signal(14) != SIGSEGV ||
        i386_trap_signal(2) != 0 ||
        i386_trap_signal(8) != 0 ||
        i386_trap_signal(18) != 0)
        return EFAULT;

    i386_trap_uarea = (struct user *)0;
    i386_trap_vmspace = (struct vmspace *)0;
    i386_trap_active = 0;
    i386_trap_result = EFAULT;
    i386_trap_reschedule = 0;
    i386_trap_priority = -1;
    i386_trap_next_count = 0;
    i386_trap_deliver_count = 0;
    i386_trap_priority_count = 0;
    bzero(&process, sizeof(process));
    i386_syscall_get_table(&saved_table, &saved_table_count);
    i386_syscall_set_table(i386_trap_sysent,
        sizeof(i386_trap_sysent) / sizeof(i386_trap_sysent[0]));
    free_before = vm_page_boot_allocator.vpa_free_count;
    error = 0;

    i386_trap_uarea = md_uarea_alloc();
    if (i386_trap_uarea == (struct user *)0) {
        error = ENOMEM;
        goto out;
    }
    error = vmspace_create(&i386_trap_vmspace);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(i386_trap_vmspace,
        I386_TRAP_TEST_CODE, VM_PAGE_SIZE, VM_PROT_ALL,
        VM_MAP_EXECUTABLE);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(i386_trap_vmspace,
        I386_TRAP_TEST_STACK, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_MAP_STACK);
    if (error != 0)
        goto out;
    error = vmspace_write(i386_trap_vmspace, I386_TRAP_TEST_CODE,
        i386_trap_test_code, sizeof(i386_trap_test_code));
    if (error != 0)
        goto out;
    error = i386_vmspace_activate(i386_trap_vmspace);
    if (error != 0)
        goto out;

    process.p_vmspace = i386_trap_vmspace;
    process.p_daddr = I386_TRAP_TEST_CODE;
    process.p_saddr = I386_TRAP_TEST_STACK;
    process.p_dsize = VM_PAGE_SIZE;
    process.p_ssize = VM_PAGE_SIZE;
    process.p_sigmask = sigmask(SIGUSR2);
    i386_trap_uarea->u_procp = &process;
    i386_trap_uarea->u_dsize = VM_PAGE_SIZE;
    i386_trap_uarea->u_ssize = VM_PAGE_SIZE;
    i386_trap_uarea->u_signal[SIGILL] =
        (sig_t)I386_TRAP_TEST_ILL_HANDLER;
    i386_trap_uarea->u_signal[SIGSEGV] =
        (sig_t)I386_TRAP_TEST_SEGV_HANDLER;
    i386_trap_uarea->u_sigtramp = I386_TRAP_TEST_TRAMPOLINE;
    md_curuser = i386_trap_uarea;
    i386_tss_set_kernel_stack((unsigned)(unsigned long)
        i386_trap_uarea + USIZE);
    i386_user_return_set_ops(&i386_trap_return_ops);

    i386_trap_active = 1;
    resumed = setjmp(&i386_trap_uarea->u_rsave);
    if (resumed == 0)
        i386_user_enter(I386_TRAP_TEST_CODE, I386_TRAP_TEST_STACK_TOP);
    if (resumed != 1 || i386_trap_result != I386_TRAP_TEST_RESULT)
        error = EFAULT;

out:
    i386_trap_active = 0;
    i386_user_return_set_ops((const struct i386_user_return_ops *)0);
    i386_syscall_set_table(saved_table, saved_table_count);
    i386_tss_reset_kernel_stack();
    if (i386_trap_vmspace != (struct vmspace *)0)
        i386_vmspace_deactivate(i386_trap_vmspace);
    md_curuser = (struct user *)0;
    if (i386_trap_uarea != (struct user *)0)
        md_uarea_free(i386_trap_uarea);
    i386_trap_uarea = (struct user *)0;
    if (i386_trap_vmspace != (struct vmspace *)0 &&
        vmspace_destroy(i386_trap_vmspace) != 0 && error == 0)
        error = EFAULT;
    i386_trap_vmspace = (struct vmspace *)0;
    if (vm_page_boot_allocator.vpa_free_count != free_before && error == 0)
        error = EFAULT;
    return error;
}
