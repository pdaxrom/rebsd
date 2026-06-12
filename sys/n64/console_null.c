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
