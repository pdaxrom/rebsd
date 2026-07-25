#ifndef _N64_VIDEO_H_
#define _N64_VIDEO_H_

#include <sys/drm.h>

#define N64FB_MODE_320X240      0
#define N64FB_MODE_640X480      1

#define N64FB_TV_PAL            0
#define N64FB_TV_NTSC           1
#define N64FB_TV_MPAL           2

struct n64_video_info {
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

#ifdef KERNEL
void n64_video_attach(void);
int n64_video_set_mode(unsigned mode);
void n64_video_get_info(struct n64_video_info *info);
volatile unsigned short *n64_video_framebuffer(void);
void n64_video_clear(unsigned color);
void n64_video_intr(void);
void n64_video_intr_enable(void);
#endif

#endif
