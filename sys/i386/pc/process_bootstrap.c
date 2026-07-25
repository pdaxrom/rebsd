#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <machine/layout.h>
#include <vm/vmspace.h>

#include "elf_bootstrap.h"
#include "interrupt.h"
#include "privilege.h"
#include "process.h"
#include "syscall.h"
#include "tss.h"
#include "user_bootstrap.h"
#include "vmspace_bootstrap.h"

#define I386_PROCESS_USER_STACK      (I386_USER_VADDR_END - VM_PAGE_SIZE)
#define I386_PROCESS_USER_STACK_TOP  I386_USER_VADDR_END
#define I386_PROCESS_USER_MAGIC      0x70726f63u

static struct proc i386_bootstrap_proc;
static struct user *i386_bootstrap_uarea;
static struct vmspace *i386_bootstrap_vmspace;
static struct i386_elf_image i386_bootstrap_user_image;
static int i386_bootstrap_ready;
static volatile unsigned i386_process_user_active;
static volatile unsigned i386_process_user_result;

int
i386_process_bootstrap_validate(void)
{
    unsigned kernel_stack;

    if (!i386_bootstrap_ready ||
        i386_bootstrap_uarea == (struct user *)0 ||
        i386_bootstrap_vmspace == (struct vmspace *)0)
        return EINVAL;
    kernel_stack = (unsigned)(unsigned long)i386_bootstrap_uarea + USIZE;
    md_uarea_guard_check(i386_bootstrap_uarea);
    if (md_curuser != i386_bootstrap_uarea ||
        i386_bootstrap_uarea->u_procp != &i386_bootstrap_proc ||
        i386_bootstrap_proc.p_uarea != i386_bootstrap_uarea ||
        i386_bootstrap_proc.p_addr !=
        (size_t)(unsigned long)i386_bootstrap_uarea ||
        i386_bootstrap_proc.p_vmspace != i386_bootstrap_vmspace ||
        i386_bootstrap_proc.p_pid != 0 ||
        i386_bootstrap_proc.p_ppid != 0 ||
        i386_bootstrap_proc.p_stat != SRUN ||
        (i386_bootstrap_proc.p_flag & (SLOAD | SSYS)) !=
        (SLOAD | SSYS) ||
        vmspace_current() != i386_bootstrap_vmspace ||
        i386_tss_kernel_stack() != kernel_stack)
        return EFAULT;
    return 0;
}

int
i386_process_bootstrap(void)
{
    struct user *up;
    struct vmspace *vmspace;
    int error;
    int index;

    if (i386_bootstrap_ready)
        return i386_process_bootstrap_validate();
    if (md_curuser != (struct user *)0 ||
        vmspace_current() != (struct vmspace *)0)
        return EBUSY;

    up = md_uarea_alloc();
    if (up == (struct user *)0)
        return ENOMEM;
    vmspace = (struct vmspace *)0;
    error = vmspace_create(&vmspace);
    if (error != 0)
        goto failed;

    bzero(&i386_bootstrap_proc, sizeof(i386_bootstrap_proc));
    i386_bootstrap_proc.p_uarea = up;
    i386_bootstrap_proc.p_addr = (size_t)(unsigned long)up;
    i386_bootstrap_proc.p_vmspace = vmspace;
    i386_bootstrap_proc.p_stat = SRUN;
    i386_bootstrap_proc.p_flag = SLOAD | SSYS;
    i386_bootstrap_proc.p_nice = NZERO;

    up->u_procp = &i386_bootstrap_proc;
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
    i386_bootstrap_uarea = up;
    i386_bootstrap_vmspace = vmspace;
    i386_bootstrap_ready = 1;
    return i386_process_bootstrap_validate();

failed:
    if (vmspace != (struct vmspace *)0)
        (void)vmspace_destroy(vmspace);
    md_uarea_free(up);
    return error;
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
        i386_process_bootstrap_validate() == 0)
        i386_process_user_result = I386_PROCESS_USER_MAGIC;
    else
        i386_process_user_result = EFAULT;

    longjmp((size_t)i386_bootstrap_uarea,
        &i386_bootstrap_uarea->u_rsave);
    for (;;)
        __asm__ volatile ("cli; hlt");
}

int
i386_process_handle_return(struct i386_trapframe *frame)
{
    unsigned expected_stack;

    if (!i386_process_user_active)
        return 0;

    expected_stack = (unsigned)(unsigned long)i386_bootstrap_uarea + USIZE;
    if (frame->tf_vector != I386_USER_RETURN_VECTOR ||
        frame->tf_cs != I386_USER_CODE_SELECTOR ||
        frame->tf_ss != I386_USER_DATA_SELECTOR ||
        frame->tf_ds != I386_USER_DATA_SELECTOR ||
        frame->tf_useresp != I386_PROCESS_USER_STACK_TOP ||
        frame->tf_eax != 0 ||
        frame->tf_edx != 0 ||
        frame->tf_ebx != I386_BOOTSTRAP_USER_RETURN_MAGIC ||
        (frame->tf_eflags & I386_EFLAGS_CARRY) != 0 ||
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
    error = i386_elf_load_bootstrap_user(i386_bootstrap_vmspace,
        &i386_bootstrap_user_image);
    if (error != 0)
        return error;
    error = vmspace_map_anon(i386_bootstrap_vmspace,
        I386_PROCESS_USER_STACK, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_MAP_STACK);
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

    i386_process_user_result = EFAULT;
    i386_process_user_active = 1;
    resumed = setjmp(&i386_bootstrap_uarea->u_rsave);
    if (resumed == 0)
        i386_user_enter(i386_bootstrap_user_image.iei_entry,
            I386_PROCESS_USER_STACK_TOP);
    i386_process_user_active = 0;
    if (resumed != 1 ||
        i386_process_user_result != I386_PROCESS_USER_MAGIC)
        return EFAULT;
    return i386_process_bootstrap_validate();

failed:
    (void)vmspace_unmap(i386_bootstrap_vmspace,
        I386_PROCESS_USER_STACK, VM_PAGE_SIZE);
    (void)i386_elf_unload_image(i386_bootstrap_vmspace,
        &i386_bootstrap_user_image);
    return error;
}
