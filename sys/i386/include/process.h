#ifndef _I386_PROCESS_H_
#define _I386_PROCESS_H_

struct i386_trapframe;

int i386_uarea_selftest(void);
int i386_context_selftest(void);
int i386_context_register_selftest(void *, unsigned);
int i386_process_bootstrap(void);
int i386_process_bootstrap_validate(void);
int i386_process_bootstrap_user_probe(void);
int i386_process_handle_return(struct i386_trapframe *);

#endif
