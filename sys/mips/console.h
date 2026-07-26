#ifndef _MIPS_CONSOLE_H_
#define _MIPS_CONSOLE_H_

struct winsize;
struct tty;

int mips_console_poll(void);
int mips_console_getc(void);
void mips_console_putc(int ch);
void mips_console_debug_putc(int ch);
void mips_console_winsize(struct winsize *ws);
void mips_console_tty_winsize(struct tty *tp);

#define md_console_poll        mips_console_poll
#define md_console_getc        mips_console_getc
#define md_console_putc        mips_console_putc
#define md_console_tty_winsize mips_console_tty_winsize

#endif
