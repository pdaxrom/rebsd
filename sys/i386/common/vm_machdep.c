#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <vm/pmap.h>
#include <vm/vmspace.h>

#include "context.h"
#include "interrupt.h"
#include "memory.h"

#define I386_UAREA_PAGES       (USIZE / VM_PAGE_SIZE)
#define I386_UAREA_GUARD       0x75617265u

typedef char i386_assert_uarea_page_multiple[
    (USIZE % VM_PAGE_SIZE) == 0 ? 1 : -1];
typedef char i386_assert_user_fits_uarea[
    sizeof(struct user) < USIZE ? 1 : -1];
typedef char i386_assert_frame_fits_uarea[
    sizeof(struct user) + sizeof(struct i386_trapframe) < USIZE ? 1 : -1];

extern void i386_fork_trampoline(void);
extern void i386_init_trampoline(void);

struct user *md_curuser;

static void
i386_uarea_halt(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

/* Replaced by the machine-independent implementation in the full kernel. */
void __attribute__((weak))
md_init_process(void)
{
    i386_uarea_halt();
}

void
md_uarea_guard_init(struct user *up)
{
    if (up == (struct user *)0)
        i386_uarea_halt();
    up->u_stack[0] = I386_UAREA_GUARD;
}

void
md_uarea_guard_check(const struct user *up)
{
    if (up == (const struct user *)0 ||
        up->u_stack[0] != I386_UAREA_GUARD)
        i386_uarea_halt();
}

struct user *
md_uarea_alloc(void)
{
    struct vm_page_request request;
    struct vm_page *page;
    struct user *up;
    int error;

    vm_page_request_init(&request);
    request.vpr_npages = I386_UAREA_PAGES;
    request.vpr_alignment = VM_PAGE_SIZE;
    request.vpr_state = VM_PAGE_WIRED;
    error = vm_page_alloc(&vm_page_boot_allocator, &request, &page);
    if (error != 0)
        return (struct user *)0;
    up = (struct user *)pmap_page_direct_map(page, PMAP_CACHE_CACHED);
    if (up == (struct user *)0) {
        vm_pfn_t index;

        for (index = 0; index < I386_UAREA_PAGES; ++index)
            (void)vm_page_counter_dec(&vm_page_boot_allocator,
                page + index, VM_PAGE_COUNTER_WIRE);
        (void)vm_page_free(&vm_page_boot_allocator, page,
            I386_UAREA_PAGES);
        return (struct user *)0;
    }
    bzero((caddr_t)up, USIZE);
    md_uarea_guard_init(up);
    return up;
}

struct user *
md_uarea_fork(const struct user *source, int bootstrap)
{
    const struct i386_trapframe *source_frame;
    struct i386_trapframe *target_frame;
    struct user *target;
    unsigned source_address;
    unsigned source_frame_address;
    unsigned stack_pointer;

    if (source == (const struct user *)0)
        return (struct user *)0;
    target = md_uarea_alloc();
    if (target == (struct user *)0)
        return (struct user *)0;
    bcopy(source, target, sizeof(*target));
    bzero((caddr_t)&target->u_qsave, sizeof(target->u_qsave));
    bzero((caddr_t)&target->u_rsave, sizeof(target->u_rsave));
    bzero((caddr_t)&target->u_ssave, sizeof(target->u_ssave));
    md_uarea_guard_init(target);

    stack_pointer = (unsigned)(unsigned long)target + USIZE;
    target->u_ssave.val[I386_LABEL_EFLAGS] = I386_EFLAGS_RESERVED;
    if (bootstrap) {
        stack_pointer -= sizeof(unsigned);
        *(unsigned *)stack_pointer = 0;
        target->u_frame = (int *)0;
        target->u_ssave.val[I386_LABEL_ESP] = stack_pointer;
        target->u_ssave.val[I386_LABEL_EIP] =
            (unsigned)(unsigned long)i386_init_trampoline;
        return target;
    }

    source_address = (unsigned)(unsigned long)source;
    source_frame_address = (unsigned)(unsigned long)source->u_frame;
    if ((source_frame_address & (sizeof(unsigned) - 1u)) != 0 ||
        source_frame_address < source_address + sizeof(struct user) ||
        source_frame_address > source_address + USIZE -
            sizeof(struct i386_trapframe)) {
        md_uarea_free(target);
        return (struct user *)0;
    }
    source_frame = (const struct i386_trapframe *)source->u_frame;
    stack_pointer -= sizeof(*target_frame);
    target_frame = (struct i386_trapframe *)stack_pointer;
    bcopy(source_frame, target_frame, sizeof(*target_frame));
    target_frame->tf_eax = 0;
    target->u_frame = (int *)target_frame;
    target->u_ssave.val[I386_LABEL_ESP] = stack_pointer;
    target->u_ssave.val[I386_LABEL_EIP] =
        (unsigned)(unsigned long)i386_fork_trampoline;
    return target;
}

void
md_uarea_free(struct user *up)
{
    struct vm_page *page;
    vm_paddr_t paddr;
    vm_pfn_t index;
    unsigned address;

    if (up == (struct user *)0 || up == md_curuser)
        return;
    md_uarea_guard_check(up);
    address = (unsigned)(unsigned long)up;
    if (address < I386_KERNEL_BASE)
        i386_uarea_halt();
    paddr = address - I386_KERNEL_BASE;
    if (paddr >= I386_PHYS_LIMIT || !vm_paddr_page_aligned(paddr))
        i386_uarea_halt();
    page = vm_page_lookup(&vm_page_boot_allocator, paddr);
    if (page == (struct vm_page *)0)
        i386_uarea_halt();
    for (index = 0; index < I386_UAREA_PAGES; ++index) {
        if (vm_page_counter_dec(&vm_page_boot_allocator, page + index,
            VM_PAGE_COUNTER_WIRE) != 0)
            i386_uarea_halt();
    }
    if (vm_page_free(&vm_page_boot_allocator, page,
        I386_UAREA_PAGES) != 0)
        i386_uarea_halt();
}
