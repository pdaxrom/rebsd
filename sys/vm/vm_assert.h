/*
 * Optional kernel-only VM invariants.  VM_DIAGNOSTIC changes no public
 * structure or syscall ABI; it only turns these checks into panic-on-failure
 * assertions in a diagnostic kernel.
 */

#ifndef _VM_VM_ASSERT_H_
#define _VM_VM_ASSERT_H_

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST) && \
    defined(VM_DIAGNOSTIC)
#define VM_ASSERT(expression) do {                                      \
    if (!(expression))                                                  \
        panic("VM assertion failed: " #expression);                    \
} while (0)
#else
#define VM_ASSERT(expression) ((void)0)
#endif

#endif /* _VM_VM_ASSERT_H_ */
