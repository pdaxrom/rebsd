#ifndef _I386_VMSPACE_BOOTSTRAP_H_
#define _I386_VMSPACE_BOOTSTRAP_H_

struct vmspace;

int i386_vmspace_bootstrap_init(void);
int i386_vmspace_bootstrap_selftest(void);
int i386_vmspace_activate(struct vmspace *);
void i386_vmspace_deactivate(struct vmspace *);
int i386_vmspace_fault_active(unsigned, unsigned, int);

#endif
