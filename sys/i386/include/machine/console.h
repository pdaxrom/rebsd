#ifndef _I386_CONSOLE_H_
#define _I386_CONSOLE_H_

struct tty;

int i386_console_poll(void);
int i386_console_getc(void);
void i386_console_putc(int);
void i386_console_tty_winsize(struct tty *);

#define md_console_poll        i386_console_poll
#define md_console_getc        i386_console_getc
#define md_console_putc        i386_console_putc
#define md_console_tty_winsize i386_console_tty_winsize

#endif
