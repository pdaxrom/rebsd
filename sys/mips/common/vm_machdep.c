/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <machine/io.h>
#include <vm/pmap.h>
#include <vm/vmspace.h>

#define MIPS_UAREA_PAGES       (USIZE / VM_PAGE_SIZE)
#define MIPS_UAREA_KSEG0       0x80000000u
#define MIPS_UAREA_PHYS_MASK   0x1fffffffu
#define MIPS_LABEL_RA          9
#define MIPS_LABEL_SP          11
#define MIPS_UAREA_GUARD       0x75617265u

extern void mips_fork_trampoline(void);
extern void mips_init_trampoline(void);

typedef char mips_assert_uarea_page_multiple[
    (USIZE % VM_PAGE_SIZE) == 0 ? 1 : -1];
typedef char mips_assert_user_fits_uarea[
    sizeof(struct user) < USIZE ? 1 : -1];

struct user *md_curuser;

void
md_uarea_guard_init(struct user *up)
{
    if (up == 0)
        panic("null uarea guard");
    up->u_stack[0] = MIPS_UAREA_GUARD;
}

void
md_uarea_guard_check(const struct user *up)
{
    if (up == 0 || up->u_stack[0] != MIPS_UAREA_GUARD)
        panic("kernel stack overflow");
}

struct vmspace *
vmspace_current(void)
{
    if (md_curuser == 0 || u.u_procp == 0)
        return 0;
    return u.u_procp->p_vmspace;
}

struct user *
md_uarea_alloc(void)
{
    struct vm_page_request request;
    struct vm_page *page;
    struct user *up;
    int error;

    vm_page_request_init(&request);
    request.vpr_npages = MIPS_UAREA_PAGES;
    request.vpr_alignment = VM_PAGE_SIZE;
    request.vpr_state = VM_PAGE_WIRED;
    error = vm_page_alloc(&vm_page_boot_allocator, &request, &page);
    if (error != 0)
        return 0;
    up = (struct user *)pmap_page_direct_map(page, PMAP_CACHE_CACHED);
    if (up == 0) {
        vm_pfn_t index;

        for (index = 0; index < MIPS_UAREA_PAGES; ++index)
            (void)vm_page_counter_dec(&vm_page_boot_allocator,
                page + index, VM_PAGE_COUNTER_WIRE);
        (void)vm_page_free(&vm_page_boot_allocator, page,
            MIPS_UAREA_PAGES);
        return 0;
    }
    bzero((caddr_t)up, USIZE);
    md_uarea_guard_init(up);
    return up;
}

struct user *
md_uarea_fork(const struct user *source, int bootstrap)
{
    int *frame;
    struct user *target;
    unsigned source_address;
    unsigned target_address;
    unsigned stack_pointer;

    if (source == 0)
        return 0;
    target = md_uarea_alloc();
    if (target == 0)
        return 0;
    bcopy(source, target, sizeof(*target));
    source_address = (unsigned)source;
    target_address = (unsigned)target;
    bzero((caddr_t)&target->u_qsave, sizeof(target->u_qsave));
    bzero((caddr_t)&target->u_rsave, sizeof(target->u_rsave));
    bzero((caddr_t)&target->u_ssave, sizeof(target->u_ssave));
    md_uarea_guard_init(target);
    (void)setjmp(&target->u_ssave);
    stack_pointer = target_address + USIZE;
    if (bootstrap) {
        target->u_frame = 0;
        target->u_ssave.val[MIPS_LABEL_RA] =
            (unsigned)mips_init_trampoline;
        target->u_ssave.val[MIPS_LABEL_SP] = stack_pointer - 16;
        return target;
    }
    if (source->u_frame == 0 ||
        (unsigned)source->u_frame < source_address ||
        (unsigned)source->u_frame > source_address + USIZE -
        FRAME_WORDS * sizeof(int)) {
        md_uarea_free(target);
        return 0;
    }
    stack_pointer -= FRAME_STACK_BYTES;
    frame = (int *)(stack_pointer + 16);
    bcopy(source->u_frame, frame, FRAME_WORDS * sizeof(int));
    mips_frame_set_gpr(frame, FRAME_R2, 0);
    mips_frame_set_gpr(frame, FRAME_R3, 0);
    mips_frame_set_gpr(frame, FRAME_R8, 0);
    target->u_frame = frame;
    target->u_ssave.val[MIPS_LABEL_RA] = (unsigned)mips_fork_trampoline;
    target->u_ssave.val[MIPS_LABEL_SP] = stack_pointer;
    return target;
}

void
md_uarea_free(struct user *up)
{
    struct vm_page *page;
    vm_paddr_t paddr;
    vm_pfn_t index;

    if (up == 0 || up == md_curuser)
        return;
    md_uarea_guard_check(up);
    if (((unsigned)up & 0xe0000000u) != MIPS_UAREA_KSEG0)
        panic("bad uarea");
    paddr = (unsigned)up & MIPS_UAREA_PHYS_MASK;
    if (!vm_paddr_page_aligned(paddr))
        panic("unaligned uarea");
    page = vm_page_lookup(&vm_page_boot_allocator, paddr);
    if (page == 0)
        panic("missing uarea");
    for (index = 0; index < MIPS_UAREA_PAGES; ++index) {
        if (vm_page_counter_dec(&vm_page_boot_allocator, page + index,
            VM_PAGE_COUNTER_WIRE) != 0)
            panic("wired uarea");
    }
    if (vm_page_free(&vm_page_boot_allocator, page,
        MIPS_UAREA_PAGES) != 0)
        panic("busy uarea");
}
