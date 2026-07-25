/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _DRM_H_
#define _DRM_H_

#include <sys/ioctl.h>

/*
 * ReBSD's small architecture-independent DRM/KMS interface.  DRMFBIOC_* is
 * the native ReBSD extension ABI.  The dumb framebuffer character device
 * also implements the Linux fbdev userspace ABI subset from <linux/fb.h>.
 */

#define DRM_FORMAT_XRGB8888         1
#define DRM_FORMAT_RGBA5551         2
#define DRM_FORMAT_RGBA8888         3

#define DRM_MODE_FLAG_PHSYNC        (1u << 0)
#define DRM_MODE_FLAG_NHSYNC        (1u << 1)
#define DRM_MODE_FLAG_PVSYNC        (1u << 2)
#define DRM_MODE_FLAG_NVSYNC        (1u << 3)
#define DRM_MODE_FLAG_INTERLACE     (1u << 4)

#define DRM_CONNECTOR_UNKNOWN       0
#define DRM_CONNECTOR_CONNECTED     1
#define DRM_CONNECTOR_DISCONNECTED  2

struct drmfb_info {
    unsigned width;
    unsigned height;
    unsigned stride;
    unsigned bpp;
    unsigned format;
    unsigned fb_phys;
    unsigned fb_bytes;
    unsigned reserved_bytes;
    unsigned pixel_clock_khz;
    unsigned phy_status;
    unsigned mode_index;
    unsigned mode_count;
};

struct drmfb_map {
    unsigned vaddr;
    unsigned bytes;
    unsigned reserved_bytes;
};

struct drmfb_mode {
    unsigned index;
    unsigned width;
    unsigned height;
    unsigned stride;
    unsigned bpp;
    unsigned format;
    unsigned pixel_clock_khz;
    unsigned flags;
};

#define DRMFBIOC_GETINFO            _IOR('F', 1, struct drmfb_info)
#define DRMFBIOC_GETMAP             _IOR('F', 2, struct drmfb_map)
#define DRMFBIOC_GETMODE            _IOWR('F', 3, struct drmfb_mode)
#define DRMFBIOC_SETMODE            _IOW('F', 4, struct drmfb_mode)

#ifdef KERNEL

struct uio;
struct vm_page;

struct drm_display_mode {
    unsigned clock_khz;
    unsigned hdisplay;
    unsigned hsync_start;
    unsigned hsync_end;
    unsigned htotal;
    unsigned vdisplay;
    unsigned vsync_start;
    unsigned vsync_end;
    unsigned vtotal;
    unsigned flags;
};

struct drm_framebuffer {
    volatile unsigned char *vaddr;
    unsigned paddr;
    unsigned bytes;
    unsigned reserved_bytes;
    unsigned map_hint;
    unsigned width;
    unsigned height;
    unsigned stride;
    unsigned bpp;
    unsigned format;
    int cache_mode;
    struct vm_page *pages;
    unsigned npages;
};

struct drm_mode_config {
    struct drm_display_mode mode;
    struct drm_framebuffer framebuffer;
};

struct drm_plane {
    struct drm_framebuffer *framebuffer;
    int enabled;
};

struct drm_crtc {
    struct drm_display_mode mode;
    struct drm_plane *primary;
    int enabled;
};

struct drm_encoder {
    struct drm_crtc *crtc;
    int enabled;
};

struct drm_connector {
    const char *name;
    struct drm_encoder *encoder;
    int status;
};

struct drm_device;

struct drm_driver {
    int (*enable)(struct drm_device *, const struct drm_display_mode *,
        struct drm_framebuffer *);
    void (*disable)(struct drm_device *);
    void (*mode_changed)(struct drm_device *);
    int (*prepare_fb)(struct drm_device *, const struct drm_display_mode *,
        struct drm_framebuffer *);
    void (*release_fb)(struct drm_device *, struct drm_framebuffer *);
};

struct drm_device {
    const char *name;
    const struct drm_driver *driver;
    void *driver_private;
    struct drm_framebuffer framebuffer;
    struct drm_plane primary_plane;
    struct drm_crtc crtc;
    struct drm_encoder encoder;
    struct drm_connector connector;
    struct drm_mode_config *modes;
    unsigned mode_count;
    unsigned mode_index;
    unsigned phy_status;
    int minor;
    int registered;
};

int drm_device_register(struct drm_device *);
void drm_device_unregister(struct drm_device *);
struct drm_device *drm_device_lookup(unsigned);
int drm_mode_set(struct drm_device *, const struct drm_display_mode *,
    struct drm_framebuffer *);
int drm_mode_set_index(struct drm_device *, unsigned);
int drm_mode_blank(struct drm_device *, int);
int drm_mode_validate(const struct drm_display_mode *,
    const struct drm_framebuffer *);
int drm_framebuffer_alloc_contiguous(struct drm_framebuffer *, unsigned,
    unsigned);
void drm_framebuffer_free_contiguous(struct drm_framebuffer *);

int drmfb_open(dev_t, int, int);
int drmfb_close(dev_t, int, int);
int drmfb_read(dev_t, struct uio *, int);
int drmfb_write(dev_t, struct uio *, int);
int drmfb_ioctl(dev_t, u_int, caddr_t, int);
int drmfb_mmap(dev_t, off_t, u_int, int, u_int *, int *);

#endif /* KERNEL */
#endif /* _DRM_H_ */
