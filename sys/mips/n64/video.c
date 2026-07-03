#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <machine/n64.h>
#include <machine/n64int.h>
#include <machine/video.h>

#ifndef VIDEO_ENABLED
int
n64_video_set_mode(unsigned mode)
{
    return ENXIO;
}

void
n64_video_get_info(struct n64fb_info *info)
{
    bzero(info, sizeof(*info));
}

volatile unsigned short *
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

int
n64_video_useraddr_valid(caddr_t addr)
{
    return 0;
}

int
n64fb_open(dev_t dev, int flag, int mode)
{
    return ENXIO;
}

int
n64fb_close(dev_t dev, int flag, int mode)
{
    return 0;
}

int
n64fb_read(dev_t dev, struct uio *uio, int flag)
{
    return ENXIO;
}

int
n64fb_write(dev_t dev, struct uio *uio, int flag)
{
    return ENXIO;
}

int
n64fb_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
    return ENXIO;
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

static struct n64fb_info n64_video_info;
static int n64_video_initialized;
static int n64_video_interlaced;
static unsigned n64_video_base_yscale;
static unsigned n64_video_last_field = ~0u;

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

static volatile unsigned short *
n64_video_fb_ptr(void)
{
    return (volatile unsigned short *)
        N64_PHYS_TO_KSEG1(n64_video_info.fb_phys);
}

static unsigned
n64_video_reserved_bytes(void)
{
    return n64_rdram_size() >= N64_RDRAM_SIZE_8M ?
        N64_EXPANSION_FB_RESERVED_BYTES : N64_BASE_FB_RESERVED_BYTES;
}

static void
n64_video_clear_current(unsigned color)
{
    volatile unsigned short *fb;
    unsigned pixels;
    unsigned i;

    fb = n64_video_fb_ptr();
    pixels = n64_video_info.fb_bytes / sizeof(*fb);
    color &= 0xffffu;
    for (i = 0; i < pixels; ++i)
        fb[i] = color;
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
        n64_video_info.mode == N64FB_MODE_640X480;

    ctrl = n64_io_read8(N64_IPL_IQUE) ?
        N64_VI_CTRL_IQUE_ADVANCE : N64_VI_CTRL_ADVANCE;
    ctrl |= N64_VI_CTRL_RESAMPLE | N64_VI_CTRL_RGBA16;
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

static int
n64_video_fill_info(unsigned mode, struct n64fb_info *info)
{
    unsigned rdram = n64_rdram_size();
    unsigned fb_phys;

    if (rdram >= N64_RDRAM_SIZE_8M)
        fb_phys = N64_EXPANSION_FB_PHYS_START;
    else
        fb_phys = N64_BASE_FB_PHYS_START;

    switch (mode) {
    case N64FB_MODE_320X240:
        info->width = N64_VIDEO_320_WIDTH;
        info->height = N64_VIDEO_320_HEIGHT;
        info->fb_bytes = N64_VIDEO_320_BYTES;
        break;
    case N64FB_MODE_640X480:
        if (rdram < N64_RDRAM_SIZE_8M)
            return ENOMEM;
        info->width = N64_VIDEO_640_WIDTH;
        info->height = N64_VIDEO_640_HEIGHT;
        info->fb_bytes = N64_VIDEO_640_BYTES;
        break;
    default:
        return EINVAL;
    }

    info->mode = mode;
    info->stride = info->width * N64_VIDEO_BPP_BYTES;
    info->bpp = N64_VIDEO_BPP_BYTES * 8;
    info->format = N64FB_FORMAT_RGBA5551;
    info->fb_phys = fb_phys;
    info->rdram_bytes = rdram;
    info->tv_type = n64_video_tv_type();
    return 0;
}

int
n64_video_set_mode(unsigned mode)
{
    struct n64fb_info info;
    int error;

    error = n64_video_fill_info(mode, &info);
    if (error)
        return error;

    n64_video_info = info;
    n64_video_initialized = 1;
    n64_video_clear_current(0x0001);
    n64_video_program_vi();
    return 0;
}

static void
n64_video_init(void)
{
    if (!n64_video_initialized) {
        unsigned mode = n64_rdram_size() >= N64_RDRAM_SIZE_8M ?
            N64FB_MODE_640X480 : N64FB_MODE_320X240;

        (void)n64_video_set_mode(mode);
    }
}

void
n64_video_get_info(struct n64fb_info *info)
{
    n64_video_init();
    *info = n64_video_info;
}

static void
n64_video_get_map(struct n64fb_map *map)
{
    n64_video_init();
    map->vaddr = N64_FB_USER_VADDR_START;
    map->bytes = n64_video_info.fb_bytes;
    map->reserved_bytes = n64_video_reserved_bytes();
}

volatile unsigned short *
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

int
n64_video_useraddr_valid(caddr_t addr)
{
    unsigned a = (unsigned)addr;

    if (a < N64_FB_USER_VADDR_START)
        return 0;
    n64_video_init();
    return a < N64_FB_USER_VADDR_START + n64_video_info.fb_bytes;
}

int
n64fb_open(dev_t dev, int flag, int mode)
{
    if (minor(dev) != 0)
        return ENXIO;
    n64_video_init();
    return 0;
}

int
n64fb_close(dev_t dev, int flag, int mode)
{
    return 0;
}

static int
n64fb_rw(dev_t dev, struct uio *uio, int flag)
{
    volatile unsigned char *fb;
    struct iovec *iov;
    unsigned offset;
    unsigned count;
    unsigned i;

    if (minor(dev) != 0)
        return ENXIO;
    if (uio->uio_offset < 0)
        return EINVAL;

    n64_video_init();
    fb = (volatile unsigned char *)n64_video_fb_ptr();
    while (uio->uio_resid) {
        iov = uio->uio_iov;
        if (iov->iov_len == 0) {
            uio->uio_iov++;
            uio->uio_iovcnt--;
            if (uio->uio_iovcnt < 0)
                panic("n64fb_rw");
            continue;
        }

        offset = (unsigned)uio->uio_offset;
        if (offset >= n64_video_info.fb_bytes)
            return 0;

        count = iov->iov_len;
        if (count > n64_video_info.fb_bytes - offset)
            count = n64_video_info.fb_bytes - offset;

        if (uio->uio_rw == UIO_READ) {
            for (i = 0; i < count; ++i)
                iov->iov_base[i] = fb[offset + i];
        } else {
            for (i = 0; i < count; ++i)
                fb[offset + i] = iov->iov_base[i];
        }

        iov->iov_base += count;
        iov->iov_len -= count;
        uio->uio_offset += count;
        uio->uio_resid -= count;
    }
    return 0;
}

int
n64fb_read(dev_t dev, struct uio *uio, int flag)
{
    return n64fb_rw(dev, uio, flag);
}

int
n64fb_write(dev_t dev, struct uio *uio, int flag)
{
    return n64fb_rw(dev, uio, flag);
}

int
n64fb_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
    struct n64fb_mode *mode;

    if (minor(dev) != 0)
        return ENXIO;

    switch (cmd) {
    case N64FBIOC_GETINFO:
        n64_video_get_info((struct n64fb_info *)data);
        return 0;
    case N64FBIOC_SETMODE:
        mode = (struct n64fb_mode *)data;
        return n64_video_set_mode(mode->mode);
    case N64FBIOC_GETMAP:
        n64_video_get_map((struct n64fb_map *)data);
        return 0;
    default:
        return EINVAL;
    }
}
#endif
