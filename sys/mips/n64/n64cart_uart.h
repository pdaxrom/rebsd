#ifndef _N64_N64CART_UART_H_
#define _N64_N64CART_UART_H_

#ifndef N64CART_ENABLED
#error "n64cart UART requires device \"n64cart\" in kernel Config"
#endif

#define N64CART_REG_PHYS        0x1fd01000u
#define N64CART_REG_BASE        0xbfd01000u
#define N64CART_UART_PHYS       N64CART_REG_PHYS
#define N64CART_UART_BASE       N64CART_REG_BASE
#define N64CART_UART_CTRL       0x00u
#define N64CART_UART_RXTX       0x04u
#define N64CART_LED_CTRL        0x08u

#define N64CART_UART_RX_AVAIL   0x01u
#define N64CART_UART_TX_FREE    0x02u
#define N64CART_LED_RGB         0x00ffffffu
#define N64CART_LED_RGB_MASK    N64CART_LED_RGB

void n64cart_uart_putc(int ch);
int n64cart_uart_getc(void);
int n64cart_uart_poll(void);
void n64cart_led_write(unsigned rgb);

#ifdef KERNEL
#include <sys/tty.h>

struct n64cart_uart_stats {
    u_int nus_tx_chars;
    u_int nus_wait_events;
    u_int nus_recheck_misses;
    u_int nus_waiting;
    u_int nus_control;
};

struct tty;
extern struct tty n64cart_uart_ttys[1];
int n64cart_uart_tx_ready(void);
void n64cart_uart_get_stats(struct n64cart_uart_stats *stats);
void n64cart_uart_emergency_putc(int ch);
int n64cart_uart_open(dev_t dev, int flag, int mode);
int n64cart_uart_close(dev_t dev, int flag, int mode);
int n64cart_uart_read(dev_t dev, struct uio *uio, int flag);
int n64cart_uart_write(dev_t dev, struct uio *uio, int flag);
int n64cart_uart_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
int n64cart_uart_select(dev_t dev, int rw);
void n64cart_uart_intr(void);
char n64cart_uart_raw_read(dev_t dev);
void n64cart_uart_raw_write(dev_t dev, char ch);
#endif

#endif
