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
#include "tss.h"
#include "user_return.h"
#include "vmspace_bootstrap.h"

#define I386_USER_RETURN_CODE       0x53700000u
#define I386_USER_RETURN_STACK      0x53800000u
#define I386_USER_RETURN_STACK_TOP  \
    (I386_USER_RETURN_STACK + VM_PAGE_SIZE)
#define I386_USER_RETURN_HANDLER    (I386_USER_RETURN_CODE + 0x20u)
#define I386_USER_RETURN_TRAMPOLINE (I386_USER_RETURN_CODE + 0x40u)
#define I386_USER_RETURN_MAGIC      0x55524554u
#define I386_USER_RETURN_SIGNAL_CODE 0x1234

/*
 *   00: int $0x30                 request a pending-signal return
 *   02: cmp $MAGIC,%eax           sigreturn restores handler-edited eax
 *   07: jne 0x0b
 *   09: int $0x30                 return to the kernel self-test
 *   0b: ud2
 *
 * Handler 0x20 validates cdecl signum/code/context arguments, writes MAGIC
 * to sc_eax and returns through the trampoline at 0x40.  The trampoline
 * passes the context in EBX to sigreturn syscall zero.
 */
static const unsigned char i386_user_return_code[] = {
    0xcd, 0x30,
    0x3d, 0x54, 0x45, 0x52, 0x55,
    0x75, 0x02,
    0xcd, 0x30,
    0x0f, 0x0b,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0x83, 0x7c, 0x24, 0x04, SIGUSR1,
    0x75, 0xe4,
    0x81, 0x7c, 0x24, 0x08, 0x34, 0x12, 0x00, 0x00,
    0x75, 0xda,
    0x8b, 0x44, 0x24, 0x0c,
    0xc7, 0x40, 0x30, 0x54, 0x45, 0x52, 0x55,
    0xc3,
    0x90, 0x90, 0x90,
    0x8b, 0x5c, 0x24, 0x08,
    0x31, 0xc0,
    0xcd, 0x80,
    0x0f, 0x0b
};

typedef char i386_assert_user_return_code_size[
    sizeof(i386_user_return_code) == 74 ? 1 : -1];
typedef char i386_assert_user_return_sc_eax[
    __builtin_offsetof(struct sigcontext, sc_eax) == 0x30 ? 1 : -1];

static struct user *i386_user_return_uarea;
static struct vmspace *i386_user_return_vmspace;
static volatile int i386_user_return_active;
static volatile int i386_user_return_phase;
static volatile int i386_user_return_result;
static volatile int i386_user_return_reschedule;
static int i386_user_return_priority;
static unsigned i386_user_return_next_count;
static unsigned i386_user_return_deliver_count;
static unsigned i386_user_return_priority_count;
static unsigned i386_user_return_enqueue_count;
static unsigned i386_user_return_switch_count;

static int
i386_user_return_test_next_signal(struct proc *process)
{
    ++i386_user_return_next_count;
    if ((process->p_sig & sigmask(SIGUSR1)) != 0)
        return SIGUSR1;
    return 0;
}

static void
i386_user_return_test_deliver_signal(int signum)
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
    ++i386_user_return_deliver_count;
    sendsig(u.u_signal[signum], signum, return_mask);
}

static int
i386_user_return_test_set_priority(struct proc *process)
{
    ++i386_user_return_priority_count;
    process->p_pri = 42;
    return process->p_pri;
}

static void
i386_user_return_test_enqueue(struct proc *process)
{
    if (process == u.u_procp)
        ++i386_user_return_enqueue_count;
}

static void
i386_user_return_test_switch(void)
{
    if (i386_user_return_enqueue_count == 1u &&
        u.u_ru.ru_nivcsw == 1)
        ++i386_user_return_switch_count;
    i386_user_return_reschedule = 0;
}

static const struct i386_user_return_ops i386_user_return_ops = {
    i386_user_return_test_next_signal,
    i386_user_return_test_deliver_signal,
    i386_user_return_test_set_priority,
    i386_user_return_test_enqueue,
    i386_user_return_test_switch,
    &i386_user_return_reschedule,
    &i386_user_return_priority
};

static const struct sysent i386_user_return_sysent[] = {
    { 1, sigreturn }
};

static void
i386_user_return_kernel_resume(void)
{
    volatile unsigned stack_probe;
    unsigned stack_address;
    unsigned stack_start;

    stack_address = (unsigned)(unsigned long)&stack_probe;
    stack_start = (unsigned)(unsigned long)i386_user_return_uarea;
    if (stack_address >= stack_start &&
        stack_address < stack_start + USIZE &&
        i386_user_return_result == 0)
        i386_user_return_result = I386_USER_RETURN_MAGIC;
    else
        i386_user_return_result = EFAULT;
    longjmp((size_t)i386_user_return_uarea,
        &i386_user_return_uarea->u_rsave);
    for (;;)
        __asm__ volatile ("cli; hlt");
}

int
i386_user_return_handle_test(struct i386_trapframe *frame)
{
    if (!i386_user_return_active)
        return 0;
    if (frame->tf_vector != I386_USER_RETURN_VECTOR ||
        frame->tf_cs != I386_USER_CODE_SELECTOR ||
        frame->tf_ss != I386_USER_DATA_SELECTOR ||
        frame->tf_useresp != I386_USER_RETURN_STACK_TOP)
        goto failed;

    if (i386_user_return_phase == 0 &&
        frame->tf_eip == I386_USER_RETURN_CODE + 2u) {
        i386_user_return_phase = 1;
        return 1;
    }
    if (i386_user_return_phase == 1 &&
        frame->tf_eip == I386_USER_RETURN_CODE + 11u &&
        frame->tf_eax == I386_USER_RETURN_MAGIC &&
        i386_user_return_uarea->u_procp->p_sig == 0 &&
        i386_user_return_uarea->u_procp->p_sigmask ==
            sigmask(SIGUSR2) &&
        i386_user_return_next_count == 3u &&
        i386_user_return_deliver_count == 1u &&
        i386_user_return_priority_count == 2u &&
        i386_user_return_enqueue_count == 1u &&
        i386_user_return_switch_count == 1u &&
        i386_user_return_priority == 42 &&
        i386_user_return_uarea->u_ru.ru_nsignals == 1 &&
        i386_user_return_uarea->u_ru.ru_nivcsw == 1) {
        i386_user_return_result = 0;
        i386_privilege_return_to_kernel(frame,
            (unsigned)(unsigned long)i386_user_return_kernel_resume);
        return 1;
    }

failed:
    i386_user_return_result = EFAULT;
    i386_privilege_return_to_kernel(frame,
        (unsigned)(unsigned long)i386_user_return_kernel_resume);
    return 1;
}

int
i386_user_return_selftest(void)
{
    struct proc process;
    const struct sysent *saved_table;
    unsigned saved_table_count;
    vm_pfn_t free_before;
    int resumed;
    volatile int error;

    i386_user_return_uarea = (struct user *)0;
    i386_user_return_vmspace = (struct vmspace *)0;
    i386_user_return_active = 0;
    i386_user_return_phase = 0;
    i386_user_return_result = EFAULT;
    i386_user_return_reschedule = 1;
    i386_user_return_priority = -1;
    i386_user_return_next_count = 0;
    i386_user_return_deliver_count = 0;
    i386_user_return_priority_count = 0;
    i386_user_return_enqueue_count = 0;
    i386_user_return_switch_count = 0;
    bzero(&process, sizeof(process));
    i386_syscall_get_table(&saved_table, &saved_table_count);
    i386_syscall_set_table(i386_user_return_sysent,
        sizeof(i386_user_return_sysent) /
        sizeof(i386_user_return_sysent[0]));
    free_before = vm_page_boot_allocator.vpa_free_count;
    error = 0;

    i386_user_return_uarea = md_uarea_alloc();
    if (i386_user_return_uarea == (struct user *)0) {
        error = ENOMEM;
        goto out;
    }
    error = vmspace_create(&i386_user_return_vmspace);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(i386_user_return_vmspace,
        I386_USER_RETURN_CODE, VM_PAGE_SIZE, VM_PROT_ALL,
        VM_MAP_EXECUTABLE);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(i386_user_return_vmspace,
        I386_USER_RETURN_STACK, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_MAP_STACK);
    if (error != 0)
        goto out;
    error = vmspace_write(i386_user_return_vmspace,
        I386_USER_RETURN_CODE, i386_user_return_code,
        sizeof(i386_user_return_code));
    if (error != 0)
        goto out;
    error = i386_vmspace_activate(i386_user_return_vmspace);
    if (error != 0)
        goto out;

    process.p_vmspace = i386_user_return_vmspace;
    process.p_daddr = I386_USER_RETURN_CODE;
    process.p_saddr = I386_USER_RETURN_STACK;
    process.p_dsize = VM_PAGE_SIZE;
    process.p_ssize = VM_PAGE_SIZE;
    process.p_sig = sigmask(SIGUSR1);
    process.p_sigmask = sigmask(SIGUSR2);
    i386_user_return_uarea->u_procp = &process;
    i386_user_return_uarea->u_dsize = VM_PAGE_SIZE;
    i386_user_return_uarea->u_ssize = VM_PAGE_SIZE;
    i386_user_return_uarea->u_signal[SIGUSR1] =
        (sig_t)I386_USER_RETURN_HANDLER;
    i386_user_return_uarea->u_sigtramp =
        I386_USER_RETURN_TRAMPOLINE;
    i386_user_return_uarea->u_code = I386_USER_RETURN_SIGNAL_CODE;
    md_curuser = i386_user_return_uarea;
    i386_tss_set_kernel_stack((unsigned)(unsigned long)
        i386_user_return_uarea + USIZE);
    i386_user_return_set_ops(&i386_user_return_ops);

    i386_user_return_active = 1;
    resumed = setjmp(&i386_user_return_uarea->u_rsave);
    if (resumed == 0)
        i386_user_enter(I386_USER_RETURN_CODE,
            I386_USER_RETURN_STACK_TOP);
    if (resumed != 1 ||
        i386_user_return_result != I386_USER_RETURN_MAGIC)
        error = EFAULT;

out:
    i386_user_return_active = 0;
    i386_user_return_set_ops((const struct i386_user_return_ops *)0);
    i386_syscall_set_table(saved_table, saved_table_count);
    i386_tss_reset_kernel_stack();
    if (i386_user_return_vmspace != (struct vmspace *)0)
        i386_vmspace_deactivate(i386_user_return_vmspace);
    md_curuser = (struct user *)0;
    if (i386_user_return_uarea != (struct user *)0)
        md_uarea_free(i386_user_return_uarea);
    i386_user_return_uarea = (struct user *)0;
    if (i386_user_return_vmspace != (struct vmspace *)0 &&
        vmspace_destroy(i386_user_return_vmspace) != 0 && error == 0)
        error = EFAULT;
    i386_user_return_vmspace = (struct vmspace *)0;
    if (vm_page_boot_allocator.vpa_free_count != free_before && error == 0)
        error = EFAULT;
    return error;
}
