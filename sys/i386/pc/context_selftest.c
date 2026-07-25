#include <sys/errno.h>
#include <sys/param.h>
#include <sys/user.h>
#include <vm/vmspace.h>

#include "process.h"

#define I386_LABEL_EBX       0
#define I386_LABEL_ESI       1
#define I386_LABEL_EDI       2
#define I386_LABEL_EBP       3
#define I386_LABEL_ESP       4
#define I386_LABEL_EIP       5
#define I386_LABEL_EFLAGS    6

typedef char i386_assert_context_label_size[
    sizeof(label_t) == 7 * sizeof(unsigned) ? 1 : -1];

static struct user *i386_context_source;
static struct user *i386_context_child;
static volatile unsigned i386_context_child_result;

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
        stack_address < stack_start || stack_address >= stack_end)
        i386_context_child_result = EFAULT;
    else {
        md_uarea_guard_check(i386_context_child);
        i386_context_child_result = 0x63747831u;
    }
    longjmp((size_t)i386_context_source,
        &i386_context_source->u_qsave);
    for (;;)
        __asm__ volatile ("cli; hlt");
}

int
i386_context_selftest(void)
{
    vm_pfn_t free_before;
    unsigned stack_pointer;
    int resumed;
    int error;

    i386_context_source = (struct user *)0;
    i386_context_child = (struct user *)0;
    error = 0;
    free_before = vm_page_boot_allocator.vpa_free_count;

    i386_context_source = md_uarea_alloc();
    i386_context_child = md_uarea_alloc();
    if (i386_context_source == (struct user *)0 ||
        i386_context_child == (struct user *)0) {
        error = ENOMEM;
        goto out;
    }
    md_curuser = i386_context_source;
    if (i386_context_register_selftest(&i386_context_source->u_rsave,
        (unsigned)(unsigned long)i386_context_source) != 0 ||
        md_curuser != i386_context_source) {
        error = EFAULT;
        goto out;
    }

    i386_context_child_result = EFAULT;
    resumed = setjmp(&i386_context_source->u_qsave);
    if (resumed == 0) {
        stack_pointer = (unsigned)(unsigned long)i386_context_child +
            USIZE - sizeof(unsigned);
        *(unsigned *)stack_pointer = 0;
        i386_context_child->u_ssave.val[I386_LABEL_EBX] = 0;
        i386_context_child->u_ssave.val[I386_LABEL_ESI] = 0;
        i386_context_child->u_ssave.val[I386_LABEL_EDI] = 0;
        i386_context_child->u_ssave.val[I386_LABEL_EBP] = 0;
        i386_context_child->u_ssave.val[I386_LABEL_ESP] = stack_pointer;
        i386_context_child->u_ssave.val[I386_LABEL_EIP] =
            (unsigned)(unsigned long)i386_context_child_entry;
        i386_context_child->u_ssave.val[I386_LABEL_EFLAGS] =
            i386_context_source->u_qsave.val[I386_LABEL_EFLAGS];
        longjmp((size_t)i386_context_child,
            &i386_context_child->u_ssave);
        error = EFAULT;
        goto out;
    }
    if (resumed != 1 || md_curuser != i386_context_source ||
        i386_context_child_result != 0x63747831u) {
        error = EFAULT;
        goto out;
    }
    md_uarea_guard_check(i386_context_source);
    md_uarea_guard_check(i386_context_child);

out:
    md_curuser = (struct user *)0;
    if (i386_context_child != (struct user *)0)
        md_uarea_free(i386_context_child);
    if (i386_context_source != (struct user *)0)
        md_uarea_free(i386_context_source);
    i386_context_child = (struct user *)0;
    i386_context_source = (struct user *)0;
    if (vm_page_boot_allocator.vpa_free_count != free_before &&
        error == 0)
        error = EFAULT;
    return error;
}
