#include <sys/types.h>
#include <machine/console.h>
#include <machine/n64cart_uart.h>

static volatile u_int *
n64cart_reg(u_int offset)
{
    return (volatile u_int *)(N64CART_UART_BASE + offset);
}

static u_int
n64cart_read(u_int offset)
{
    u_int value;

    asm volatile ("" ::: "memory");
    value = *n64cart_reg(offset);
    asm volatile ("" ::: "memory");
    return value;
}

static void
n64cart_write(u_int offset, u_int value)
{
    asm volatile ("" ::: "memory");
    *n64cart_reg(offset) = value;
    asm volatile ("" ::: "memory");
}

int
n64cart_uart_poll(void)
{
    return (n64cart_read(N64CART_UART_CTRL) & N64CART_UART_RX_AVAIL) != 0;
}

int
n64cart_uart_getc(void)
{
    while (!n64cart_uart_poll())
        ;
    return n64cart_read(N64CART_UART_RXTX) & 0xff;
}

void
n64cart_uart_putc(int ch)
{
    while ((n64cart_read(N64CART_UART_CTRL) & N64CART_UART_TX_FREE) == 0)
        ;

    n64cart_write(N64CART_UART_RXTX, ch & 0xff);
    (void)n64cart_read(N64CART_UART_CTRL);
}

int
n64_console_poll(void)
{
    return n64cart_uart_poll();
}

int
n64_console_getc(void)
{
    return n64cart_uart_getc();
}

void
n64_console_putc(int ch)
{
    n64cart_uart_putc(ch);
}
