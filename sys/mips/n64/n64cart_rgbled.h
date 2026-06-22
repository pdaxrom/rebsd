#ifndef _N64_N64CART_RGBLED_H_
#define _N64_N64CART_RGBLED_H_

#include <sys/ioctl.h>

#define N64RGBLEDIOC_SET        _IOW('L', 1, unsigned)
#define N64RGBLEDIOC_GET        _IOR('L', 2, unsigned)

#ifdef KERNEL
int n64cart_rgbled_open(dev_t dev, int flag, int mode);
int n64cart_rgbled_close(dev_t dev, int flag, int mode);
int n64cart_rgbled_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag);
#endif

#endif
