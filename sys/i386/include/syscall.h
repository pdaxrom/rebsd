#ifndef _I386_SYSCALL_H_
#define _I386_SYSCALL_H_

struct i386_trapframe;
struct sysent;

#define I386_SYSCALL_MAX_ARGS 6u

/*
 * int 0x80 register ABI:
 *
 *   eax       syscall number
 *   ebx..ebp  up to six 32-bit arguments (ebx, ecx, edx, esi, edi, ebp)
 *   eax, edx  return values
 *
 * Carry is clear on success.  On failure, eax contains a positive errno and
 * Carry is set.  The dispatcher must leave the user return frame otherwise
 * suitable for iret.
 */
void i386_syscall_set_table(const struct sysent *, unsigned);
void i386_syscall_get_table(const struct sysent **, unsigned *);
int i386_syscall_install_production(void);
void i386_syscall_dispatch(struct i386_trapframe *);
int i386_syscall_handle_return(struct i386_trapframe *);
int i386_syscall_selftest(void);

#endif
