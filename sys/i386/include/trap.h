#ifndef _I386_TRAP_H_
#define _I386_TRAP_H_

struct i386_trapframe;

int i386_trap_signal(unsigned);
int i386_user_trap(struct i386_trapframe *, unsigned);
int i386_grow_user_stack(unsigned, int);

#endif
