#ifndef _N64_N64CART_FLASH_H_
#define _N64_N64CART_FLASH_H_

#include <sys/ioctl.h>

#define N64CART_FLASH_SECTOR       4096u
#define N64CART_FLASH_MAX_TRANSFER N64CART_FLASH_SECTOR

#define N64CART_SSI_SR             0x10u
#define N64CART_SSI_DR0            0x14u
#define N64CART_FW_SIZE            0x18u
#define N64CART_SYS_CTRL           0x0cu

#define N64CART_FLASH_MODE_QUAD    0x10u
#define N64CART_FLASH_CS_HIGH      0x01u

#define N64CART_SSI_SR_TFNF        0x01u
#define N64CART_SSI_SR_RFNE        0x02u

struct n64cart_flash_info {
    unsigned jedec_id;
    unsigned rom_size;
    unsigned fw_size;
    unsigned romfs_offset;
    unsigned sector_size;
};

struct n64cart_flash_io {
    unsigned offset;
    unsigned size;
    char *buffer;
};

#define N64CARTFLASHIOC_GETINFO _IOR('F', 1, struct n64cart_flash_info)
#define N64CARTFLASHIOC_READ    _IOW('F', 2, struct n64cart_flash_io)
#define N64CARTFLASHIOC_WRITE   _IOW('F', 3, struct n64cart_flash_io)
#define N64CARTFLASHIOC_ERASE   _IOW('F', 4, unsigned)

#ifdef KERNEL
#ifndef N64CART_ENABLED
#error "n64cart flash requires device \"n64cart\" in kernel Config"
#endif

int n64cart_flash_open(dev_t dev, int flag, int mode);
int n64cart_flash_close(dev_t dev, int flag, int mode);
int n64cart_flash_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag);
#endif

#endif
