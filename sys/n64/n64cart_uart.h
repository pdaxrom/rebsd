#ifndef _N64_N64CART_UART_H_
#define _N64_N64CART_UART_H_

#define N64CART_UART_PHYS       0x1fd01000u
#define N64CART_UART_BASE       0xbfd01000u
#define N64CART_UART_CTRL       0x00u
#define N64CART_UART_RXTX       0x04u

#define N64CART_UART_RX_AVAIL   0x01u
#define N64CART_UART_TX_FREE    0x02u

void n64cart_uart_putc(int ch);
int n64cart_uart_getc(void);
int n64cart_uart_poll(void);

#endif
