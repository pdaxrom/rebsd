#include <sys/errno.h>
#include <sys/param.h>
#include <sys/user.h>
#include <vm/vmspace.h>

#include "process.h"

static int
i386_uarea_zero(const void *pointer, unsigned size)
{
    const unsigned char *bytes;

    bytes = (const unsigned char *)pointer;
    while (size-- != 0) {
        if (*bytes++ != 0)
            return 0;
    }
    return 1;
}

int
i386_uarea_selftest(void)
{
    struct user *source;
    struct user *child;
    vm_pfn_t free_before;
    int error;

    source = (struct user *)0;
    child = (struct user *)0;
    error = 0;
    free_before = vm_page_boot_allocator.vpa_free_count;

    source = md_uarea_alloc();
    if (source == (struct user *)0)
        return ENOMEM;
    if (((unsigned)(unsigned long)source & VM_PAGE_MASK) != 0) {
        error = EFAULT;
        goto out;
    }
    md_uarea_guard_check(source);
    source->u_uid = 42;
    source->u_comm[0] = 'i';
    source->u_comm[1] = '3';
    source->u_comm[2] = '8';
    source->u_comm[3] = '6';
    source->u_comm[4] = '\0';

    child = md_uarea_fork(source, 0);
    if (child == (struct user *)0 || child == source ||
        child->u_uid != source->u_uid ||
        child->u_comm[0] != 'i' || child->u_comm[3] != '6' ||
        child->u_frame != (int *)0 ||
        !i386_uarea_zero(&child->u_qsave, sizeof(child->u_qsave)) ||
        !i386_uarea_zero(&child->u_rsave, sizeof(child->u_rsave)) ||
        !i386_uarea_zero(&child->u_ssave, sizeof(child->u_ssave))) {
        error = EFAULT;
        goto out;
    }
    md_uarea_guard_check(child);
    source->u_uid = 7;
    if (child->u_uid != 42) {
        error = EFAULT;
        goto out;
    }
    md_curuser = source;
    if (&u != source || u.u_uid != 7) {
        error = EFAULT;
        goto out;
    }
    md_curuser = (struct user *)0;

out:
    md_curuser = (struct user *)0;
    if (child != (struct user *)0)
        md_uarea_free(child);
    if (source != (struct user *)0)
        md_uarea_free(source);
    if (vm_page_boot_allocator.vpa_free_count != free_before &&
        error == 0)
        error = EFAULT;
    return error;
}
