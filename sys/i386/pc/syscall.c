#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <vm/vmspace.h>

#include "interrupt.h"
#include "privilege.h"
#include "syscall.h"
#include "tss.h"
#include "vmspace_bootstrap.h"

#define I386_SYSCALL_TEST_CODE    0x53200000u
#define I386_SYSCALL_TEST_STACK   0x53300000u
#define I386_SYSCALL_TEST_TOP     (I386_SYSCALL_TEST_STACK + VM_PAGE_SIZE)
#define I386_SYSCALL_ENOSYS_EIP   (I386_SYSCALL_TEST_CODE + 9u)
#define I386_SYSCALL_SUCCESS_EIP  (I386_SYSCALL_TEST_CODE + 48u)
#define I386_SYSCALL_ERROR_EIP    (I386_SYSCALL_TEST_CODE + 57u)
#define I386_SYSCALL_RESTART_EIP  (I386_SYSCALL_TEST_CODE + 66u)
#define I386_SYSCALL_JUST_EIP     (I386_SYSCALL_TEST_CODE + 76u)
#define I386_SYSCALL_LONGJMP_EIP  (I386_SYSCALL_TEST_CODE + 85u)
#define I386_SYSCALL_GETPID_EIP   (I386_SYSCALL_TEST_CODE + 94u)
#define I386_SYSCALL_TEST_RESULT  21u
#define I386_SYSCALL_TEST_RESULT2 0x53594332u
#define I386_SYSCALL_RESTART_RESULT 0x52535432u
#define I386_SYSCALL_TEST_MAGIC   0x696e7438u
#define I386_SYSCALL_TEST_PID     386

/*
 * eax=number; ebx/ecx/edx/esi/edi/ebp=arguments; eax/edx=results.
 * Carry is clear on success and set when eax contains a positive errno.
 */
static const unsigned char i386_syscall_test_code[] = {
    0xb8, 0xff, 0xff, 0xff, 0xff,
    0xcd, 0x80,
    0xcd, 0x30,
    0xb8, 0x01, 0x00, 0x00, 0x00,
    0xbb, 0x01, 0x00, 0x00, 0x00,
    0xb9, 0x02, 0x00, 0x00, 0x00,
    0xba, 0x03, 0x00, 0x00, 0x00,
    0xbe, 0x04, 0x00, 0x00, 0x00,
    0xbf, 0x05, 0x00, 0x00, 0x00,
    0xbd, 0x06, 0x00, 0x00, 0x00,
    0xcd, 0x80,
    0xcd, 0x30,
    0xb8, 0x02, 0x00, 0x00, 0x00,
    0xcd, 0x80,
    0xcd, 0x30,
    0xb8, 0x03, 0x00, 0x00, 0x00,
    0xcd, 0x80,
    0xcd, 0x30,
    0xf9,
    0xb8, 0x04, 0x00, 0x00, 0x00,
    0xcd, 0x80,
    0xcd, 0x30,
    0xb8, 0x05, 0x00, 0x00, 0x00,
    0xcd, 0x80,
    0xcd, 0x30,
    0xb8, 0x14, 0x00, 0x00, 0x00,
    0xcd, 0x80,
    0xcd, 0x30,
    0x0f, 0x0b
};

typedef char i386_assert_syscall_code_eip[
    sizeof(i386_syscall_test_code) == 96 ? 1 : -1];

static struct user *i386_syscall_uarea;
static struct vmspace *i386_syscall_vmspace;
static struct proc i386_syscall_process;
static volatile unsigned i386_syscall_active;
static volatile unsigned i386_syscall_result;
static volatile unsigned i386_syscall_phase;
static unsigned i386_syscall_count;
static unsigned i386_syscall_restart_count;
static unsigned i386_syscall_longjmp_count;
static const struct sysent *i386_syscall_table;
static unsigned i386_syscall_table_count;

static void
i386_syscall_frame_zero(struct i386_trapframe *frame)
{
    unsigned char *bytes;
    unsigned size;

    bytes = (unsigned char *)frame;
    size = sizeof(*frame);
    while (size-- != 0)
        *bytes++ = 0;
}

static void
i386_syscall_test_nosys(void)
{
    u.u_error = ENOSYS;
}

static void
i386_syscall_test_success(void)
{
    u.u_rval = u.u_arg[0] + u.u_arg[1] + u.u_arg[2] + u.u_arg[3] +
        u.u_arg[4] + u.u_arg[5];
    u.u_rval2 = I386_SYSCALL_TEST_RESULT2;
    ++i386_syscall_count;
}

static void
i386_syscall_test_error(void)
{
    u.u_error = EACCES;
}

static void
i386_syscall_test_restart(void)
{
    ++i386_syscall_restart_count;
    if (i386_syscall_restart_count == 1u) {
        u.u_error = ERESTART;
        return;
    }
    u.u_rval = I386_SYSCALL_RESTART_RESULT;
}

static void
i386_syscall_test_justreturn(void)
{
    u.u_error = EJUSTRETURN;
}

static void
i386_syscall_test_longjmp(void)
{
    ++i386_syscall_longjmp_count;
    u.u_error = EINTR;
    longjmp((size_t)md_curuser, &u.u_qsave);
    u.u_error = EFAULT;
}

static const struct sysent i386_syscall_test_table[] = {
    { 0, i386_syscall_test_nosys },
    { 6, i386_syscall_test_success },
    { 0, i386_syscall_test_error },
    { 0, i386_syscall_test_restart },
    { 0, i386_syscall_test_justreturn },
    { 0, i386_syscall_test_longjmp },
    { 7, i386_syscall_test_success }
};

void
i386_syscall_set_table(const struct sysent *table, unsigned count)
{
    if (table == (const struct sysent *)0 || count == 0) {
        i386_syscall_table = (const struct sysent *)0;
        i386_syscall_table_count = 0;
        return;
    }
    i386_syscall_table = table;
    i386_syscall_table_count = count;
}

void
i386_syscall_get_table(const struct sysent **table, unsigned *count)
{
    *table = i386_syscall_table;
    *count = i386_syscall_table_count;
}

int
i386_syscall_install_production(void)
{
    if (nsysent <= 20 || sysent[2].sy_call != fork ||
        sysent[20].sy_call != getpid)
        return EINVAL;
    i386_syscall_set_table(sysent, (unsigned)nsysent);
    return 0;
}

static void
i386_syscall_error(struct i386_trapframe *frame, int error)
{
    frame->tf_eax = (unsigned)error;
    frame->tf_eflags |= I386_EFLAGS_CARRY;
}

void
i386_syscall_dispatch(struct i386_trapframe *frame)
{
    const struct sysent *callp;
    unsigned code;
    unsigned arg_count;

    if ((frame->tf_cs & 3u) != 3u || md_curuser == (struct user *)0 ||
        i386_syscall_table == (const struct sysent *)0) {
        i386_syscall_error(frame, ENOSYS);
        return;
    }

    code = frame->tf_eax;
    if (code >= i386_syscall_table_count) {
        i386_syscall_error(frame, ENOSYS);
        return;
    }
    callp = &i386_syscall_table[code];
    arg_count = sizeof(u.u_arg) / sizeof(u.u_arg[0]);
    if (callp->sy_narg < 0 || (unsigned)callp->sy_narg > arg_count ||
        (unsigned)callp->sy_narg > I386_SYSCALL_MAX_ARGS) {
        i386_syscall_error(frame, EINVAL);
        return;
    }

    for (code = 0; code < arg_count; ++code)
        u.u_arg[code] = 0;
    if (callp->sy_narg > 0)
        u.u_arg[0] = frame->tf_ebx;
    if (callp->sy_narg > 1)
        u.u_arg[1] = frame->tf_ecx;
    if (callp->sy_narg > 2)
        u.u_arg[2] = frame->tf_edx;
    if (callp->sy_narg > 3)
        u.u_arg[3] = frame->tf_esi;
    if (callp->sy_narg > 4)
        u.u_arg[4] = frame->tf_edi;
    if (callp->sy_narg > 5)
        u.u_arg[5] = frame->tf_ebp;

    u.u_frame = (int *)frame;
    u.u_rval = 0;
    u.u_rval2 = 0;
    u.u_error = 0;
    if (setjmp(&u.u_qsave) == 0)
        (*callp->sy_call)();

    switch (u.u_error) {
    case 0:
        frame->tf_eax = (unsigned)u.u_rval;
        frame->tf_edx = (unsigned)u.u_rval2;
        frame->tf_eflags &= ~I386_EFLAGS_CARRY;
        break;
    case ERESTART:
        if (frame->tf_eip < 2u)
            i386_syscall_error(frame, EFAULT);
        else
            frame->tf_eip -= 2u;
        break;
    case EJUSTRETURN:
        break;
    default:
        i386_syscall_error(frame,
            u.u_error > 0 ? u.u_error : EINVAL);
        break;
    }
}

static void
i386_syscall_kernel_return(void)
{
    volatile unsigned stack_probe;
    unsigned stack_address;
    unsigned stack_start;
    unsigned stack_end;

    stack_address = (unsigned)(unsigned long)&stack_probe;
    stack_start = (unsigned)(unsigned long)i386_syscall_uarea;
    stack_end = stack_start + USIZE;
    if (md_curuser == i386_syscall_uarea &&
        vmspace_current() == i386_syscall_vmspace &&
        stack_address >= stack_start && stack_address < stack_end &&
        i386_syscall_count == 1u && i386_syscall_result == 0)
        i386_syscall_result = I386_SYSCALL_TEST_MAGIC;
    else
        i386_syscall_result = EFAULT;

    longjmp((size_t)i386_syscall_uarea, &i386_syscall_uarea->u_rsave);
    for (;;)
        __asm__ volatile ("cli; hlt");
}

int
i386_syscall_handle_return(struct i386_trapframe *frame)
{
    unsigned expected_stack;

    if (!i386_syscall_active)
        return 0;

    expected_stack = (unsigned)(unsigned long)i386_syscall_uarea + USIZE;
    if (i386_syscall_phase == 0) {
        if (frame->tf_vector != I386_USER_RETURN_VECTOR ||
            frame->tf_cs != I386_USER_CODE_SELECTOR ||
            frame->tf_ss != I386_USER_DATA_SELECTOR ||
            frame->tf_eip != I386_SYSCALL_ENOSYS_EIP ||
            frame->tf_useresp != I386_SYSCALL_TEST_TOP ||
            frame->tf_eax != ENOSYS ||
            (frame->tf_eflags & I386_EFLAGS_CARRY) == 0 ||
            i386_tss_kernel_stack() != expected_stack ||
            i386_syscall_count != 0) {
            i386_syscall_result = EFAULT;
            i386_privilege_return_to_kernel(frame,
                (unsigned)(unsigned long)i386_syscall_kernel_return);
        } else {
            i386_syscall_phase = 1;
        }
        return 1;
    }

    if (frame->tf_vector != I386_USER_RETURN_VECTOR ||
        frame->tf_cs != I386_USER_CODE_SELECTOR ||
        frame->tf_ss != I386_USER_DATA_SELECTOR ||
        frame->tf_useresp != I386_SYSCALL_TEST_TOP ||
        i386_tss_kernel_stack() != expected_stack)
        goto failed;

    if (i386_syscall_phase == 1) {
        if (frame->tf_eip != I386_SYSCALL_SUCCESS_EIP ||
            frame->tf_eax != I386_SYSCALL_TEST_RESULT ||
            frame->tf_edx != I386_SYSCALL_TEST_RESULT2 ||
            frame->tf_ebx != 1u || frame->tf_ecx != 2u ||
            frame->tf_esi != 4u || frame->tf_edi != 5u ||
            frame->tf_ebp != 6u ||
            (frame->tf_eflags & I386_EFLAGS_CARRY) != 0 ||
            i386_syscall_count != 1u)
            goto failed;
        i386_syscall_phase = 2;
        return 1;
    }

    if (i386_syscall_phase == 2) {
        if (frame->tf_eip != I386_SYSCALL_ERROR_EIP ||
            frame->tf_eax != EACCES ||
            (frame->tf_eflags & I386_EFLAGS_CARRY) == 0)
            goto failed;
        i386_syscall_phase = 3;
        return 1;
    }

    if (i386_syscall_phase == 3) {
        if (frame->tf_eip != I386_SYSCALL_RESTART_EIP ||
            frame->tf_eax != I386_SYSCALL_RESTART_RESULT ||
            (frame->tf_eflags & I386_EFLAGS_CARRY) != 0 ||
            i386_syscall_restart_count != 2u)
            goto failed;
        i386_syscall_phase = 4;
        return 1;
    }

    if (i386_syscall_phase == 4) {
        if (frame->tf_eip != I386_SYSCALL_JUST_EIP ||
            frame->tf_eax != 4u ||
            (frame->tf_eflags & I386_EFLAGS_CARRY) == 0)
            goto failed;
        i386_syscall_phase = 5;
        return 1;
    }

    if (i386_syscall_phase == 5 &&
        frame->tf_eip == I386_SYSCALL_LONGJMP_EIP &&
        frame->tf_eax == EINTR &&
        (frame->tf_eflags & I386_EFLAGS_CARRY) != 0 &&
        i386_syscall_longjmp_count == 1u &&
        i386_tss_kernel_stack() == expected_stack) {
        i386_syscall_set_table(sysent, (unsigned)nsysent);
        i386_syscall_phase = 6;
        return 1;
    }

    if (i386_syscall_phase == 6 &&
        frame->tf_eip == I386_SYSCALL_GETPID_EIP &&
        frame->tf_eax == I386_SYSCALL_TEST_PID &&
        frame->tf_edx == 0 &&
        (frame->tf_eflags & I386_EFLAGS_CARRY) == 0 &&
        i386_tss_kernel_stack() == expected_stack)
        i386_syscall_result = 0;
    else
        i386_syscall_result = EFAULT;

    i386_privilege_return_to_kernel(frame,
        (unsigned)(unsigned long)i386_syscall_kernel_return);
    return 1;

failed:
    i386_syscall_result = EFAULT;
    i386_privilege_return_to_kernel(frame,
        (unsigned)(unsigned long)i386_syscall_kernel_return);
    return 1;
}

int
i386_syscall_selftest(void)
{
    struct i386_trapframe frame;
    const struct sysent *saved_table;
    unsigned saved_table_count;
    vm_pfn_t free_before;
    int resumed;
    volatile int error;

    i386_syscall_frame_zero(&frame);
    frame.tf_cs = I386_USER_CODE_SELECTOR;
    frame.tf_eax = 0xffffffffu;
    i386_syscall_dispatch(&frame);
    if (frame.tf_eax != ENOSYS ||
        (frame.tf_eflags & I386_EFLAGS_CARRY) == 0)
        return EFAULT;

    i386_syscall_uarea = (struct user *)0;
    i386_syscall_vmspace = (struct vmspace *)0;
    i386_syscall_active = 0;
    i386_syscall_result = EFAULT;
    i386_syscall_phase = 0;
    i386_syscall_count = 0;
    i386_syscall_restart_count = 0;
    i386_syscall_longjmp_count = 0;
    if (i386_syscall_table != sysent ||
        i386_syscall_table_count != (unsigned)nsysent)
        return EFAULT;
    saved_table = i386_syscall_table;
    saved_table_count = i386_syscall_table_count;
    i386_syscall_set_table(i386_syscall_test_table,
        sizeof(i386_syscall_test_table) /
        sizeof(i386_syscall_test_table[0]));
    free_before = vm_page_boot_allocator.vpa_free_count;
    error = 0;

    i386_syscall_uarea = md_uarea_alloc();
    if (i386_syscall_uarea == (struct user *)0) {
        error = ENOMEM;
        goto out;
    }
    error = vmspace_create(&i386_syscall_vmspace);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(i386_syscall_vmspace,
        I386_SYSCALL_TEST_CODE, VM_PAGE_SIZE, VM_PROT_ALL,
        VM_MAP_EXECUTABLE);
    if (error != 0)
        goto out;
    error = vmspace_map_anon(i386_syscall_vmspace,
        I386_SYSCALL_TEST_STACK, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_MAP_STACK);
    if (error != 0)
        goto out;
    error = vmspace_write(i386_syscall_vmspace, I386_SYSCALL_TEST_CODE,
        i386_syscall_test_code, sizeof(i386_syscall_test_code));
    if (error != 0)
        goto out;
    error = i386_vmspace_activate(i386_syscall_vmspace);
    if (error != 0)
        goto out;

    md_curuser = i386_syscall_uarea;
    bzero(&i386_syscall_process, sizeof(i386_syscall_process));
    i386_syscall_process.p_pid = I386_SYSCALL_TEST_PID;
    i386_syscall_process.p_uarea = i386_syscall_uarea;
    i386_syscall_process.p_vmspace = i386_syscall_vmspace;
    i386_syscall_uarea->u_procp = &i386_syscall_process;
    i386_tss_set_kernel_stack((unsigned)(unsigned long)
        i386_syscall_uarea + USIZE);

    i386_syscall_frame_zero(&frame);
    frame.tf_cs = I386_USER_CODE_SELECTOR;
    frame.tf_eax = 0;
    i386_syscall_dispatch(&frame);
    if (frame.tf_eax != ENOSYS ||
        (frame.tf_eflags & I386_EFLAGS_CARRY) == 0) {
        error = EFAULT;
        goto out;
    }
    i386_syscall_frame_zero(&frame);
    frame.tf_cs = I386_USER_CODE_SELECTOR;
    frame.tf_eax = 6;
    i386_syscall_dispatch(&frame);
    if (frame.tf_eax != EINVAL ||
        (frame.tf_eflags & I386_EFLAGS_CARRY) == 0) {
        error = EFAULT;
        goto out;
    }

    i386_syscall_active = 1;
    resumed = setjmp(&i386_syscall_uarea->u_rsave);
    if (resumed == 0)
        i386_user_enter(I386_SYSCALL_TEST_CODE, I386_SYSCALL_TEST_TOP);
    if (resumed != 1 || i386_syscall_result != I386_SYSCALL_TEST_MAGIC)
        error = EFAULT;

out:
    i386_syscall_active = 0;
    i386_syscall_set_table(saved_table, saved_table_count);
    i386_tss_reset_kernel_stack();
    if (i386_syscall_vmspace != (struct vmspace *)0)
        i386_vmspace_deactivate(i386_syscall_vmspace);
    md_curuser = (struct user *)0;
    if (i386_syscall_uarea != (struct user *)0)
        md_uarea_free(i386_syscall_uarea);
    i386_syscall_uarea = (struct user *)0;
    if (i386_syscall_vmspace != (struct vmspace *)0 &&
        vmspace_destroy(i386_syscall_vmspace) != 0 && error == 0)
        error = EFAULT;
    i386_syscall_vmspace = (struct vmspace *)0;
    if (vm_page_boot_allocator.vpa_free_count != free_before && error == 0)
        error = EFAULT;
    return error;
}
