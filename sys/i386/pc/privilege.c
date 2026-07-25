#include <sys/errno.h>
#include <sys/param.h>
#include <sys/user.h>
#include <vm/vmspace.h>

#include "context.h"
#include "interrupt.h"
#include "privilege.h"
#include "tss.h"
#include "vmspace_bootstrap.h"

#define I386_PRIVILEGE_CODE       0x53000000u
#define I386_PRIVILEGE_STACK      0x53100000u
#define I386_PRIVILEGE_STACK_TOP  (I386_PRIVILEGE_STACK + VM_PAGE_SIZE)
#define I386_PRIVILEGE_RESULT     0x72696e67u

static const unsigned char i386_privilege_code[] = {
    0xcc,
    0xcd, 0x30,
    0x0f, 0x0b
};

static struct user *i386_privilege_uarea;
static struct vmspace *i386_privilege_vmspace;
static volatile unsigned i386_privilege_active;
static volatile unsigned i386_privilege_result;
static unsigned i386_privilege_breakpoints;

static void
i386_privilege_kernel_return(void)
{
    volatile unsigned stack_probe;
    unsigned stack_address;
    unsigned stack_start;
    unsigned stack_end;

    stack_address = (unsigned)(unsigned long)&stack_probe;
    stack_start = (unsigned)(unsigned long)i386_privilege_uarea;
    stack_end = stack_start + USIZE;
    if (md_curuser == i386_privilege_uarea &&
        vmspace_current() == i386_privilege_vmspace &&
        stack_address >= stack_start && stack_address < stack_end &&
        i386_breakpoint_count() == i386_privilege_breakpoints + 1u &&
        i386_privilege_result == 0)
        i386_privilege_result = I386_PRIVILEGE_RESULT;
    else
        i386_privilege_result = EFAULT;

    longjmp((size_t)i386_privilege_uarea,
        &i386_privilege_uarea->u_qsave);
    for (;;)
        __asm__ volatile ("cli; hlt");
}

int
i386_privilege_handle_return(struct i386_trapframe *frame)
{
    unsigned expected_stack;

    if (!i386_privilege_active)
        return 0;

    expected_stack = (unsigned)(unsigned long)i386_privilege_uarea + USIZE;
    if (frame->tf_vector != I386_USER_RETURN_VECTOR ||
        frame->tf_cs != I386_USER_CODE_SELECTOR ||
        frame->tf_ss != I386_USER_DATA_SELECTOR ||
        frame->tf_ds != I386_USER_DATA_SELECTOR ||
        frame->tf_eip != I386_PRIVILEGE_CODE + 3u ||
        frame->tf_useresp != I386_PRIVILEGE_STACK_TOP ||
        i386_tss_kernel_stack() != expected_stack)
        i386_privilege_result = EFAULT;
    else
        i386_privilege_result = 0;

    i386_privilege_return_to_kernel(frame,
        (unsigned)(unsigned long)i386_privilege_kernel_return);
    return 1;
}

void
i386_privilege_return_to_kernel(struct i386_trapframe *frame,
    unsigned entry)
{
    frame->tf_gs = I386_KERNEL_DATA_SELECTOR;
    frame->tf_fs = I386_KERNEL_DATA_SELECTOR;
    frame->tf_es = I386_KERNEL_DATA_SELECTOR;
    frame->tf_ds = I386_KERNEL_DATA_SELECTOR;
    frame->tf_eip = entry;
    frame->tf_cs = I386_KERNEL_CODE_SELECTOR;
    frame->tf_eflags = I386_EFLAGS_RESERVED;
    frame->tf_useresp = 0;
    frame->tf_ss = 0;
}

int
i386_privilege_selftest(void)
{
    vm_pfn_t free_before;
    int resumed;
    volatile int error;

    i386_privilege_uarea = (struct user *)0;
    i386_privilege_vmspace = (struct vmspace *)0;
    i386_privilege_active = 0;
    i386_privilege_result = EFAULT;
    free_before = vm_page_boot_allocator.vpa_free_count;
    error = 0;

    i386_privilege_uarea = md_uarea_alloc();
    if (i386_privilege_uarea == (struct user *)0) {
        error = ENOMEM;
        goto out;
    }
    error = vmspace_create(&i386_privilege_vmspace);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(i386_privilege_vmspace,
        I386_PRIVILEGE_CODE, VM_PAGE_SIZE, VM_PROT_ALL,
        VM_MAP_EXECUTABLE);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(i386_privilege_vmspace,
        I386_PRIVILEGE_STACK, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_MAP_STACK);
    if (error != 0)
        goto out;
    error = vmspace_write(i386_privilege_vmspace, I386_PRIVILEGE_CODE,
        i386_privilege_code, sizeof(i386_privilege_code));
    if (error != 0)
        goto out;
    error = i386_vmspace_activate(i386_privilege_vmspace);
    if (error != 0)
        goto out;

    md_curuser = i386_privilege_uarea;
    i386_tss_set_kernel_stack((unsigned)(unsigned long)
        i386_privilege_uarea + USIZE);
    i386_privilege_breakpoints = i386_breakpoint_count();
    i386_privilege_active = 1;
    resumed = setjmp(&i386_privilege_uarea->u_qsave);
    if (resumed == 0)
        i386_user_enter(I386_PRIVILEGE_CODE, I386_PRIVILEGE_STACK_TOP);
    if (resumed != 1 || i386_privilege_result != I386_PRIVILEGE_RESULT)
        error = EFAULT;

out:
    i386_privilege_active = 0;
    i386_tss_reset_kernel_stack();
    if (i386_privilege_vmspace != (struct vmspace *)0)
        i386_vmspace_deactivate(i386_privilege_vmspace);
    md_curuser = (struct user *)0;
    if (i386_privilege_uarea != (struct user *)0)
        md_uarea_free(i386_privilege_uarea);
    i386_privilege_uarea = (struct user *)0;
    if (i386_privilege_vmspace != (struct vmspace *)0 &&
        vmspace_destroy(i386_privilege_vmspace) != 0 && error == 0)
        error = EFAULT;
    i386_privilege_vmspace = (struct vmspace *)0;
    if (vm_page_boot_allocator.vpa_free_count != free_before && error == 0)
        error = EFAULT;
    return error;
}
