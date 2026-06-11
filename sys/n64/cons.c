#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <machine/n64cart_uart.h>

struct tty cnttys[1];
extern int uwritec(struct uio *uio);
void cnputc(char c);

int
cnopen(dev_t dev, int flag, int mode)
{
    return 0;
}

int
cnclose(dev_t dev, int flag, int mode)
{
    return 0;
}

int
cnread(dev_t dev, struct uio *uio, int flag)
{
    int c;
    int error;

    if (uio->uio_resid == 0)
        return 0;

    c = n64cart_uart_getc();
    if (c == '\r')
        c = '\n';
    cnputc(c);

    error = ureadc(c, uio);
    return error;
}

int
cnwrite(dev_t dev, struct uio *uio, int flag)
{
    int c;

    while ((c = uwritec(uio)) >= 0)
        cnputc(c);
    return 0;
}

int
cnselect(dev_t dev, int rw)
{
    return 1;
}

int
cnioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    return EIO;
}

void
cnputc(char c)
{
    if (c == '\n')
        n64cart_uart_putc('\r');
    n64cart_uart_putc(c);
}

int
cngetc(void)
{
    return n64cart_uart_getc();
}
