#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/drm.h>
#include <machine/n64.h>
#include <machine/n64int.h>
#include <machine/video.h>
#include <vm/pmap.h>

#ifndef VIDEO_ENABLED
void
n64_video_attach(void)
{
}

int
n64_video_set_mode(unsigned mode)
{
    return ENXIO;
}

void
n64_video_get_info(struct n64_video_info *info)
{
    bzero(info, sizeof(*info));
}

volatile void *
n64_video_framebuffer(void)
{
    return 0;
}

void
n64_video_clear(unsigned color)
{
}

void
n64_video_intr_enable(void)
{
}

void
n64_video_intr(void)
{
}
#else

#define N64_VI_REGS            ((volatile unsigned *)0xa4400000u)
#define N64_VI_CTRL            0
#define N64_VI_ORIGIN          1
#define N64_VI_WIDTH           2
#define N64_VI_V_INTR          3
#define N64_VI_V_CURRENT       4
#define N64_VI_BURST           5
#define N64_VI_V_TOTAL         6
#define N64_VI_H_TOTAL         7
#define N64_VI_H_TOTAL_LEAP    8
#define N64_VI_H_VIDEO         9
#define N64_VI_V_VIDEO         10
#define N64_VI_V_BURST         11
#define N64_VI_X_SCALE         12
#define N64_VI_Y_SCALE         13

#define N64_VI_CTRL_RGBA16     0x00000002u
#define N64_VI_CTRL_RGBA32     0x00000003u
#define N64_VI_CTRL_SERRATE    0x00000040u
#define N64_VI_CTRL_RESAMPLE   0x00000200u
#define N64_VI_CTRL_ADVANCE    0x00003000u
#define N64_VI_CTRL_IQUE_ADVANCE 0x00001000u

#define N64_IPL_TV_TYPE        0xa4000009u
#define N64_IPL_IQUE           0xa400000bu

#define N64_VI_H_VIDEO_SET(start, end) \
    ((((start) & 0x3ffu) << 16) | ((end) & 0x3ffu))
#define N64_VI_V_VIDEO_SET(start, end) \
    ((((start) & 0x3ffu) << 16) | ((end) & 0x3ffu))
#define N64_VI_V_BURST_SET(start, end) \
    ((((start) & 0x3ffu) << 16) | ((end) & 0x3ffu))
#define N64_VI_SCALE_SET(from, to) \
    ((1024u * (from) + ((to) / 2u)) / (to))

struct n64_vi_timing {
    unsigned burst;
    unsigned v_total;
    unsigned h_total;
    unsigned h_total_leap;
    unsigned h_video;
    unsigned v_video;
    unsigned v_burst;
};

static const struct n64_vi_timing n64_vi_timings[3] = {
    {
        0x0404233au,
        0x00000271u,
        0x00150c69u,
        0x0c6f0c6eu,
        N64_VI_H_VIDEO_SET(128, 768),
        N64_VI_V_VIDEO_SET(93, 573),
        0x0009026bu,
    },
    {
        0x03e52239u,
        0x0000020du,
        0x00000c15u,
        0x0c150c15u,
        N64_VI_H_VIDEO_SET(108, 748),
        N64_VI_V_VIDEO_SET(35, 515),
        0x000e0204u,
    },
    {
        0x04651e39u,
        0x0000020du,
        0x00040c11u,
        0x0c190c1au,
        N64_VI_H_VIDEO_SET(108, 748),
        N64_VI_V_VIDEO_SET(35, 515),
        0x000e0204u,
    },
};

static struct n64_video_info n64_video_info;
static struct drm_device n64_drm_device;
static struct drm_mode_config n64_drm_modes[4];
static int n64_drm_registered;
static int n64_video_initialized;
static int n64_video_vm_ready;
static int n64_video_interlaced;
static unsigned n64_video_base_yscale;
static unsigned n64_video_last_field = ~0u;

static int n64_drm_enable(struct drm_device *,
    const struct drm_display_mode *, struct drm_framebuffer *);
static void n64_drm_disable(struct drm_device *);
static int n64_drm_prepare_fb(struct drm_device *,
    const struct drm_display_mode *, struct drm_framebuffer *);
static void n64_drm_release_fb(struct drm_device *,
    struct drm_framebuffer *);

static const struct drm_driver n64_drm_driver = {
    n64_drm_enable,
    n64_drm_disable,
    0,
    n64_drm_prepare_fb,
    n64_drm_release_fb,
};

static unsigned
n64_io_read32(unsigned vaddr)
{
    volatile unsigned *ptr = (volatile unsigned *)(vaddr & ~3u);

    return *ptr;
}

static unsigned
n64_io_read8(unsigned vaddr)
{
    unsigned value = n64_io_read32(vaddr);

    return (value >> ((~vaddr & 3u) * 8u)) & 0xffu;
}

static unsigned
n64_video_tv_type(void)
{
    unsigned tv_type = n64_io_read8(N64_IPL_TV_TYPE);

    if (tv_type > N64FB_TV_MPAL)
        tv_type = N64FB_TV_NTSC;
    return tv_type;
}

static volatile void *
n64_video_fb_ptr(void)
{
    return (volatile void *)
        N64_PHYS_TO_KSEG1(n64_video_info.fb_phys);
}

static void
n64_video_clear_current(unsigned color)
{
    volatile unsigned short *fb16;
    volatile unsigned *fb32;
    unsigned pixels;
    unsigned i;

    if (n64_video_info.bpp == 16) {
        fb16 = (volatile unsigned short *)n64_video_fb_ptr();
        pixels = n64_video_info.fb_bytes / sizeof(*fb16);
        color &= 0xffffu;
        for (i = 0; i < pixels; ++i)
            fb16[i] = color;
    } else {
        fb32 = (volatile unsigned *)n64_video_fb_ptr();
        pixels = n64_video_info.fb_bytes / sizeof(*fb32);
        for (i = 0; i < pixels; ++i)
            fb32[i] = color;
    }
}

static void
n64_video_program_vi(void)
{
    const struct n64_vi_timing *timing;
    volatile unsigned *vi = N64_VI_REGS;
    unsigned ctrl;
    unsigned output_width;
    unsigned output_height;
    unsigned y_target;
    unsigned v_total;

    timing = &n64_vi_timings[n64_video_info.tv_type];
    output_width = 640;
    output_height = 480;
    y_target = output_height / 2;
    n64_video_interlaced =
        n64_video_info.height == N64_VIDEO_640_HEIGHT;

    ctrl = n64_io_read8(N64_IPL_IQUE) ?
        N64_VI_CTRL_IQUE_ADVANCE : N64_VI_CTRL_ADVANCE;
    ctrl |= N64_VI_CTRL_RESAMPLE;
    ctrl |= n64_video_info.bpp == 32 ?
        N64_VI_CTRL_RGBA32 : N64_VI_CTRL_RGBA16;
    if (n64_video_interlaced)
        ctrl |= N64_VI_CTRL_SERRATE;

    v_total = timing->v_total & ~1u;
    if (!n64_video_interlaced)
        v_total |= 1u;

    n64_video_base_yscale =
        N64_VI_SCALE_SET(n64_video_info.height, y_target);

    vi[N64_VI_ORIGIN] = n64_video_info.fb_phys;
    vi[N64_VI_WIDTH] = n64_video_info.width;
    vi[N64_VI_BURST] = timing->burst;
    vi[N64_VI_V_TOTAL] = v_total;
    vi[N64_VI_H_TOTAL] = timing->h_total;
    vi[N64_VI_H_TOTAL_LEAP] = timing->h_total_leap;
    vi[N64_VI_H_VIDEO] = timing->h_video;
    vi[N64_VI_V_VIDEO] = timing->v_video;
    vi[N64_VI_V_BURST] = timing->v_burst;
    vi[N64_VI_X_SCALE] =
        N64_VI_SCALE_SET(n64_video_info.width, output_width);
    vi[N64_VI_Y_SCALE] = n64_video_base_yscale;
    vi[N64_VI_V_INTR] = 2;
    vi[N64_VI_CTRL] = ctrl;
    n64_video_last_field = ~0u;
    n64_video_intr_enable();
}

static void
n64_drm_mode_init(struct drm_mode_config *config, unsigned width,
    unsigned height, unsigned bpp, unsigned clock_khz, unsigned flags)
{
    struct drm_display_mode *mode;
    struct drm_framebuffer *fb;

    bzero(config, sizeof(*config));
    mode = &config->mode;
    fb = &config->framebuffer;
    mode->clock_khz = clock_khz;
    mode->hdisplay = width;
    mode->hsync_start = width + width / 40u;
    mode->hsync_end = mode->hsync_start + width * 3u / 20u;
    mode->htotal = width + width / 4u;
    mode->vdisplay = height;
    mode->vsync_start = height + 5u;
    mode->vsync_end = mode->vsync_start + 2u;
    mode->vtotal = height + height / 10u;
    mode->flags = flags;

    fb->bytes = width * height * (bpp / 8u);
    fb->map_hint = N64_FB_USER_VADDR_START;
    fb->width = width;
    fb->height = height;
    fb->stride = width * (bpp / 8u);
    fb->bpp = bpp;
    fb->format = bpp == 32 ?
        DRM_FORMAT_RGBA8888 : DRM_FORMAT_RGBA5551;
    fb->cache_mode = PMAP_CACHE_UNCACHED;
}

static int
n64_drm_register(void)
{
    unsigned rdram;

    if (n64_drm_registered)
        return 0;
    rdram = n64_rdram_size();

    n64_drm_mode_init(&n64_drm_modes[N64FB_MODE_320X240X16],
        N64_VIDEO_320_WIDTH, N64_VIDEO_320_HEIGHT, 16, 12587u, 0);
    n64_drm_mode_init(&n64_drm_modes[N64FB_MODE_320X240X32],
        N64_VIDEO_320_WIDTH, N64_VIDEO_320_HEIGHT, 32, 12587u, 0);
    n64_drm_device.mode_count = 2;
    if (rdram >= N64_RDRAM_SIZE_8M) {
        n64_drm_mode_init(&n64_drm_modes[N64FB_MODE_640X480X16],
            N64_VIDEO_640_WIDTH, N64_VIDEO_640_HEIGHT, 16,
            25175u, DRM_MODE_FLAG_INTERLACE);
        n64_drm_mode_init(&n64_drm_modes[N64FB_MODE_640X480X32],
            N64_VIDEO_640_WIDTH, N64_VIDEO_640_HEIGHT, 32,
            25175u, DRM_MODE_FLAG_INTERLACE);
        n64_drm_device.mode_count = 4;
    }

    n64_drm_device.name = "n64-vi-drm";
    n64_drm_device.driver = &n64_drm_driver;
    n64_drm_device.driver_private = 0;
    n64_drm_device.connector.name = "N64-AV-1";
    n64_drm_device.modes = n64_drm_modes;
    if (drm_device_register(&n64_drm_device) != 0)
        return ENXIO;
    n64_drm_registered = 1;
    return 0;
}

static int
n64_drm_prepare_fb(struct drm_device *dev,
    const struct drm_display_mode *mode, struct drm_framebuffer *fb)
{
    (void)dev;
    (void)mode;
    if (!n64_video_vm_ready) {
        if (fb->width != N64_VIDEO_320_WIDTH ||
            fb->height != N64_VIDEO_320_HEIGHT || fb->bpp != 16)
            return ENXIO;
        fb->vaddr = (volatile unsigned char *)
            N64_PHYS_TO_KSEG1(N64_BASE_FB_PHYS_START);
        fb->paddr = N64_BASE_FB_PHYS_START;
        fb->reserved_bytes = N64_BASE_FB_RESERVED_BYTES;
        return 0;
    }
    return drm_framebuffer_alloc_contiguous(fb,
        n64_rdram_size() - 1u, VM_PAGE_SIZE);
}

static void
n64_drm_release_fb(struct drm_device *dev, struct drm_framebuffer *fb)
{
    (void)dev;
    drm_framebuffer_free_contiguous(fb);
}

static int
n64_drm_enable(struct drm_device *dev, const struct drm_display_mode *mode,
    struct drm_framebuffer *fb)
{
    unsigned mode_index;
    int clear;

    (void)dev;
    if (mode->hdisplay == N64_VIDEO_320_WIDTH &&
        mode->vdisplay == N64_VIDEO_320_HEIGHT && fb->bpp == 16)
        mode_index = N64FB_MODE_320X240X16;
    else if (mode->hdisplay == N64_VIDEO_320_WIDTH &&
        mode->vdisplay == N64_VIDEO_320_HEIGHT && fb->bpp == 32)
        mode_index = N64FB_MODE_320X240X32;
    else if (mode->hdisplay == N64_VIDEO_640_WIDTH &&
        mode->vdisplay == N64_VIDEO_640_HEIGHT && fb->bpp == 16)
        mode_index = N64FB_MODE_640X480X16;
    else if (mode->hdisplay == N64_VIDEO_640_WIDTH &&
        mode->vdisplay == N64_VIDEO_640_HEIGHT && fb->bpp == 32)
        mode_index = N64FB_MODE_640X480X32;
    else
        return EINVAL;

    clear = !n64_video_initialized ||
        n64_video_info.width != fb->width ||
        n64_video_info.height != fb->height ||
        n64_video_info.fb_phys != fb->paddr;
    n64_video_info.mode = mode_index;
    n64_video_info.width = fb->width;
    n64_video_info.height = fb->height;
    n64_video_info.stride = fb->stride;
    n64_video_info.bpp = fb->bpp;
    n64_video_info.format = fb->format;
    n64_video_info.fb_phys = fb->paddr;
    n64_video_info.fb_bytes = fb->bytes;
    n64_video_info.rdram_bytes = n64_rdram_size();
    n64_video_info.tv_type = n64_video_tv_type();
    n64_video_initialized = 1;
    if (clear)
        n64_video_clear_current(fb->bpp == 32 ?
            0x000000ffu : 0x0001u);
    n64_video_program_vi();
    return 0;
}

static void
n64_drm_disable(struct drm_device *dev)
{
    (void)dev;
    N64_VI_REGS[N64_VI_CTRL] = 0;
}

int
n64_video_set_mode(unsigned mode)
{
    int error;

    error = n64_drm_register();
    if (error != 0)
        return error;
    return drm_mode_set_index(&n64_drm_device, mode);
}

static void
n64_video_init(void)
{
    if (n64_video_initialized)
        return;
    if (n64_drm_register() != 0)
        return;
    (void)drm_mode_set_index(&n64_drm_device,
        N64FB_MODE_320X240X16);
}

void
n64_video_attach(void)
{
    n64_video_vm_ready = 1;
    n64_video_init();
}

void
n64_video_get_info(struct n64_video_info *info)
{
    n64_video_init();
    *info = n64_video_info;
}

volatile void *
n64_video_framebuffer(void)
{
    n64_video_init();
    return n64_video_fb_ptr();
}

void
n64_video_clear(unsigned color)
{
    n64_video_init();
    n64_video_clear_current(color);
}

void
n64_video_intr_enable(void)
{
    if (!n64_video_initialized)
        return;

    N64_VI_REGS[N64_VI_V_INTR] = 2;
    if (n64_video_interlaced)
        n64_mi_enable(N64_MI_INTERRUPT_VI);
    else
        n64_mi_disable(N64_MI_INTERRUPT_VI);
}

void
n64_video_intr(void)
{
    volatile unsigned *vi = N64_VI_REGS;
    unsigned field;
    unsigned origin;

    if (!n64_video_initialized || !n64_video_interlaced)
        return;

    field = vi[N64_VI_V_CURRENT] & 1u;
    if (field == n64_video_last_field)
        return;

    origin = n64_video_info.fb_phys;
    if (field == 0)
        origin += n64_video_info.stride;

    vi[N64_VI_ORIGIN] = origin;
    vi[N64_VI_Y_SCALE] = n64_video_base_yscale;
    if (n64_video_info.tv_type == N64FB_TV_MPAL) {
        vi[N64_VI_V_BURST] = field ?
            N64_VI_V_BURST_SET(14, 516) :
            N64_VI_V_BURST_SET(11, 514);
    }
    n64_video_last_field = field;
}

#endif
