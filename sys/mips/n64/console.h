#ifndef _N64_CONSOLE_H_
#define _N64_CONSOLE_H_

struct winsize;
struct tty;

int n64_console_poll(void);
int n64_console_getc(void);
void n64_console_putc(int ch);
void n64_console_debug_putc(int ch);
void n64_console_panic_mode(void);
void n64_console_winsize(struct winsize *ws);
void n64_console_tty_winsize(struct tty *tp);

#define md_console_poll        n64_console_poll
#define md_console_getc        n64_console_getc
#define md_console_putc        n64_console_putc
#define md_console_tty_winsize n64_console_tty_winsize

#endif
