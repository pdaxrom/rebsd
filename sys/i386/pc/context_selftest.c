#include <sys/errno.h>
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <vm/vmspace.h>

#include "context.h"
#include "interrupt.h"
#include "process.h"
#include "vmspace_bootstrap.h"

#define I386_CONTEXT_TEST_VADDR 0x52000000u
#define I386_CONTEXT_SOURCE_VALUE 0x736f7572u
#define I386_CONTEXT_CHILD_VALUE 0x6368696cu

typedef char i386_assert_context_label_size[
    sizeof(label_t) == 7 * sizeof(unsigned) ? 1 : -1];

static struct user *i386_context_source;
static struct user *i386_context_child;
static struct vmspace *i386_context_source_vmspace;
static struct vmspace *i386_context_child_vmspace;
static struct proc i386_context_source_proc;
static struct proc i386_context_child_proc;
static volatile unsigned i386_context_child_result;

static void
i386_context_frame_zero(struct i386_trapframe *frame)
{
    unsigned char *bytes;
    unsigned size;

    bytes = (unsigned char *)frame;
    size = sizeof(*frame);
    while (size-- != 0)
        *bytes++ = 0;
}

static void
i386_context_child_entry(void)
{
    volatile unsigned stack_probe;
    unsigned stack_address;
    unsigned stack_start;
    unsigned stack_end;

    stack_address = (unsigned)(unsigned long)&stack_probe;
    stack_start = (unsigned)(unsigned long)i386_context_child;
    stack_end = stack_start + USIZE;
    if (md_curuser != i386_context_child ||
        md_curuser->u_procp != &i386_context_child_proc ||
        vmspace_current() != i386_context_child_vmspace ||
        *(volatile unsigned *)I386_CONTEXT_TEST_VADDR !=
            I386_CONTEXT_CHILD_VALUE ||
        stack_address < stack_start || stack_address >= stack_end)
        i386_context_child_result = EFAULT;
    else {
        md_uarea_guard_check(i386_context_child);
        i386_context_child_result = 0x63747831u;
    }
    if (i386_vmspace_activate(i386_context_source_vmspace) != 0)
        i386_context_child_result = EFAULT;
    longjmp((size_t)i386_context_source,
        &i386_context_source->u_qsave);
    for (;;)
        __asm__ volatile ("cli; hlt");
}

int
i386_context_selftest(void)
{
    struct i386_trapframe *frame;
    struct i386_trapframe *child_frame;
    vm_pfn_t free_before;
    int resumed;
    volatile int error;

    i386_context_source = (struct user *)0;
    i386_context_child = (struct user *)0;
    i386_context_source_vmspace = (struct vmspace *)0;
    i386_context_child_vmspace = (struct vmspace *)0;
    error = 0;
    free_before = vm_page_boot_allocator.vpa_free_count;

    i386_context_source = md_uarea_alloc();
    if (i386_context_source == (struct user *)0) {
        error = ENOMEM;
        goto out;
    }
    error = vmspace_create(&i386_context_source_vmspace);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(i386_context_source_vmspace,
        I386_CONTEXT_TEST_VADDR, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, 0);
    if (error != 0)
        goto out;
    {
        unsigned value;

        value = I386_CONTEXT_SOURCE_VALUE;
        error = vmspace_write(i386_context_source_vmspace,
            I386_CONTEXT_TEST_VADDR, &value, sizeof(value));
    }
    if (error != 0)
        goto out;
    error = vmspace_clone(i386_context_source_vmspace,
        &i386_context_child_vmspace);
    if (error != 0)
        goto out;
    {
        unsigned value;

        value = I386_CONTEXT_CHILD_VALUE;
        error = vmspace_write(i386_context_child_vmspace,
            I386_CONTEXT_TEST_VADDR, &value, sizeof(value));
    }
    if (error != 0)
        goto out;

    i386_context_source_proc.p_uarea = i386_context_source;
    i386_context_source_proc.p_vmspace = i386_context_source_vmspace;
    i386_context_source->u_procp = &i386_context_source_proc;
    md_curuser = i386_context_source;
    error = i386_vmspace_activate(i386_context_source_vmspace);
    if (error != 0)
        goto out;
    if (i386_context_register_selftest(&i386_context_source->u_rsave,
        (unsigned)(unsigned long)i386_context_source) != 0 ||
        md_curuser != i386_context_source) {
        error = EFAULT;
        goto out;
    }

    frame = (struct i386_trapframe *)((unsigned)(unsigned long)
        i386_context_source + USIZE - sizeof(*frame));
    i386_context_frame_zero(frame);
    frame->tf_gs = I386_KERNEL_DATA_SELECTOR;
    frame->tf_fs = I386_KERNEL_DATA_SELECTOR;
    frame->tf_es = I386_KERNEL_DATA_SELECTOR;
    frame->tf_ds = I386_KERNEL_DATA_SELECTOR;
    frame->tf_eax = 0xdeadbeefu;
    frame->tf_eip = (unsigned)(unsigned long)i386_context_child_entry;
    frame->tf_cs = I386_KERNEL_CODE_SELECTOR;
    frame->tf_eflags = I386_EFLAGS_RESERVED;
    i386_context_source->u_frame = (int *)frame;
    i386_context_child = md_uarea_fork(i386_context_source, 0);
    if (i386_context_child == (struct user *)0) {
        error = EFAULT;
        goto out;
    }
    i386_context_child_proc.p_uarea = i386_context_child;
    i386_context_child_proc.p_vmspace = i386_context_child_vmspace;
    i386_context_child->u_procp = &i386_context_child_proc;
    child_frame = (struct i386_trapframe *)i386_context_child->u_frame;
    if (child_frame == (struct i386_trapframe *)0 ||
        child_frame->tf_eax != 0 || child_frame->tf_eip != frame->tf_eip ||
        i386_context_child->u_ssave.val[I386_LABEL_ESP] !=
            (unsigned)(unsigned long)child_frame) {
        error = EFAULT;
        goto out;
    }

    i386_context_child_result = EFAULT;
    resumed = setjmp(&i386_context_source->u_qsave);
    if (resumed == 0) {
        error = i386_vmspace_activate(i386_context_child_vmspace);
        if (error != 0)
            goto out;
        longjmp((size_t)i386_context_child,
            &i386_context_child->u_ssave);
        error = EFAULT;
        goto out;
    }
    if (resumed != 1 || md_curuser != i386_context_source ||
        md_curuser->u_procp != &i386_context_source_proc ||
        vmspace_current() != i386_context_source_vmspace ||
        *(volatile unsigned *)I386_CONTEXT_TEST_VADDR !=
            I386_CONTEXT_SOURCE_VALUE ||
        i386_context_child_result != 0x63747831u) {
        error = EFAULT;
        goto out;
    }
    md_uarea_guard_check(i386_context_source);
    md_uarea_guard_check(i386_context_child);

out:
    if (i386_context_child_vmspace != (struct vmspace *)0)
        i386_vmspace_deactivate(i386_context_child_vmspace);
    if (i386_context_source_vmspace != (struct vmspace *)0)
        i386_vmspace_deactivate(i386_context_source_vmspace);
    md_curuser = (struct user *)0;
    if (i386_context_child != (struct user *)0)
        md_uarea_free(i386_context_child);
    if (i386_context_source != (struct user *)0)
        md_uarea_free(i386_context_source);
    i386_context_child = (struct user *)0;
    i386_context_source = (struct user *)0;
    if (i386_context_child_vmspace != (struct vmspace *)0 &&
        vmspace_destroy(i386_context_child_vmspace) != 0 && error == 0)
        error = EFAULT;
    if (i386_context_source_vmspace != (struct vmspace *)0 &&
        vmspace_destroy(i386_context_source_vmspace) != 0 && error == 0)
        error = EFAULT;
    i386_context_child_vmspace = (struct vmspace *)0;
    i386_context_source_vmspace = (struct vmspace *)0;
    if (vm_page_boot_allocator.vpa_free_count != free_before &&
        error == 0)
        error = EFAULT;
    return error;
}
