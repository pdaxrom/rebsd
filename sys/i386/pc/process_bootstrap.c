#include <sys/errno.h>
#include <sys/param.h>
#include <sys/exec.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <machine/layout.h>
#include <vm/vmspace.h>

#include "context.h"
#include "boot.h"
#include "interrupt.h"
#include "privilege.h"
#include "process.h"
#include "syscall.h"
#include "tss.h"
#include "user_bootstrap.h"
#include "vmspace_bootstrap.h"

#define I386_PROCESS_USER_MAGIC      0x70726f63u
#define I386_PROCESS_SCHED_SWITCHES  2u

static struct proc *i386_bootstrap_proc;
static struct user *i386_bootstrap_uarea;
static struct vmspace *i386_bootstrap_vmspace;
static struct user *i386_idle_uarea;

static int
i386_process_file_table_closed(void)
{
    int index;

    if (u.u_lastfile != -1)
        return 0;
    for (index = 0; index < NOFILE; ++index)
        if (u.u_ofile[index] != (struct file *)0)
            return 0;
    for (index = 0; index < NFILE; ++index)
        if (file[index].f_count != 0)
            return 0;
    return 1;
}

static struct vmspace *i386_idle_vmspace;
static struct proc *i386_fork_child;
static label_t i386_process_idle_saved;
static struct exec_params i386_bootstrap_exec;
static int i386_bootstrap_ready;
static volatile unsigned i386_process_scheduler_switches;
static int i386_process_scheduler_tested;
static int i386_process_fork_created;
static int i386_process_fork_parent_ready;
static unsigned i386_process_fork_count;
static int i386_process_zombie_before_wait;
static int i386_process_fork_tested;
static int i386_process_vfs_fd_tested;
static volatile unsigned i386_process_user_active;
static volatile unsigned i386_process_user_result;

static int
i386_process_root_refs(unsigned count)
{
    return rootdir != (struct inode *)0 && u.u_cdir == rootdir &&
        rootdir->i_count == count;
}

static int
i386_process_root_state(void)
{
    if (rootdir == (struct inode *)0)
        return u.u_cdir == (struct inode *)0;
    return i386_process_root_refs(2u);
}

static int
i386_process_context_equal(const label_t *left, const label_t *right)
{
    unsigned index;

    for (index = 0;
        index < sizeof(left->val) / sizeof(left->val[0]); ++index) {
        if (left->val[index] != right->val[index])
            return 0;
    }
    return 1;
}

int
i386_process_bootstrap_validate(void)
{
    unsigned kernel_stack;

    if (!i386_bootstrap_ready ||
        i386_idle_uarea == (struct user *)0 ||
        i386_idle_vmspace == (struct vmspace *)0)
        return EINVAL;
    kernel_stack = (unsigned)(unsigned long)i386_idle_uarea + USIZE;
    md_uarea_guard_check(i386_idle_uarea);
    if (md_curuser != i386_idle_uarea ||
        i386_idle_uarea->u_procp != &proc[0] ||
        nproc != NPROC ||
        proc[0].p_pid != 0 ||
        proc[0].p_ppid != 0 ||
        proc[0].p_pptr != &proc[0] ||
        proc[0].p_stat != SRUN ||
        (proc[0].p_flag & (SLOAD | SSYS)) != (SLOAD | SSYS) ||
        proc[0].p_uarea != i386_idle_uarea ||
        proc[0].p_addr != (size_t)(unsigned long)i386_idle_uarea ||
        proc[0].p_vmspace != i386_idle_vmspace ||
        vmspace_current() != i386_idle_vmspace ||
        i386_tss_kernel_stack() != kernel_stack)
        return EFAULT;
    if (i386_bootstrap_proc == (struct proc *)0) {
        if (allproc != &proc[0] ||
            proc[0].p_prev != &allproc ||
            proc[0].p_nxt != (struct proc *)0 ||
            freeproc != &proc[1] ||
            zombproc != (struct proc *)0 ||
            qs != (struct proc *)0 ||
            pfind(1) != (struct proc *)0 ||
            !i386_process_root_state())
            return EFAULT;
        return 0;
    }

    if (i386_bootstrap_proc != &proc[1] ||
        i386_bootstrap_uarea == (struct user *)0 ||
        i386_bootstrap_vmspace == (struct vmspace *)0 ||
        i386_bootstrap_proc->p_uarea != i386_bootstrap_uarea)
        return EINVAL;
    md_uarea_guard_check(i386_bootstrap_uarea);
    if (i386_bootstrap_uarea->u_procp != i386_bootstrap_proc ||
        i386_bootstrap_proc->p_addr !=
        (size_t)(unsigned long)i386_bootstrap_uarea ||
        i386_bootstrap_proc->p_vmspace != i386_bootstrap_vmspace ||
        i386_bootstrap_proc->p_pid != 1 ||
        i386_bootstrap_proc->p_ppid != 0 ||
        i386_bootstrap_proc->p_pptr != &proc[0] ||
        i386_bootstrap_proc->p_stat != SRUN ||
        (i386_bootstrap_proc->p_flag & (SLOAD | SSWAP)) != SLOAD ||
        (i386_bootstrap_proc->p_flag & SSYS) != 0 ||
        i386_fork_child != &proc[2] ||
        !i386_process_fork_parent_ready ||
        i386_process_fork_count != 2u ||
        !i386_process_zombie_before_wait ||
        !i386_process_fork_tested ||
        !i386_process_scheduler_tested ||
        i386_process_scheduler_switches != I386_PROCESS_SCHED_SWITCHES ||
        allproc != i386_bootstrap_proc ||
        i386_bootstrap_proc->p_prev != &allproc ||
        i386_bootstrap_proc->p_nxt != &proc[0] ||
        proc[0].p_prev != &i386_bootstrap_proc->p_nxt ||
        freeproc != i386_fork_child ||
        i386_fork_child->p_nxt != &proc[3] ||
        i386_fork_child->p_stat != 0 ||
        i386_fork_child->p_pid != 0 ||
        i386_fork_child->p_ppid != 0 ||
        i386_fork_child->p_pptr != (struct proc *)0 ||
        i386_fork_child->p_uarea != (struct user *)0 ||
        i386_fork_child->p_vmspace != (struct vmspace *)0 ||
        pfind(1) != i386_bootstrap_proc ||
        pfind(2) != (struct proc *)0 ||
        pfind(3) != (struct proc *)0 ||
        qs != i386_bootstrap_proc ||
        i386_bootstrap_proc->p_link != (struct proc *)0 ||
        !i386_process_root_refs(3u))
        return EFAULT;
    return 0;
}

int
i386_process_bootstrap(void)
{
    struct user *idle_up;
    int error;

    if (i386_bootstrap_ready)
        return i386_process_bootstrap_validate();
    if (md_curuser != (struct user *)0 ||
        vmspace_current() != (struct vmspace *)0)
        return EBUSY;

    idle_up = md_uarea_alloc();
    if (idle_up == (struct user *)0)
        return ENOMEM;
    md_curuser = idle_up;
    error = proc0_bootstrap(idle_up);
    if (error != 0) {
        md_curuser = (struct user *)0;
        md_uarea_free(idle_up);
        return error;
    }
    i386_idle_vmspace = proc[0].p_vmspace;
    i386_idle_uarea = idle_up;
    i386_tss_set_kernel_stack((unsigned)(unsigned long)idle_up + USIZE);
    i386_bootstrap_ready = 1;
    return i386_process_bootstrap_validate();
}

void
i386_process_enter_proc0(void (*entry)(void))
{
    if (entry == (void (*)(void))0 ||
        i386_process_bootstrap_validate() != 0) {
        for (;;)
            __asm__ volatile ("cli; hlt");
    }
    i386_context_enter(i386_idle_uarea, entry);
}

static int
i386_process_scheduler_fail(unsigned stage)
{
    i386_early_puts("scheduler-switch: failed stage=");
    i386_early_put_hex32(stage);
    i386_early_putc('\n');
    return EFAULT;
}

static int
i386_process_scheduler_roundtrip(void)
{
    unsigned idle_stack;
    int first;

    if (md_curuser != i386_bootstrap_uarea ||
        vmspace_current() != i386_bootstrap_vmspace ||
        qs != (struct proc *)0 ||
        i386_process_scheduler_switches >= I386_PROCESS_SCHED_SWITCHES)
        return i386_process_scheduler_fail(1);

    first = i386_process_scheduler_switches == 0;
    setrq(i386_bootstrap_proc);
    if (qs != i386_bootstrap_proc) {
        return i386_process_scheduler_fail(2);
    }
    swtch();
    if (qs != (struct proc *)0)
        return i386_process_scheduler_fail(33);
    if (md_curuser != i386_bootstrap_uarea)
        return i386_process_scheduler_fail(34);
    if (vmspace_current() != i386_bootstrap_vmspace)
        return i386_process_scheduler_fail(35);
    if (i386_tss_kernel_stack() !=
        (unsigned)(unsigned long)i386_bootstrap_uarea + USIZE)
        return i386_process_scheduler_fail(36);
    md_uarea_guard_check(i386_bootstrap_uarea);

    idle_stack = (unsigned)(unsigned long)i386_idle_uarea;
    if (i386_idle_uarea->u_qsave.val[I386_LABEL_ESP] < idle_stack ||
        i386_idle_uarea->u_qsave.val[I386_LABEL_ESP] >=
        idle_stack + USIZE ||
        i386_idle_uarea->u_qsave.val[I386_LABEL_EIP] == 0)
        return i386_process_scheduler_fail(4);
    if (first)
        bcopy(&i386_idle_uarea->u_qsave, &i386_process_idle_saved,
            sizeof(i386_process_idle_saved));
    else if (!i386_process_context_equal(&i386_idle_uarea->u_qsave,
        &i386_process_idle_saved))
        return i386_process_scheduler_fail(5);
    ++i386_process_scheduler_switches;
    return 0;
}

static int
i386_process_fork_roundtrip(void)
{
    if (!i386_process_fork_created ||
        !i386_process_fork_parent_ready ||
        i386_process_fork_count != 2u ||
        !i386_process_zombie_before_wait ||
        i386_fork_child != &proc[2] ||
        qs != (struct proc *)0 ||
        md_curuser != i386_bootstrap_uarea ||
        vmspace_current() != i386_bootstrap_vmspace ||
        i386_tss_kernel_stack() !=
        (unsigned)(unsigned long)i386_bootstrap_uarea + USIZE ||
        allproc != i386_bootstrap_proc ||
        i386_bootstrap_proc->p_nxt != &proc[0] ||
        zombproc != (struct proc *)0 ||
        freeproc != i386_fork_child ||
        i386_fork_child->p_stat != 0 ||
        i386_fork_child->p_pid != 0 ||
        i386_fork_child->p_ppid != 0 ||
        i386_fork_child->p_pptr != (struct proc *)0 ||
        i386_fork_child->p_vmspace != (struct vmspace *)0 ||
        i386_fork_child->p_uarea != (struct user *)0 ||
        pfind(2) != (struct proc *)0 ||
        pfind(3) != (struct proc *)0 ||
        !i386_process_root_refs(3u))
        return i386_process_scheduler_fail(0x41);
    i386_process_fork_tested = 1;
    return 0;
}

static void
i386_process_user_kernel_return(void)
{
    volatile unsigned stack_probe;
    unsigned stack_address;
    unsigned stack_start;
    unsigned stack_end;

    stack_address = (unsigned)(unsigned long)&stack_probe;
    stack_start = (unsigned)(unsigned long)i386_bootstrap_uarea;
    stack_end = stack_start + USIZE;
    if (i386_process_user_active &&
        i386_process_user_result == 0 &&
        stack_address >= stack_start && stack_address < stack_end &&
        i386_process_fork_roundtrip() == 0 &&
        i386_process_scheduler_roundtrip() == 0 &&
        i386_process_scheduler_roundtrip() == 0 &&
        i386_process_scheduler_switches == I386_PROCESS_SCHED_SWITCHES)
        i386_process_user_result = I386_PROCESS_USER_MAGIC;
    else
        i386_process_user_result = EFAULT;
    if (i386_process_user_result == I386_PROCESS_USER_MAGIC)
        i386_process_scheduler_tested = 1;

    setrq(i386_bootstrap_proc);
    setrq(&proc[0]);
    swtch();
    for (;;)
        __asm__ volatile ("cli; hlt");
}

int
i386_process_handle_return(struct i386_trapframe *frame)
{
    unsigned expected_stack;
    int saved_priority;

    if (!i386_process_user_active)
        return 0;

    expected_stack = (unsigned)(unsigned long)i386_bootstrap_uarea + USIZE;
    if (i386_bootstrap_exec.vmspace == (struct vmspace *)0 &&
        i386_process_fork_count == 0u &&
        frame->tf_ebx == I386_BOOTSTRAP_FORK_PARENT_MAGIC) {
        if (md_curuser != i386_bootstrap_uarea ||
            u.u_procp != i386_bootstrap_proc)
            return 0;
        i386_bootstrap_vmspace = i386_bootstrap_proc->p_vmspace;
        if (i386_bootstrap_vmspace == (struct vmspace *)0 ||
            vmspace_current() != i386_bootstrap_vmspace)
            return 0;
        i386_bootstrap_exec.entry = frame->tf_eip;
        i386_bootstrap_exec.stack_pointer = frame->tf_useresp;
        i386_bootstrap_exec.stack.vaddr =
            (caddr_t)(unsigned long)i386_bootstrap_proc->p_saddr;
        i386_bootstrap_exec.stack.len = i386_bootstrap_proc->p_ssize;
        i386_bootstrap_exec.vmspace = i386_bootstrap_vmspace;
        i386_early_puts("vfs-exec-init: ok\n");
    }
    if (i386_process_fork_count == 1u &&
        frame->tf_ebx == I386_BOOTSTRAP_VFS_FD_MAGIC) {
        if (i386_process_vfs_fd_tested ||
            md_curuser != i386_bootstrap_uarea ||
            u.u_procp != i386_bootstrap_proc ||
            frame->tf_vector != I386_USER_RETURN_VECTOR ||
            frame->tf_cs != I386_USER_CODE_SELECTOR ||
            frame->tf_ss != I386_USER_DATA_SELECTOR ||
            frame->tf_ds != I386_USER_DATA_SELECTOR ||
            frame->tf_useresp !=
            i386_bootstrap_exec.stack_pointer -
            sizeof(unsigned) ||
            (frame->tf_eflags & I386_EFLAGS_CARRY) != 0 ||
            frame->tf_eax != 0 ||
            !i386_process_file_table_closed() ||
            pfind(2) != (struct proc *)0 ||
            freeproc != i386_fork_child ||
            i386_fork_child->p_stat != 0 ||
            i386_fork_child->p_pid != 0 ||
            i386_fork_child->p_uarea != (struct user *)0 ||
            i386_fork_child->p_vmspace != (struct vmspace *)0 ||
            !i386_process_root_refs(3u) ||
            vmspace_current() != i386_bootstrap_vmspace ||
            i386_tss_kernel_stack() != expected_stack) {
            i386_process_user_result = EFAULT;
            i386_privilege_return_to_kernel(frame,
                (unsigned)(unsigned long)i386_process_user_kernel_return);
            return 1;
        }
        i386_process_vfs_fd_tested = 1;
        return 1;
    }
    if (i386_process_fork_count == 0u &&
        frame->tf_ebx == I386_BOOTSTRAP_FORK_PARENT_MAGIC) {
        if (md_curuser != i386_bootstrap_uarea ||
            u.u_procp != i386_bootstrap_proc ||
            frame->tf_vector != I386_USER_RETURN_VECTOR ||
            frame->tf_cs != I386_USER_CODE_SELECTOR ||
            frame->tf_ss != I386_USER_DATA_SELECTOR ||
            frame->tf_ds != I386_USER_DATA_SELECTOR ||
            frame->tf_useresp !=
            i386_bootstrap_exec.stack_pointer ||
            (frame->tf_eflags & I386_EFLAGS_CARRY) != 0 ||
            frame->tf_eax != 2 ||
            qs != &proc[2] ||
            proc[2].p_pid != 2 ||
            proc[2].p_ppid != 1 ||
            proc[2].p_pptr != i386_bootstrap_proc ||
            proc[2].p_stat != SRUN ||
            (proc[2].p_flag & (SLOAD | SSWAP)) != (SLOAD | SSWAP) ||
            proc[2].p_vmspace == i386_bootstrap_vmspace ||
            proc[2].p_uarea == i386_bootstrap_uarea ||
            vmspace_current() != i386_bootstrap_vmspace ||
            i386_tss_kernel_stack() != expected_stack) {
            i386_process_user_result = EFAULT;
            i386_privilege_return_to_kernel(frame,
                (unsigned)(unsigned long)i386_process_user_kernel_return);
            return 1;
        }
        md_uarea_guard_check(proc[2].p_uarea);
        if (u.u_lastfile != 0 || proc[2].p_uarea->u_lastfile != 0 ||
            u.u_ofile[0] == (struct file *)0 ||
            proc[2].p_uarea->u_ofile[0] != u.u_ofile[0] ||
            u.u_ofile[0]->f_count != 2 ||
            u.u_ofile[0]->f_offset != 4) {
            i386_process_user_result = EFAULT;
            i386_privilege_return_to_kernel(frame,
                (unsigned)(unsigned long)i386_process_user_kernel_return);
            return 1;
        }
        i386_fork_child = &proc[2];
        i386_process_fork_created = 1;
        i386_process_fork_parent_ready = 1;
        i386_process_fork_count = 1u;
        return 1;
    }

    if (i386_process_fork_count == 1u &&
        frame->tf_ebx == I386_BOOTSTRAP_FORK2_PARENT_MAGIC) {
        if (md_curuser != i386_bootstrap_uarea ||
            u.u_procp != i386_bootstrap_proc ||
            frame->tf_vector != I386_USER_RETURN_VECTOR ||
            frame->tf_cs != I386_USER_CODE_SELECTOR ||
            frame->tf_ss != I386_USER_DATA_SELECTOR ||
            frame->tf_ds != I386_USER_DATA_SELECTOR ||
            frame->tf_useresp !=
            i386_bootstrap_exec.stack_pointer -
            sizeof(unsigned) ||
            (frame->tf_eflags & I386_EFLAGS_CARRY) != 0 ||
            frame->tf_eax != 3 ||
            qs != i386_fork_child ||
            i386_fork_child->p_pid != 3 ||
            i386_fork_child->p_ppid != 1 ||
            i386_fork_child->p_pptr != i386_bootstrap_proc ||
            i386_fork_child->p_stat != SRUN ||
            (i386_fork_child->p_flag & (SLOAD | SSWAP)) !=
            (SLOAD | SSWAP) ||
            i386_fork_child->p_vmspace == i386_bootstrap_vmspace ||
            i386_fork_child->p_uarea == i386_bootstrap_uarea ||
            freeproc != &proc[3] ||
            pfind(2) != (struct proc *)0 ||
            pfind(3) != i386_fork_child ||
            !i386_process_root_refs(4u) ||
            vmspace_current() != i386_bootstrap_vmspace ||
            i386_tss_kernel_stack() != expected_stack) {
            i386_process_user_result = EFAULT;
            i386_privilege_return_to_kernel(frame,
                (unsigned)(unsigned long)i386_process_user_kernel_return);
            return 1;
        }
        md_uarea_guard_check(i386_fork_child->p_uarea);
        saved_priority = i386_bootstrap_proc->p_pri;
        i386_bootstrap_proc->p_pri = i386_fork_child->p_pri + 1;
        setrq(i386_bootstrap_proc);
        swtch();
        i386_bootstrap_proc->p_pri = saved_priority;
        if (md_curuser != i386_bootstrap_uarea ||
            u.u_procp != i386_bootstrap_proc ||
            qs != (struct proc *)0 ||
            allproc != i386_bootstrap_proc ||
            i386_bootstrap_proc->p_nxt != &proc[0] ||
            zombproc != i386_fork_child ||
            i386_fork_child->p_stat != SZOMB ||
            i386_fork_child->p_pid != 3 ||
            i386_fork_child->p_ppid != 1 ||
            i386_fork_child->p_pptr != i386_bootstrap_proc ||
            i386_fork_child->p_vmspace != (struct vmspace *)0 ||
            i386_fork_child->p_uarea == (struct user *)0 ||
            freeproc != &proc[3] ||
            pfind(3) != (struct proc *)0 ||
            !i386_process_root_refs(3u) ||
            vmspace_current() != i386_bootstrap_vmspace ||
            i386_tss_kernel_stack() != expected_stack) {
            i386_process_user_result = EFAULT;
            i386_privilege_return_to_kernel(frame,
                (unsigned)(unsigned long)i386_process_user_kernel_return);
            return 1;
        }
        md_uarea_guard_check(i386_fork_child->p_uarea);
        i386_process_fork_count = 2u;
        i386_process_zombie_before_wait = 1;
        return 1;
    }

    if (!i386_process_fork_parent_ready ||
        i386_process_fork_count != 2u ||
        !i386_process_zombie_before_wait ||
        md_curuser != i386_bootstrap_uarea ||
        frame->tf_vector != I386_USER_RETURN_VECTOR ||
        frame->tf_cs != I386_USER_CODE_SELECTOR ||
        frame->tf_ss != I386_USER_DATA_SELECTOR ||
        frame->tf_ds != I386_USER_DATA_SELECTOR ||
        frame->tf_useresp !=
        i386_bootstrap_exec.stack_pointer ||
        frame->tf_edx != 0 ||
        frame->tf_ebx != I386_BOOTSTRAP_USER_RETURN_MAGIC ||
        (frame->tf_eflags & I386_EFLAGS_CARRY) != 0 ||
        frame->tf_eax != 3 ||
        i386_bootstrap_proc->p_saddr !=
        (size_t)i386_bootstrap_exec.stack.vaddr ||
        i386_bootstrap_proc->p_ssize !=
        i386_bootstrap_exec.stack.len ||
        i386_bootstrap_uarea->u_ssize !=
        i386_bootstrap_exec.stack.len ||
        vmspace_check(i386_bootstrap_vmspace, frame->tf_eip, 1,
        VM_PROT_EXECUTE) != 0 ||
        i386_tss_kernel_stack() != expected_stack)
        i386_process_user_result = EFAULT;
    else
        i386_process_user_result = 0;

    i386_privilege_return_to_kernel(frame,
        (unsigned)(unsigned long)i386_process_user_kernel_return);
    return 1;
}

int
i386_process_bootstrap_user_probe(void)
{
    volatile unsigned stack_probe;
    unsigned stack_address;
    unsigned stack_start;
    int error;

    error = i386_process_bootstrap_validate();
    if (error != 0)
        return error;
    stack_address = (unsigned)(unsigned long)&stack_probe;
    stack_start = (unsigned)(unsigned long)i386_idle_uarea;
    if (stack_address < stack_start ||
        stack_address >= stack_start + USIZE ||
        !i386_process_root_refs(2u))
        return EFAULT;
    error = i386_syscall_install_production();
    if (error != 0)
        return error;

    bzero(&i386_bootstrap_exec, sizeof(i386_bootstrap_exec));
    i386_process_fork_created = 0;
    i386_process_fork_parent_ready = 0;
    i386_process_fork_count = 0;
    i386_process_zombie_before_wait = 0;
    i386_process_fork_tested = 0;
    i386_process_vfs_fd_tested = 0;
    i386_process_scheduler_switches = 0;
    i386_process_scheduler_tested = 0;
    i386_process_user_result = EFAULT;
    i386_process_user_active = 1;
    i386_early_puts("process-image: vfs\n");

    if (newproc(0) != 0) {
        i386_process_user_active = 0;
        return ENOMEM;
    }
    i386_bootstrap_proc = pfind(1);
    if (i386_bootstrap_proc != &proc[1] ||
        i386_bootstrap_proc->p_uarea == (struct user *)0 ||
        i386_bootstrap_proc->p_vmspace == (struct vmspace *)0 ||
        qs != i386_bootstrap_proc ||
        !i386_process_root_refs(3u)) {
        i386_process_user_active = 0;
        return EFAULT;
    }
    i386_bootstrap_uarea = i386_bootstrap_proc->p_uarea;
    i386_bootstrap_vmspace = i386_bootstrap_proc->p_vmspace;
    swtch();

    i386_process_user_active = 0;
    if (i386_process_user_result != I386_PROCESS_USER_MAGIC ||
        !i386_process_vfs_fd_tested ||
        !i386_process_file_table_closed() ||
        md_curuser != i386_idle_uarea ||
        vmspace_current() != i386_idle_vmspace ||
        qs != i386_bootstrap_proc)
        return EFAULT;
    if (i386_process_vfs_fd_tested) {
        i386_early_puts("syscall-open: ok\n");
        i386_early_puts("syscall-read: ok\n");
        i386_early_puts("syscall-lseek: ok\n");
        i386_early_puts("syscall-close: ok\n");
        i386_early_puts("fd-vfs: ok\n");
        i386_early_puts("fd-fork-shared-offset: ok\n");
        i386_early_puts("fd-exit-close: ok\n");
    }
    return i386_process_bootstrap_validate();
}
