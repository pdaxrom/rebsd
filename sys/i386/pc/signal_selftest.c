#include <sys/errno.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <vm/vmspace.h>

#include "context.h"
#include "interrupt.h"
#include "signal_machdep.h"
#include "vmspace_bootstrap.h"

#define I386_SIGNAL_CODE       0x53400000u
#define I386_SIGNAL_STACK      0x53500000u
#define I386_SIGNAL_STACK_TOP  (I386_SIGNAL_STACK + VM_PAGE_SIZE)
#define I386_SIGNAL_ALTSTACK   0x53600000u
#define I386_SIGNAL_HANDLER    (I386_SIGNAL_CODE + 0x20u)
#define I386_SIGNAL_TRAMPOLINE (I386_SIGNAL_CODE + 0x40u)
#define I386_SIGNAL_RESUME     (I386_SIGNAL_CODE + 0x60u)

typedef char i386_assert_signal_frame_fits_page[
    sizeof(struct i386_sigframe) < VM_PAGE_SIZE ? 1 : -1];

static int
i386_signal_check_restored(const struct i386_trapframe *frame,
    const struct proc *process, long mask)
{
    unsigned expected_flags;

    expected_flags = I386_EFLAGS_USER_SETTABLE | I386_EFLAGS_RESERVED |
        I386_EFLAGS_INTERRUPT;
    return frame->tf_gs == I386_USER_DATA_SELECTOR &&
        frame->tf_fs == I386_USER_DATA_SELECTOR &&
        frame->tf_es == I386_USER_DATA_SELECTOR &&
        frame->tf_ds == I386_USER_DATA_SELECTOR &&
        frame->tf_edi == 0x11111111u &&
        frame->tf_esi == 0x22222222u &&
        frame->tf_ebp == 0x33333333u &&
        frame->tf_ebx == 0x44444444u &&
        frame->tf_edx == 0x55555555u &&
        frame->tf_ecx == 0x66666666u &&
        frame->tf_eax == 0x77777777u &&
        frame->tf_eip == I386_SIGNAL_RESUME &&
        frame->tf_cs == I386_USER_CODE_SELECTOR &&
        frame->tf_eflags == expected_flags &&
        frame->tf_useresp == I386_SIGNAL_STACK_TOP &&
        frame->tf_ss == I386_USER_DATA_SELECTOR &&
        process->p_sigmask == (mask & ~sigcantmask);
}

int
i386_signal_selftest(void)
{
    struct i386_trapframe *frame;
    struct i386_sigframe signal_frame;
    struct proc process;
    struct user *uarea;
    struct vmspace *vmspace;
    vm_pfn_t free_before;
    vm_vaddr_t regular_frame_address;
    long return_mask;
    int error;

    uarea = (struct user *)0;
    vmspace = (struct vmspace *)0;
    free_before = vm_page_boot_allocator.vpa_free_count;
    error = 0;
    bzero(&process, sizeof(process));
    bzero(&signal_frame, sizeof(signal_frame));

    uarea = md_uarea_alloc();
    if (uarea == (struct user *)0)
        return ENOMEM;
    error = vmspace_create(&vmspace);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(vmspace, I386_SIGNAL_CODE, VM_PAGE_SIZE,
        VM_PROT_ALL, VM_MAP_EXECUTABLE);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(vmspace, I386_SIGNAL_STACK, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_MAP_STACK);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(vmspace, I386_SIGNAL_ALTSTACK, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_MAP_STACK);
    if (error != 0)
        goto out;
    error = i386_vmspace_activate(vmspace);
    if (error != 0)
        goto out;

    process.p_vmspace = vmspace;
    process.p_daddr = I386_SIGNAL_CODE;
    process.p_saddr = I386_SIGNAL_STACK;
    process.p_ssize = VM_PAGE_SIZE;
    uarea->u_procp = &process;
    uarea->u_dsize = VM_PAGE_SIZE;
    uarea->u_ssize = VM_PAGE_SIZE;
    uarea->u_sigtramp = I386_SIGNAL_TRAMPOLINE;
    uarea->u_code = 0x1234;
    frame = (struct i386_trapframe *)((unsigned)(unsigned long)uarea +
        USIZE - sizeof(*frame));
    md_user_frame_exec((int *)frame, I386_SIGNAL_RESUME,
        I386_SIGNAL_STACK_TOP, 0, 0, 0);
    frame->tf_edi = 0x11111111u;
    frame->tf_esi = 0x22222222u;
    frame->tf_ebp = 0x33333333u;
    frame->tf_ebx = 0x44444444u;
    frame->tf_edx = 0x55555555u;
    frame->tf_ecx = 0x66666666u;
    frame->tf_eax = 0x88888888u;
    frame->tf_eflags |= I386_EFLAGS_DIRECTION;
    uarea->u_frame = (int *)frame;
    md_curuser = uarea;

    return_mask = sigmask(SIGUSR2) | sigmask(SIGKILL) | sigmask(SIGSTOP);
    sendsig((sig_t)I386_SIGNAL_HANDLER, SIGUSR1, return_mask);
    regular_frame_address = frame->tf_useresp;
    if (frame->tf_eip != I386_SIGNAL_HANDLER ||
        regular_frame_address < I386_SIGNAL_STACK ||
        regular_frame_address >= I386_SIGNAL_STACK_TOP ||
        (regular_frame_address & 0x0fu) != 0 ||
        (frame->tf_eflags & I386_EFLAGS_DIRECTION) != 0 ||
        (frame->tf_eflags & I386_EFLAGS_INTERRUPT) == 0) {
        error = EFAULT;
        goto out;
    }
    error = vmspace_read(vmspace, regular_frame_address, &signal_frame,
        sizeof(signal_frame));
    if (error != 0)
        goto out;
    if (signal_frame.sf_return != I386_SIGNAL_TRAMPOLINE ||
        signal_frame.sf_signum != SIGUSR1 ||
        signal_frame.sf_code != 0x1234 ||
        signal_frame.sf_context != regular_frame_address +
            offsetof(struct i386_sigframe, sf_sc) ||
        signal_frame.sf_sc.sc_eax != 0x88888888u ||
        signal_frame.sf_sc.sc_eip != I386_SIGNAL_RESUME ||
        signal_frame.sf_sc.sc_esp != I386_SIGNAL_STACK_TOP ||
        signal_frame.sf_sc.sc_onstack != 0) {
        error = EFAULT;
        goto out;
    }

    signal_frame.sf_sc.sc_eax = 0x77777777u;
    signal_frame.sf_sc.sc_eflags = 0xffffffffu;
    error = vmspace_write(vmspace, signal_frame.sf_context,
        &signal_frame.sf_sc, sizeof(signal_frame.sf_sc));
    if (error != 0)
        goto out;
    uarea->u_arg[0] = (int)signal_frame.sf_context;
    uarea->u_error = 0;
    sigreturn();
    if (uarea->u_error != EJUSTRETURN ||
        !i386_signal_check_restored(frame, &process, return_mask)) {
        error = EFAULT;
        goto out;
    }

    signal_frame.sf_sc.sc_cs = I386_KERNEL_CODE_SELECTOR;
    error = vmspace_write(vmspace, signal_frame.sf_context,
        &signal_frame.sf_sc, sizeof(signal_frame.sf_sc));
    if (error != 0)
        goto out;
    uarea->u_error = 0;
    sigreturn();
    if (uarea->u_error != EINVAL ||
        frame->tf_eip != I386_SIGNAL_RESUME) {
        error = EFAULT;
        goto out;
    }

    uarea->u_psflags = SAS_ALTSTACK;
    uarea->u_sigonstack = sigmask(SIGUSR1);
    uarea->u_sigstk.ss_base = (char *)I386_SIGNAL_ALTSTACK;
    uarea->u_sigstk.ss_size = VM_PAGE_SIZE;
    uarea->u_sigstk.ss_flags = 0;
    sendsig((sig_t)I386_SIGNAL_HANDLER, SIGUSR1, return_mask);
    if (frame->tf_useresp < I386_SIGNAL_ALTSTACK ||
        frame->tf_useresp >= I386_SIGNAL_ALTSTACK + VM_PAGE_SIZE ||
        (uarea->u_sigstk.ss_flags & SA_ONSTACK) == 0) {
        error = EFAULT;
        goto out;
    }
    error = vmspace_read(vmspace, frame->tf_useresp, &signal_frame,
        sizeof(signal_frame));
    if (error != 0 || signal_frame.sf_sc.sc_onstack != 0) {
        if (error == 0)
            error = EFAULT;
        goto out;
    }
    uarea->u_arg[0] = (int)signal_frame.sf_context;
    uarea->u_error = 0;
    sigreturn();
    if (uarea->u_error != EJUSTRETURN ||
        (uarea->u_sigstk.ss_flags & SA_ONSTACK) != 0) {
        error = EFAULT;
        goto out;
    }

out:
    if (vmspace != (struct vmspace *)0)
        i386_vmspace_deactivate(vmspace);
    md_curuser = (struct user *)0;
    if (uarea != (struct user *)0)
        md_uarea_free(uarea);
    if (vmspace != (struct vmspace *)0 &&
        vmspace_destroy(vmspace) != 0 && error == 0)
        error = EFAULT;
    if (vm_page_boot_allocator.vpa_free_count != free_before && error == 0)
        error = EFAULT;
    return error;
}
