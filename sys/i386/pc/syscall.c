#include <sys/errno.h>
#include <sys/param.h>
#include <sys/user.h>
#include <vm/vmspace.h>

#include "interrupt.h"
#include "privilege.h"
#include "syscall.h"
#include "tss.h"
#include "vmspace_bootstrap.h"

#define I386_SYSCALL_TEST_NUMBER  0x7f000001u
#define I386_SYSCALL_TEST_CODE    0x53200000u
#define I386_SYSCALL_TEST_STACK   0x53300000u
#define I386_SYSCALL_TEST_TOP     (I386_SYSCALL_TEST_STACK + VM_PAGE_SIZE)
#define I386_SYSCALL_ENOSYS_EIP   (I386_SYSCALL_TEST_CODE + 9u)
#define I386_SYSCALL_TEST_EIP     (I386_SYSCALL_TEST_CODE + 48u)
#define I386_SYSCALL_TEST_RESULT  21u
#define I386_SYSCALL_TEST_RESULT2 0x53594332u
#define I386_SYSCALL_TEST_MAGIC   0x696e7438u

/*
 * eax=number; ebx/ecx/edx/esi/edi/ebp=arguments; eax/edx=results.
 * Carry is clear on success and set when eax contains a positive errno.
 */
static const unsigned char i386_syscall_test_code[] = {
    0xb8, 0xff, 0xff, 0xff, 0xff,
    0xcd, 0x80,
    0xcd, 0x30,
    0xb8, 0x01, 0x00, 0x00, 0x7f,
    0xbb, 0x01, 0x00, 0x00, 0x00,
    0xb9, 0x02, 0x00, 0x00, 0x00,
    0xba, 0x03, 0x00, 0x00, 0x00,
    0xbe, 0x04, 0x00, 0x00, 0x00,
    0xbf, 0x05, 0x00, 0x00, 0x00,
    0xbd, 0x06, 0x00, 0x00, 0x00,
    0xcd, 0x80,
    0xcd, 0x30,
    0x0f, 0x0b
};

typedef char i386_assert_syscall_code_eip[
    sizeof(i386_syscall_test_code) == 50 ? 1 : -1];

static struct user *i386_syscall_uarea;
static struct vmspace *i386_syscall_vmspace;
static volatile unsigned i386_syscall_active;
static volatile unsigned i386_syscall_result;
static volatile unsigned i386_syscall_phase;
static unsigned i386_syscall_count;

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

void
i386_syscall_dispatch(struct i386_trapframe *frame)
{
    if ((frame->tf_cs & 3u) != 3u || !i386_syscall_active ||
        frame->tf_eax != I386_SYSCALL_TEST_NUMBER) {
        frame->tf_eax = ENOSYS;
        frame->tf_eflags |= I386_EFLAGS_CARRY;
        return;
    }

    frame->tf_eax = frame->tf_ebx + frame->tf_ecx + frame->tf_edx +
        frame->tf_esi + frame->tf_edi + frame->tf_ebp;
    frame->tf_edx = I386_SYSCALL_TEST_RESULT2;
    frame->tf_eflags &= ~I386_EFLAGS_CARRY;
    ++i386_syscall_count;
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

    longjmp((size_t)i386_syscall_uarea, &i386_syscall_uarea->u_qsave);
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
        frame->tf_eip != I386_SYSCALL_TEST_EIP ||
        frame->tf_useresp != I386_SYSCALL_TEST_TOP ||
        frame->tf_eax != I386_SYSCALL_TEST_RESULT ||
        frame->tf_edx != I386_SYSCALL_TEST_RESULT2 ||
        frame->tf_ebx != 1u || frame->tf_ecx != 2u ||
        frame->tf_esi != 4u || frame->tf_edi != 5u ||
        frame->tf_ebp != 6u ||
        (frame->tf_eflags & I386_EFLAGS_CARRY) != 0 ||
        i386_tss_kernel_stack() != expected_stack ||
        i386_syscall_count != 1u)
        i386_syscall_result = EFAULT;
    else
        i386_syscall_result = 0;

    i386_privilege_return_to_kernel(frame,
        (unsigned)(unsigned long)i386_syscall_kernel_return);
    return 1;
}

int
i386_syscall_selftest(void)
{
    struct i386_trapframe frame;
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
    i386_tss_set_kernel_stack((unsigned)(unsigned long)
        i386_syscall_uarea + USIZE);
    i386_syscall_active = 1;
    resumed = setjmp(&i386_syscall_uarea->u_qsave);
    if (resumed == 0)
        i386_user_enter(I386_SYSCALL_TEST_CODE, I386_SYSCALL_TEST_TOP);
    if (resumed != 1 || i386_syscall_result != I386_SYSCALL_TEST_MAGIC)
        error = EFAULT;

out:
    i386_syscall_active = 0;
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
