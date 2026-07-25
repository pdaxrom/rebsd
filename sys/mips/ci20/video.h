#ifndef _CI20_VIDEO_H_
#define _CI20_VIDEO_H_

#include <sys/drm.h>

#ifdef KERNEL
struct tty;
struct winsize;

void ci20_video_attach(void);
void ci20_video_get_info(struct drmfb_info *);
volatile unsigned *ci20_video_framebuffer(void);
void ci20_video_clear(unsigned);
int ci20_video_ready(void);

void ci20_video_console_putc(int);
void ci20_video_console_mode_changed(void);
void ci20_video_console_winsize(struct winsize *);
void ci20_video_console_tty_winsize(struct tty *);
#endif

#endif
