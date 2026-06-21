#include <machine/console.h>
#include "n64cart_uart.h"

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned int uintptr;

static void
memory_barrier(void)
{
    __asm__ volatile("" ::: "memory");
}

static volatile u32 *
n64cart_reg(u32 offset)
{
    return (volatile u32 *)((uintptr)N64CART_UART_BASE + offset);
}

static u32
n64cart_io_read(u32 offset)
{
    u32 value;

    memory_barrier();
    value = *n64cart_reg(offset);
    memory_barrier();
    return value;
}

static void
n64cart_io_write(u32 offset, u32 value)
{
    memory_barrier();
    *n64cart_reg(offset) = value;
    memory_barrier();
}

void
n64_console_putc(int ch)
{
    while ((n64cart_io_read(N64CART_UART_CTRL) & N64CART_UART_TX_FREE) == 0u)
        ;

    n64cart_io_write(N64CART_UART_RXTX, (u32)(u8)ch);
    (void)n64cart_io_read(N64CART_UART_CTRL);
}
