#include <sys/param.h>
#include <machine/n64cart_uart.h>

static void
puts(const char *s)
{
    while (*s)
        n64cart_uart_putc(*s++);
}

int
main(void)
{
    puts("\nRetroBSD N64 preflight\n");
    for (;;)
        ;
}
