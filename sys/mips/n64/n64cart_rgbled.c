#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <machine/n64cart_uart.h>
#include <machine/n64cart_rgbled.h>

static unsigned n64cart_rgbled_value;

int
n64cart_rgbled_open(dev_t dev, int flag, int mode)
{
    if (minor(dev) != 0)
        return ENXIO;
    return 0;
}

int
n64cart_rgbled_close(dev_t dev, int flag, int mode)
{
    if (minor(dev) != 0)
        return ENXIO;
    return 0;
}

int
n64cart_rgbled_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
    unsigned value;

    if (minor(dev) != 0)
        return ENXIO;

    switch (cmd) {
    case N64RGBLEDIOC_SET:
        value = *(unsigned *)data & N64CART_LED_RGB_MASK;
        n64cart_rgbled_value = value;
        n64cart_led_write(value);
        return 0;
    case N64RGBLEDIOC_GET:
        *(unsigned *)data = n64cart_rgbled_value;
        return 0;
    default:
        return ENOTTY;
    }
}
