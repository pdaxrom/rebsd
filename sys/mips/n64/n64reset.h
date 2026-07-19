#ifndef _N64_N64RESET_H_
#define _N64_N64RESET_H_

int n64_reset_dump_interrupt(int *, unsigned, unsigned);
void n64_reset_dump_observe(int *, unsigned, unsigned);
void n64_reset_dump_backtrace(int *);
void n64_reset_dump_show(void);
void n64_reset_dump_reboot(void);

#endif
