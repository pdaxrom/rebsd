#ifndef _N64_VIDEO_H_
#define _N64_VIDEO_H_

#include <sys/ioctl.h>

#define N64FB_MODE_320X240      0
#define N64FB_MODE_640X480      1

#define N64FB_FORMAT_RGBA5551   1

#define N64FB_TV_PAL            0
#define N64FB_TV_NTSC           1
#define N64FB_TV_MPAL           2

struct n64fb_mode {
    unsigned mode;
};

struct n64fb_info {
    unsigned mode;
    unsigned width;
    unsigned height;
    unsigned stride;
    unsigned bpp;
    unsigned format;
    unsigned fb_phys;
    unsigned fb_bytes;
    unsigned rdram_bytes;
    unsigned tv_type;
};

#define N64FBIOC_GETINFO        _IOR('F', 1, struct n64fb_info)
#define N64FBIOC_SETMODE        _IOW('F', 2, struct n64fb_mode)

#ifdef KERNEL
struct uio;

int n64fb_open(dev_t dev, int flag, int mode);
int n64fb_close(dev_t dev, int flag, int mode);
int n64fb_read(dev_t dev, struct uio *uio, int flag);
int n64fb_write(dev_t dev, struct uio *uio, int flag);
int n64fb_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag);

int n64_video_set_mode(unsigned mode);
void n64_video_get_info(struct n64fb_info *info);
volatile unsigned short *n64_video_framebuffer(void);
void n64_video_clear(unsigned color);
void n64_video_intr(void);
void n64_video_intr_enable(void);
#endif

#endif
