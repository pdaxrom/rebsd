#ifndef _I386_TSS_H_
#define _I386_TSS_H_

int i386_tss_init(void);
void i386_tss_set_kernel_stack(unsigned);
void i386_tss_reset_kernel_stack(void);
unsigned i386_tss_kernel_stack(void);

#endif
