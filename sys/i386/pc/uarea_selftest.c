#include <sys/errno.h>
#include <sys/param.h>
#include <sys/user.h>
#include <vm/vmspace.h>

#include "context.h"
#include "interrupt.h"
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
    struct i386_trapframe user_frame;
    struct user *source;
    struct user *child;
    vm_pfn_t free_before;
    int outside_frame;
    int error;

    source = (struct user *)0;
    child = (struct user *)0;
    error = 0;
    free_before = vm_page_boot_allocator.vpa_free_count;

    md_user_frame_exec((int *)&user_frame, 0x12345678u, 0x7fff0000u,
        3u, 0x7fff0010u, 0x7fff0020u);
    outside_frame = 0;
    if (user_frame.tf_ds != I386_USER_DATA_SELECTOR ||
        user_frame.tf_cs != I386_USER_CODE_SELECTOR ||
        user_frame.tf_ss != I386_USER_DATA_SELECTOR ||
        user_frame.tf_eip != 0x12345678u ||
        user_frame.tf_useresp != 0x7fff0000u ||
        user_frame.tf_ebx != 3u ||
        user_frame.tf_ecx != 0x7fff0010u ||
        user_frame.tf_edx != 0x7fff0020u ||
        user_frame.tf_eflags !=
            (I386_EFLAGS_RESERVED | I386_EFLAGS_INTERRUPT) ||
        md_user_frame_write((int *)&user_frame,
            (int *)&user_frame.tf_eax, 0x13579bdf) != 0 ||
        user_frame.tf_eax != 0x13579bdfu ||
        md_user_frame_write((int *)&user_frame, &outside_frame, 1) == 0 ||
        md_user_frame_write((int *)&user_frame,
            (int *)&user_frame.tf_eflags, -1) != 0 ||
        user_frame.tf_eflags != (I386_EFLAGS_USER_SETTABLE |
            I386_EFLAGS_RESERVED | I386_EFLAGS_INTERRUPT) ||
        md_user_frame_set_pc((int *)&user_frame, 0x2468ace0u) != 0 ||
        user_frame.tf_eip != 0x2468ace0u ||
        md_user_frame_single_step((int *)&user_frame) != 0 ||
        (user_frame.tf_eflags & I386_EFLAGS_TRACE) == 0)
        return EFAULT;

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

    child = md_uarea_fork(source, 1);
    if (child == (struct user *)0 || child == source ||
        child->u_uid != source->u_uid ||
        child->u_comm[0] != 'i' || child->u_comm[3] != '6' ||
        child->u_frame != (int *)0 ||
        !i386_uarea_zero(&child->u_qsave, sizeof(child->u_qsave)) ||
        !i386_uarea_zero(&child->u_rsave, sizeof(child->u_rsave)) ||
        child->u_ssave.val[I386_LABEL_ESP] <
            (unsigned)(unsigned long)child ||
        child->u_ssave.val[I386_LABEL_ESP] >=
            (unsigned)(unsigned long)child + USIZE ||
        child->u_ssave.val[I386_LABEL_EIP] == 0 ||
        child->u_ssave.val[I386_LABEL_EFLAGS] != I386_EFLAGS_RESERVED) {
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
