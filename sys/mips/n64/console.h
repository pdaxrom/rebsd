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

#endif
