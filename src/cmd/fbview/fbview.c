#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/drm.h>

#include "tjpgd.h"

#define FB_DEV                  "/dev/fb0"
#define RGB_CHANNELS            3
#define FBVIEW_JPEG_WORK_BYTES  8192

struct fbview_session {
    struct drmfb_info info;
    struct drmfb_map map;
    int image_fd;
    int input_error;
    int output_error;
    unsigned decoded_width;
    unsigned decoded_height;
    unsigned output_width;
    unsigned output_height;
    unsigned output_x;
    unsigned output_y;
};

static unsigned long jpeg_work[
    FBVIEW_JPEG_WORK_BYTES / sizeof(unsigned long)];

static void
usage(void)
{
    fprintf(stderr, "usage: fbview image.jpg\n");
    exit(1);
}

static int
open_image(const char *path)
{
    int fd;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "fbview: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    return fd;
}

static int
open_fb(struct drmfb_info *info, struct drmfb_map *map)
{
    void *mapping;
    int fd;

    fd = open(FB_DEV, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "fbview: %s: %s\n", FB_DEV, strerror(errno));
        exit(1);
    }
    if (ioctl(fd, DRMFBIOC_GETINFO, info) < 0) {
        fprintf(stderr, "fbview: get fb info: %s\n", strerror(errno));
        close(fd);
        exit(1);
    }
    if (ioctl(fd, DRMFBIOC_GETMAP, map) < 0) {
        fprintf(stderr, "fbview: get fb map: %s\n", strerror(errno));
        close(fd);
        exit(1);
    }
    if (info->width == 0 || info->height == 0 || info->stride == 0 ||
        info->fb_bytes > map->bytes ||
        info->height > info->fb_bytes / info->stride) {
        fprintf(stderr, "fbview: invalid framebuffer geometry\n");
        close(fd);
        exit(1);
    }
    mapping = mmap((void *)map->vaddr, map->bytes,
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        fprintf(stderr, "fbview: mmap framebuffer: %s\n",
            strerror(errno));
        close(fd);
        exit(1);
    }
    map->vaddr = (unsigned)mapping;
    return fd;
}

static unsigned short
rgb5551(const unsigned char *p)
{
    unsigned r = p[0] >> 3;
    unsigned g = p[1] >> 3;
    unsigned b = p[2] >> 3;

    return (unsigned short)((r << 11) | (g << 6) | (b << 1) | 1);
}

static unsigned
xrgb8888(const unsigned char *p)
{
    return ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | p[2];
}

static unsigned
rgba8888(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
        ((unsigned)p[2] << 8) | 0xffu;
}

static int
supported_fb(const struct drmfb_info *info)
{
    return (info->format == DRM_FORMAT_RGBA5551 && info->bpp == 16) ||
        (info->format == DRM_FORMAT_XRGB8888 && info->bpp == 32) ||
        (info->format == DRM_FORMAT_RGBA8888 && info->bpp == 32);
}

static void
clear_fb(const struct drmfb_info *info, const struct drmfb_map *map)
{
    unsigned i;

    if (info->format == DRM_FORMAT_RGBA5551) {
        volatile unsigned short *fb =
            (volatile unsigned short *)map->vaddr;

        for (i = 0; i < info->fb_bytes / sizeof(*fb); ++i)
            fb[i] = 0;
    } else {
        volatile unsigned *fb = (volatile unsigned *)map->vaddr;

        for (i = 0; i < info->fb_bytes / sizeof(*fb); ++i)
            fb[i] = 0;
    }
}

static void
fit_image(unsigned iw, unsigned ih, unsigned fw, unsigned fh,
    unsigned *owp, unsigned *ohp)
{
    unsigned long lhs;
    unsigned long rhs;
    unsigned ow;
    unsigned oh;

    lhs = (unsigned long)iw * fh;
    rhs = (unsigned long)fw * ih;
    if (lhs > rhs) {
        ow = fw;
        oh = (unsigned)(((unsigned long)ih * fw) / iw);
    } else {
        oh = fh;
        ow = (unsigned)(((unsigned long)iw * fh) / ih);
    }
    if (ow == 0)
        ow = 1;
    if (oh == 0)
        oh = 1;
    *owp = ow;
    *ohp = oh;
}

static unsigned
ceil_mul_div(unsigned value, unsigned multiplier, unsigned divisor)
{
    unsigned long product;

    product = (unsigned long)value * multiplier;
    return (unsigned)((product + divisor - 1) / divisor);
}

static size_t
jpeg_input(JDEC *decoder, uint8_t *buffer, size_t bytes)
{
    struct fbview_session *session;
    size_t done;
    int count;

    session = (struct fbview_session *)decoder->device;
    if (buffer == 0) {
        if (lseek(session->image_fd, (off_t)bytes, SEEK_CUR) ==
            (off_t)-1) {
            session->input_error = errno;
            return 0;
        }
        return bytes;
    }

    done = 0;
    while (done < bytes) {
        count = read(session->image_fd, buffer + done, bytes - done);
        if (count < 0) {
            session->input_error = errno;
            return 0;
        }
        if (count == 0)
            break;
        done += (size_t)count;
    }
    return done;
}

static int
jpeg_output(JDEC *decoder, void *bitmap, JRECT *rect)
{
    struct fbview_session *session;
    const unsigned char *pixels;
    const unsigned char *src;
    unsigned block_width;
    unsigned dx_begin;
    unsigned dx_end;
    unsigned dy_begin;
    unsigned dy_end;
    unsigned dx;
    unsigned dy;
    unsigned sx;
    unsigned sy;

    session = (struct fbview_session *)decoder->device;
    if (rect->right < rect->left || rect->bottom < rect->top ||
        rect->right >= session->decoded_width ||
        rect->bottom >= session->decoded_height) {
        session->output_error = EINVAL;
        return 0;
    }

    block_width = rect->right - rect->left + 1;
    dx_begin = ceil_mul_div(rect->left, session->output_width,
        session->decoded_width);
    dx_end = ceil_mul_div((unsigned)rect->right + 1,
        session->output_width, session->decoded_width);
    dy_begin = ceil_mul_div(rect->top, session->output_height,
        session->decoded_height);
    dy_end = ceil_mul_div((unsigned)rect->bottom + 1,
        session->output_height, session->decoded_height);
    if (dx_end > session->output_width)
        dx_end = session->output_width;
    if (dy_end > session->output_height)
        dy_end = session->output_height;

    pixels = (const unsigned char *)bitmap;
    for (dy = dy_begin; dy < dy_end; ++dy) {
        sy = (unsigned)(((unsigned long)dy * session->decoded_height) /
            session->output_height);
        if (sy < rect->top || sy > rect->bottom)
            continue;
        if (session->info.format == DRM_FORMAT_RGBA5551) {
            volatile unsigned short *row =
                (volatile unsigned short *)((unsigned char *)
                session->map.vaddr +
                (session->output_y + dy) * session->info.stride) +
                session->output_x;

            for (dx = dx_begin; dx < dx_end; ++dx) {
                sx = (unsigned)(((unsigned long)dx *
                    session->decoded_width) / session->output_width);
                if (sx < rect->left || sx > rect->right)
                    continue;
                src = pixels + ((sy - rect->top) * block_width +
                    sx - rect->left) * RGB_CHANNELS;
                row[dx] = rgb5551(src);
            }
        } else {
            volatile unsigned *row =
                (volatile unsigned *)((unsigned char *)
                session->map.vaddr +
                (session->output_y + dy) * session->info.stride) +
                session->output_x;

            for (dx = dx_begin; dx < dx_end; ++dx) {
                sx = (unsigned)(((unsigned long)dx *
                    session->decoded_width) / session->output_width);
                if (sx < rect->left || sx > rect->right)
                    continue;
                src = pixels + ((sy - rect->top) * block_width +
                    sx - rect->left) * RGB_CHANNELS;
                row[dx] = session->info.format == DRM_FORMAT_RGBA8888 ?
                    rgba8888(src) : xrgb8888(src);
            }
        }
    }
    return 1;
}

static const char *
jpeg_error(JRESULT result)
{
    switch (result) {
    case JDR_OK:
        return "ok";
    case JDR_INTR:
        return "output interrupted";
    case JDR_INP:
        return "truncated image or input error";
    case JDR_MEM1:
        return "JPEG work area is too small";
    case JDR_MEM2:
        return "JPEG input buffer is too small";
    case JDR_PAR:
        return "invalid decoder parameter";
    case JDR_FMT1:
        return "invalid JPEG data";
    case JDR_FMT2:
        return "unsupported JPEG feature";
    case JDR_FMT3:
        return "unsupported progressive or lossless JPEG";
    default:
        return "unknown JPEG error";
    }
}

static unsigned
jpeg_scale(const JDEC *decoder, const struct drmfb_info *info)
{
    unsigned scale;

    scale = 0;
    while (scale < 3 &&
        (((unsigned)decoder->width >> scale) > info->width ||
        ((unsigned)decoder->height >> scale) > info->height))
        ++scale;
    return scale;
}

int
main(int argc, char **argv)
{
    struct fbview_session session;
    JDEC decoder;
    JRESULT result;
    unsigned scale;
    int fb_fd;

    if (argc != 2)
        usage();

    memset(&session, 0, sizeof(session));
    session.image_fd = open_image(argv[1]);
    result = jd_prepare(&decoder, jpeg_input, jpeg_work,
        sizeof(jpeg_work), &session);
    if (result != JDR_OK) {
        fprintf(stderr, "fbview: %s: %s", argv[1], jpeg_error(result));
        if (session.input_error != 0)
            fprintf(stderr, ": %s", strerror(session.input_error));
        fprintf(stderr, "\n");
        close(session.image_fd);
        return 1;
    }

    fb_fd = open_fb(&session.info, &session.map);
    if (!supported_fb(&session.info)) {
        fprintf(stderr, "fbview: unsupported framebuffer format %u/%u bpp\n",
            session.info.format, session.info.bpp);
        munmap((void *)session.map.vaddr, session.map.bytes);
        close(fb_fd);
        close(session.image_fd);
        return 1;
    }

    scale = jpeg_scale(&decoder, &session.info);
    session.decoded_width = (unsigned)decoder.width >> scale;
    session.decoded_height = (unsigned)decoder.height >> scale;
    if (session.decoded_width == 0 || session.decoded_height == 0) {
        fprintf(stderr, "fbview: %s: invalid scaled image size\n", argv[1]);
        munmap((void *)session.map.vaddr, session.map.bytes);
        close(fb_fd);
        close(session.image_fd);
        return 1;
    }
    fit_image(session.decoded_width, session.decoded_height,
        session.info.width, session.info.height,
        &session.output_width, &session.output_height);
    session.output_x =
        (session.info.width - session.output_width) / 2;
    session.output_y =
        (session.info.height - session.output_height) / 2;

    clear_fb(&session.info, &session.map);
    result = jd_decomp(&decoder, jpeg_output, (uint8_t)scale);
    if (result != JDR_OK) {
        fprintf(stderr, "fbview: %s: %s", argv[1], jpeg_error(result));
        if (session.input_error != 0)
            fprintf(stderr, ": %s", strerror(session.input_error));
        else if (session.output_error != 0)
            fprintf(stderr, ": %s", strerror(session.output_error));
        fprintf(stderr, "\n");
        munmap((void *)session.map.vaddr, session.map.bytes);
        close(fb_fd);
        close(session.image_fd);
        return 1;
    }

    asm volatile ("sync" ::: "memory");
    munmap((void *)session.map.vaddr, session.map.bytes);
    close(fb_fd);
    close(session.image_fd);
    printf("%s: %ux%u /%u -> %ux%u at %ux%u\n",
        argv[1], decoder.width, decoder.height, 1u << scale,
        session.output_width, session.output_height,
        session.info.width, session.info.height);
    return 0;
}
