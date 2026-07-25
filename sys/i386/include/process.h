#ifndef _I386_PROCESS_H_
#define _I386_PROCESS_H_

int i386_uarea_selftest(void);
int i386_context_selftest(void);
int i386_context_register_selftest(void *, unsigned);
int i386_process_bootstrap(void);
int i386_process_bootstrap_validate(void);

#endif
