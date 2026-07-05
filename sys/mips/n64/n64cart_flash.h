#ifndef _N64_N64CART_FLASH_H_
#define _N64_N64CART_FLASH_H_

#include "cartflash.h"

#define N64CART_SSI_SR             0x10u
#define N64CART_SSI_DR0            0x14u
#define N64CART_FW_SIZE            0x18u
#define N64CART_SYS_CTRL           0x0cu

#define N64CART_FLASH_MODE_QUAD    0x10u
#define N64CART_FLASH_CS_HIGH      0x01u

#define N64CART_SSI_SR_TFNF        0x01u
#define N64CART_SSI_SR_RFNE        0x02u

#ifdef KERNEL
#ifndef N64CART_ENABLED
#error "n64cart flash requires device \"n64cart\" in kernel Config"
#endif

int n64cart_flash_open(dev_t dev, int flag, int mode);
int n64cart_flash_close(dev_t dev, int flag, int mode);
int n64cart_flash_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag);
int n64cart_flash_getinfo(struct n64cart_flash_info *info);
int n64cart_flash_read_raw(unsigned offset, void *buffer, unsigned size);
int n64cart_flash_write_sector_raw(unsigned offset, const void *buffer);
int n64cart_flash_erase_sector_raw(unsigned offset);
int n64cart_flash_sync(void);
void n64cart_flash_shutdown(void);
#endif

#endif
