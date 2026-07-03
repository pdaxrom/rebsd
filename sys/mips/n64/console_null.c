#include <sys/ioctl.h>
#include <sys/tty.h>
#include <machine/console.h>

int __attribute__((weak))
n64_console_poll(void)
{
    return 0;
}

int __attribute__((weak))
n64_console_getc(void)
{
    for (;;)
        ;
}

void __attribute__((weak))
n64_console_putc(int ch)
{
    (void)ch;
}

void __attribute__((weak))
n64_console_debug_putc(int ch)
{
    (void)ch;
}

void __attribute__((weak))
n64_console_panic_mode(void)
{
}

void __attribute__((weak))
n64_console_winsize(struct winsize *ws)
{
    ws->ws_row = 24;
    ws->ws_col = 80;
    ws->ws_xpixel = 0;
    ws->ws_ypixel = 0;
}

void __attribute__((weak))
n64_console_tty_winsize(struct tty *tp)
{
    if (tp->t_winsize.ws_row == 0)
        tp->t_winsize.ws_row = 24;
    if (tp->t_winsize.ws_col == 0)
        tp->t_winsize.ws_col = 80;
}
