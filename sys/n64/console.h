#ifndef _N64_CONSOLE_H_
#define _N64_CONSOLE_H_

int n64_console_poll(void);
int n64_console_getc(void);
void n64_console_putc(int ch);
void n64_console_debug_putc(int ch);

#endif
