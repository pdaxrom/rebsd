#include <sys/errno.h>
#include <sys/param.h>
#include <sys/inode.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <machine/layout.h>
#include <vm/vmspace.h>

#include "context.h"
#include "boot.h"
#include "elf_bootstrap.h"
#include "file_bootstrap.h"
#include "initfs.h"
#include "interrupt.h"
#include "privilege.h"
#include "process.h"
#include "syscall.h"
#include "tss.h"
#include "user_bootstrap.h"
#include "user_stack.h"
#include "vfs_bootstrap.h"
#include "vmspace_bootstrap.h"

#define I386_PROCESS_USER_STACK      (I386_USER_VADDR_END - VM_PAGE_SIZE)
#define I386_PROCESS_USER_STACK_TOP  I386_USER_VADDR_END
#define I386_PROCESS_USER_MAGIC      0x70726f63u
#define I386_PROCESS_IDLE_MAGIC      0x69646c65u
#define I386_PROCESS_SCHED_SWITCHES  2u

static struct proc *i386_bootstrap_proc;
static struct user *i386_bootstrap_uarea;
static struct vmspace *i386_bootstrap_vmspace;
static struct user *i386_idle_uarea;
static struct vmspace *i386_idle_vmspace;
static struct proc *i386_fork_child;
static struct inode i386_bootstrap_cdir;
static label_t i386_process_bootstrap_return;
static label_t i386_process_idle_saved;
static struct i386_elf_image i386_bootstrap_user_image;
static struct i386_user_stack i386_bootstrap_user_stack;
static int i386_process_table_ready;
static int i386_bootstrap_ready;
static volatile unsigned i386_process_idle_active;
static volatile unsigned i386_process_idle_result;
static volatile unsigned i386_process_scheduler_switches;
static int i386_process_idle_entered;
static int i386_process_scheduler_tested;
static int i386_process_fork_created;
static int i386_process_fork_parent_ready;
static unsigned i386_process_fork_count;
static int i386_process_zombie_before_wait;
static int i386_process_fork_tested;
static int i386_process_vfs_image;
static int i386_process_vfs_fd_tested;
static volatile unsigned i386_process_user_active;
static volatile unsigned i386_process_user_result;

static void i386_process_idle_entry(void);

static void
i386_process_context_init(label_t *context, struct user *up,
    void (*entry)(void))
{
    unsigned stack_pointer;

    bzero(context, sizeof(*context));
    stack_pointer = (unsigned)(unsigned long)up + USIZE;
    stack_pointer -= sizeof(unsigned);
    *(unsigned *)stack_pointer = 0;
    context->val[I386_LABEL_ESP] = stack_pointer;
    context->val[I386_LABEL_EIP] = (unsigned)(unsigned long)entry;
    context->val[I386_LABEL_EFLAGS] = I386_EFLAGS_RESERVED;
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
        i386_bootstrap_proc != &proc[1] ||
        i386_bootstrap_uarea == (struct user *)0 ||
        i386_bootstrap_vmspace == (struct vmspace *)0 ||
        i386_idle_uarea == (struct user *)0 ||
        i386_idle_vmspace == (struct vmspace *)0)
        return EINVAL;
    kernel_stack = (unsigned)(unsigned long)i386_bootstrap_uarea + USIZE;
    md_uarea_guard_check(i386_bootstrap_uarea);
    md_uarea_guard_check(i386_idle_uarea);
    if (md_curuser != i386_bootstrap_uarea ||
        i386_bootstrap_uarea->u_procp != i386_bootstrap_proc ||
        i386_bootstrap_proc->p_uarea != i386_bootstrap_uarea ||
        i386_bootstrap_proc->p_addr !=
        (size_t)(unsigned long)i386_bootstrap_uarea ||
        i386_bootstrap_proc->p_vmspace != i386_bootstrap_vmspace ||
        i386_bootstrap_proc->p_pid != 1 ||
        i386_bootstrap_proc->p_ppid != 0 ||
        i386_bootstrap_proc->p_pptr != &proc[0] ||
        i386_bootstrap_proc->p_stat != SRUN ||
        (i386_bootstrap_proc->p_flag & SLOAD) == 0 ||
        (i386_bootstrap_proc->p_flag & SSYS) != 0 ||
        nproc != NPROC ||
        proc[0].p_pid != 0 ||
        proc[0].p_ppid != 0 ||
        proc[0].p_pptr != &proc[0] ||
        proc[0].p_stat != SRUN ||
        (proc[0].p_flag & (SLOAD | SSYS)) != (SLOAD | SSYS) ||
        proc[0].p_uarea != i386_idle_uarea ||
        proc[0].p_addr != (size_t)(unsigned long)i386_idle_uarea ||
        proc[0].p_vmspace != i386_idle_vmspace ||
        i386_idle_uarea->u_procp != &proc[0] ||
        proc[0].p_nxt != (struct proc *)0 ||
        zombproc != (struct proc *)0 ||
        pfind(1) != i386_bootstrap_proc ||
        vmspace_current() != i386_bootstrap_vmspace ||
        i386_tss_kernel_stack() != kernel_stack)
        return EFAULT;
    if (!i386_process_fork_created) {
        if (allproc != i386_bootstrap_proc ||
            i386_bootstrap_proc->p_prev != &allproc ||
            i386_bootstrap_proc->p_nxt != &proc[0] ||
            proc[0].p_prev != &i386_bootstrap_proc->p_nxt ||
            freeproc != &proc[2] ||
            pfind(2) != (struct proc *)0 ||
            i386_bootstrap_cdir.i_count != 1)
            return EFAULT;
    } else {
        if (i386_fork_child != &proc[2] ||
            !i386_process_fork_parent_ready ||
            i386_process_fork_count != 2u ||
            !i386_process_zombie_before_wait ||
            !i386_process_fork_tested ||
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
            pfind(2) != (struct proc *)0 ||
            pfind(3) != (struct proc *)0 ||
            i386_bootstrap_cdir.i_count != 1)
            return EFAULT;
    }
    if (i386_process_scheduler_tested &&
        (i386_process_scheduler_switches !=
        I386_PROCESS_SCHED_SWITCHES || qs != (struct proc *)0))
        return EFAULT;
    return 0;
}

static int
i386_process_table_claim_init(struct proc **result)
{
    struct proc *process;

    if (result == (struct proc **)0)
        return EINVAL;
    if (!i386_process_table_ready) {
        pqinit();
        proc[0].p_stat = SRUN;
        proc[0].p_flag = SLOAD | SSYS;
        proc[0].p_nice = NZERO;
        proc[0].p_pptr = &proc[0];
        i386_process_table_ready = 1;
    }
    if (allproc != &proc[0] || freeproc != &proc[1] ||
        zombproc != (struct proc *)0 ||
        pidhash[PIDHASH(1)] != (struct proc *)0)
        return EBUSY;

    process = freeproc;
    freeproc = process->p_nxt;
    bzero(process, sizeof(*process));
    process->p_pid = 1;
    process->p_ppid = 0;
    process->p_pptr = &proc[0];
    process->p_hash = pidhash[PIDHASH(process->p_pid)];
    pidhash[PIDHASH(process->p_pid)] = process;
    process->p_nxt = allproc;
    process->p_nxt->p_prev = &process->p_nxt;
    process->p_prev = &allproc;
    allproc = process;
    *result = process;
    return 0;
}

static void
i386_process_table_release_init(struct proc *process)
{
    struct proc **hash;

    if (process == (struct proc *)0)
        return;
    hash = &pidhash[PIDHASH(process->p_pid)];
    if (*hash == process)
        *hash = process->p_hash;
    if (process->p_prev != (struct proc **)0) {
        *process->p_prev = process->p_nxt;
        if (process->p_nxt != (struct proc *)0)
            process->p_nxt->p_prev = process->p_prev;
    }
    bzero(process, sizeof(*process));
    process->p_nxt = freeproc;
    freeproc = process;
}

int
i386_process_bootstrap(void)
{
    struct proc *process;
    struct user *idle_up;
    struct user *up;
    struct vmspace *idle_vmspace;
    struct vmspace *vmspace;
    int error;
    int index;

    if (i386_bootstrap_ready)
        return i386_process_bootstrap_validate();
    if (md_curuser != (struct user *)0 ||
        vmspace_current() != (struct vmspace *)0)
        return EBUSY;

    idle_up = md_uarea_alloc();
    if (idle_up == (struct user *)0)
        return ENOMEM;
    up = md_uarea_alloc();
    if (up == (struct user *)0) {
        md_uarea_free(idle_up);
        return ENOMEM;
    }
    idle_vmspace = (struct vmspace *)0;
    vmspace = (struct vmspace *)0;
    process = (struct proc *)0;
    error = vmspace_create(&idle_vmspace);
    if (error != 0)
        goto failed;
    error = vmspace_create(&vmspace);
    if (error != 0)
        goto failed;
    error = i386_process_table_claim_init(&process);
    if (error != 0)
        goto failed;

    proc[0].p_uarea = idle_up;
    proc[0].p_addr = (size_t)(unsigned long)idle_up;
    proc[0].p_vmspace = idle_vmspace;
    idle_up->u_procp = &proc[0];
    i386_process_context_init(&idle_up->u_qsave, idle_up,
        i386_process_idle_entry);

    process->p_uarea = up;
    process->p_addr = (size_t)(unsigned long)up;
    process->p_vmspace = vmspace;
    process->p_stat = SRUN;
    process->p_flag = SLOAD;
    process->p_nice = NZERO;

    up->u_procp = process;
    i386_bootstrap_cdir.i_count = 1;
    up->u_cdir = &i386_bootstrap_cdir;
    up->u_cmask = CMASK;
    up->u_lastfile = -1;
    for (index = 1; index < NGROUPS; ++index)
        up->u_groups[index] = NOGROUP;
    for (index = 0; index < RLIM_NLIMITS; ++index) {
        up->u_rlimit[index].rlim_cur = RLIM_INFINITY;
        up->u_rlimit[index].rlim_max = RLIM_INFINITY;
    }

    error = i386_vmspace_activate(vmspace);
    if (error != 0)
        goto failed;
    md_curuser = up;
    i386_tss_set_kernel_stack((unsigned)(unsigned long)up + USIZE);
    i386_bootstrap_proc = process;
    i386_bootstrap_uarea = up;
    i386_bootstrap_vmspace = vmspace;
    i386_idle_uarea = idle_up;
    i386_idle_vmspace = idle_vmspace;
    i386_bootstrap_ready = 1;
    return i386_process_bootstrap_validate();

failed:
    i386_process_table_release_init(process);
    if (vmspace != (struct vmspace *)0)
        (void)vmspace_destroy(vmspace);
    if (idle_vmspace != (struct vmspace *)0)
        (void)vmspace_destroy(idle_vmspace);
    md_uarea_free(up);
    md_uarea_free(idle_up);
    return error;
}

static int
i386_process_idle_validate(void)
{
    volatile unsigned stack_probe;
    unsigned kernel_stack;
    unsigned stack_address;
    unsigned stack_start;

    stack_address = (unsigned)(unsigned long)&stack_probe;
    stack_start = (unsigned)(unsigned long)i386_idle_uarea;
    kernel_stack = stack_start + USIZE;
    if (md_curuser != i386_idle_uarea ||
        i386_idle_uarea->u_procp != &proc[0] ||
        proc[0].p_uarea != i386_idle_uarea ||
        proc[0].p_vmspace != i386_idle_vmspace ||
        vmspace_current() != i386_idle_vmspace ||
        i386_tss_kernel_stack() != kernel_stack ||
        stack_address < stack_start || stack_address >= kernel_stack)
        return EFAULT;
    md_uarea_guard_check(i386_idle_uarea);
    return 0;
}

static int
i386_process_scheduler_fail(unsigned stage)
{
    i386_early_puts("scheduler-switch: failed stage=");
    i386_early_put_hex32(stage);
    i386_early_putc('\n');
    return EFAULT;
}

static void
i386_process_idle_entry(void)
{
    if (i386_process_idle_entered ||
        i386_process_idle_validate() != 0)
        i386_process_idle_result = EFAULT;
    else {
        i386_process_idle_entered = 1;
        i386_process_idle_result = I386_PROCESS_IDLE_MAGIC;
    }
    swtch();
    i386_process_idle_result = EFAULT;
    for (;;)
        __asm__ volatile ("cli; hlt");
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
    i386_process_idle_active = 1;
    setrq(i386_bootstrap_proc);
    if (qs != i386_bootstrap_proc) {
        i386_process_idle_active = 0;
        return i386_process_scheduler_fail(2);
    }
    swtch();
    i386_process_idle_active = 0;
    if (i386_process_idle_result != I386_PROCESS_IDLE_MAGIC)
        return i386_process_scheduler_fail(31);
    if (!i386_process_idle_entered)
        return i386_process_scheduler_fail(32);
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
        i386_idle_uarea->u_qsave.val[I386_LABEL_EIP] == 0 ||
        i386_idle_uarea->u_qsave.val[I386_LABEL_EIP] ==
        (unsigned)(unsigned long)i386_process_idle_entry)
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
        i386_process_idle_result != I386_PROCESS_IDLE_MAGIC ||
        !i386_process_idle_entered ||
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
        i386_bootstrap_cdir.i_count != 1)
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
        i386_process_scheduler_switches == I386_PROCESS_SCHED_SWITCHES &&
        i386_process_bootstrap_validate() == 0)
        i386_process_user_result = I386_PROCESS_USER_MAGIC;
    else
        i386_process_user_result = EFAULT;
    if (i386_process_user_result == I386_PROCESS_USER_MAGIC)
        i386_process_scheduler_tested = 1;

    longjmp((size_t)i386_bootstrap_uarea,
        &i386_process_bootstrap_return);
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
    if (i386_process_fork_count == 0u &&
        frame->tf_ebx == I386_BOOTSTRAP_VFS_FD_MAGIC) {
        if (!i386_process_vfs_image || i386_process_vfs_fd_tested ||
            md_curuser != i386_bootstrap_uarea ||
            u.u_procp != i386_bootstrap_proc ||
            frame->tf_vector != I386_USER_RETURN_VECTOR ||
            frame->tf_cs != I386_USER_CODE_SELECTOR ||
            frame->tf_ss != I386_USER_DATA_SELECTOR ||
            frame->tf_ds != I386_USER_DATA_SELECTOR ||
            frame->tf_useresp !=
            i386_bootstrap_user_stack.ius_stack_pointer ||
            (frame->tf_eflags & I386_EFLAGS_CARRY) != 0 ||
            frame->tf_eax != 0 ||
            i386_file_bootstrap_validate_closed() != 0 ||
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
            i386_bootstrap_user_stack.ius_stack_pointer ||
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
            i386_bootstrap_user_stack.ius_stack_pointer -
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
            i386_bootstrap_cdir.i_count != 2 ||
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
            i386_fork_child->p_vmspace == (struct vmspace *)0 ||
            i386_fork_child->p_uarea == (struct user *)0 ||
            freeproc != &proc[3] ||
            pfind(3) != (struct proc *)0 ||
            i386_bootstrap_cdir.i_count != 1 ||
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
        i386_bootstrap_user_stack.ius_stack_pointer ||
        frame->tf_edx != 0 ||
        frame->tf_ebx != I386_BOOTSTRAP_USER_RETURN_MAGIC ||
        (frame->tf_eflags & I386_EFLAGS_CARRY) != 0 ||
        frame->tf_eax != 3 ||
        i386_bootstrap_proc->p_saddr != I386_PROCESS_USER_STACK ||
        i386_bootstrap_proc->p_ssize != VM_PAGE_SIZE ||
        i386_bootstrap_uarea->u_ssize != VM_PAGE_SIZE ||
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
    static const unsigned char invalid_elf[] = { 0x7f, 'E', 'L', 'F' };
    static const unsigned char invalid_initfs[] = { 'R', 'I', 'F', 'S' };
    static const char *const bootstrap_argv[] = {
        "/sbin/init", "initfs"
    };
    static const char *const bootstrap_envp[] = { "A=i686" };
    struct i386_initfs_file init_file;
    const void *init_image;
    unsigned init_size;
    int resumed;
    volatile int error;

    error = i386_process_bootstrap_validate();
    if (error != 0)
        return error;
    error = i386_syscall_install_production();
    if (error != 0)
        return error;
    if (i386_elf_load_image(i386_bootstrap_vmspace, invalid_elf,
        sizeof(invalid_elf), &i386_bootstrap_user_image) != ENOEXEC)
        return EFAULT;
    if (i386_initfs_find(invalid_initfs, sizeof(invalid_initfs),
        "/sbin/init", &init_file) != ENOEXEC ||
        i386_initfs_find_embedded("/missing", &init_file) != ENOENT)
        return EFAULT;
    error = i386_initfs_find_embedded("/sbin/init", &init_file);
    if (error != 0)
        return error;
    error = i386_vfs_bootstrap_init_image(&init_image, &init_size);
    i386_process_vfs_image = error == 0;
    i386_process_vfs_fd_tested = 0;
    if (error == 0)
        i386_early_puts("process-image: fat-vfs\n");
    else if (error == ENOENT) {
        init_image = init_file.iif_data;
        init_size = init_file.iif_size;
        i386_early_puts("process-image: initfs\n");
    } else
        return error;
    error = i386_elf_load_image(i386_bootstrap_vmspace, init_image,
        init_size, &i386_bootstrap_user_image);
    if (error != 0)
        return error;
    error = vmspace_map_anon(i386_bootstrap_vmspace,
        I386_PROCESS_USER_STACK, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_MAP_STACK);
    if (error != 0)
        goto failed;
    error = i386_user_stack_build(i386_bootstrap_vmspace,
        I386_PROCESS_USER_STACK, I386_PROCESS_USER_STACK_TOP,
        bootstrap_argv,
        sizeof(bootstrap_argv) / sizeof(bootstrap_argv[0]),
        bootstrap_envp,
        sizeof(bootstrap_envp) / sizeof(bootstrap_envp[0]),
        &i386_bootstrap_user_stack);
    if (error != 0)
        goto failed;
    if (vmspace_check(i386_bootstrap_vmspace,
            i386_bootstrap_user_image.iei_entry, 1,
            VM_PROT_EXECUTE) != 0 ||
        vmspace_check(i386_bootstrap_vmspace,
            i386_bootstrap_user_image.iei_entry, 1,
            VM_PROT_WRITE) == 0 ||
        vmspace_check(i386_bootstrap_vmspace, I386_PROCESS_USER_STACK,
            VM_PAGE_SIZE, VM_PROT_READ | VM_PROT_WRITE) != 0 ||
        vmspace_check(i386_bootstrap_vmspace, I386_PROCESS_USER_STACK,
            VM_PAGE_SIZE, VM_PROT_EXECUTE) == 0) {
        error = EFAULT;
        goto failed;
    }
    i386_bootstrap_proc->p_saddr = I386_PROCESS_USER_STACK;
    i386_bootstrap_proc->p_ssize = VM_PAGE_SIZE;
    i386_bootstrap_uarea->u_ssize = VM_PAGE_SIZE;

    i386_process_user_result = EFAULT;
    i386_process_user_active = 1;
    resumed = setjmp(&i386_process_bootstrap_return);
    if (resumed == 0)
        i386_user_enter_exec(i386_bootstrap_user_image.iei_entry,
            i386_bootstrap_user_stack.ius_stack_pointer,
            i386_bootstrap_user_stack.ius_argc,
            i386_bootstrap_user_stack.ius_argv,
            i386_bootstrap_user_stack.ius_envp);
    i386_process_user_active = 0;
    if (resumed != 1 ||
        i386_process_user_result != I386_PROCESS_USER_MAGIC ||
        (i386_process_vfs_image && !i386_process_vfs_fd_tested) ||
        (!i386_process_vfs_image && i386_process_vfs_fd_tested) ||
        i386_file_bootstrap_validate_closed() != 0)
        return EFAULT;
    if (i386_process_vfs_fd_tested) {
        i386_early_puts("syscall-open: ok\n");
        i386_early_puts("syscall-read: ok\n");
        i386_early_puts("syscall-lseek: ok\n");
        i386_early_puts("syscall-close: ok\n");
        i386_early_puts("fd-fat-vfs: ok\n");
    }
    return i386_process_bootstrap_validate();

failed:
    (void)vmspace_unmap(i386_bootstrap_vmspace,
        I386_PROCESS_USER_STACK, VM_PAGE_SIZE);
    (void)i386_elf_unload_image(i386_bootstrap_vmspace,
        &i386_bootstrap_user_image);
    return error;
}
