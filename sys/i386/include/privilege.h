#ifndef _I386_PRIVILEGE_H_
#define _I386_PRIVILEGE_H_

int i386_privilege_selftest(void);
void i386_user_enter(unsigned, unsigned) __attribute__((noreturn));

#endif
