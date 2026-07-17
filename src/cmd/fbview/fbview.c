#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <machine/video.h>

#include "fbview_stb.h"

#define FB_DEV "/dev/fb0"
#define RGB_CHANNELS 3

static void
usage(void)
{
    fprintf(stderr, "usage: fbview image.jpg\n");
    exit(1);
}

static void *
xmalloc(unsigned size, const char *what)
{
    void *p;

    p = malloc(size);
    if (p == 0) {
        fprintf(stderr, "fbview: %s: out of memory\n", what);
        exit(1);
    }
    return p;
}

static unsigned char *
load_file(const char *path, unsigned *sizep)
{
    struct stat st;
    unsigned char *data;
    unsigned size;
    unsigned done;
    int fd;
    int n;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "fbview: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "fbview: %s: %s\n", path, strerror(errno));
        close(fd);
        exit(1);
    }
    if (st.st_size <= 0 || st.st_size > 0x7fffffffL) {
        fprintf(stderr, "fbview: %s: invalid file size\n", path);
        close(fd);
        exit(1);
    }
    size = (unsigned)st.st_size;
    data = xmalloc(size, "jpeg buffer");
    done = 0;
    while (done < size) {
        n = read(fd, data + done, size - done);
        if (n < 0) {
            fprintf(stderr, "fbview: %s: %s\n", path, strerror(errno));
            free(data);
            close(fd);
            exit(1);
        }
        if (n == 0) {
            fprintf(stderr, "fbview: %s: short read\n", path);
            free(data);
            close(fd);
            exit(1);
        }
        done += (unsigned)n;
    }
    close(fd);
    *sizep = size;
    return data;
}

static int
open_fb(struct n64fb_info *info, struct n64fb_map *map)
{
    void *mapping;
    int fd;

    fd = open(FB_DEV, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "fbview: %s: %s\n", FB_DEV, strerror(errno));
        exit(1);
    }
    if (ioctl(fd, N64FBIOC_GETINFO, info) < 0) {
        fprintf(stderr, "fbview: get fb info: %s\n", strerror(errno));
        close(fd);
        exit(1);
    }
    if (ioctl(fd, N64FBIOC_GETMAP, map) < 0) {
        fprintf(stderr, "fbview: get fb map: %s\n", strerror(errno));
        close(fd);
        exit(1);
    }
    if (map->bytes < info->height * info->stride) {
        fprintf(stderr, "fbview: framebuffer map is too small\n");
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

static void
clear_fb(volatile unsigned short *fb, unsigned pixels)
{
    unsigned i;

    for (i = 0; i < pixels; ++i)
        fb[i] = 0;
}

static void
draw_rgb(volatile unsigned short *fb, unsigned fb_stride, unsigned x0,
    unsigned y0, unsigned w, unsigned h, const unsigned char *rgb)
{
    unsigned x;
    unsigned y;
    volatile unsigned short *row;
    const unsigned char *src;

    for (y = 0; y < h; ++y) {
        row = fb + (y0 + y) * fb_stride + x0;
        src = rgb + y * w * RGB_CHANNELS;
        for (x = 0; x < w; ++x)
            row[x] = rgb5551(src + x * RGB_CHANNELS);
    }
}

static void
resize_rgb_nearest(const unsigned char *input, unsigned iw, unsigned ih,
    unsigned char *output, unsigned ow, unsigned oh)
{
    unsigned x;
    unsigned y;
    unsigned sx;
    unsigned sy;
    const unsigned char *src;
    unsigned char *dst;

    for (y = 0; y < oh; ++y) {
        sy = ((unsigned long)y * ih) / oh;
        dst = output + y * ow * RGB_CHANNELS;
        for (x = 0; x < ow; ++x) {
            sx = ((unsigned long)x * iw) / ow;
            src = input + (sy * iw + sx) * RGB_CHANNELS;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst += RGB_CHANNELS;
        }
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

int
main(int argc, char **argv)
{
    struct n64fb_info info;
    struct n64fb_map map;
    unsigned char *file_data;
    unsigned char *image;
    unsigned char *scaled;
    volatile unsigned short *fb;
    unsigned file_size;
    unsigned out_w;
    unsigned out_h;
    unsigned x0;
    unsigned y0;
    int iw;
    int ih;
    int comp;
    int fd;

    if (argc != 2)
        usage();

    fd = open_fb(&info, &map);
    file_data = load_file(argv[1], &file_size);
    image = fbview_stbi_load_rgb_from_memory(file_data, file_size, &iw, &ih,
        &comp);
    free(file_data);
    if (image == 0) {
        fprintf(stderr, "fbview: %s: %s\n", argv[1],
            fbview_stbi_failure_reason());
        munmap((void *)map.vaddr, map.bytes);
        close(fd);
        return 1;
    }
    if (iw <= 0 || ih <= 0) {
        fprintf(stderr, "fbview: %s: invalid image size\n", argv[1]);
        fbview_stbi_image_free(image);
        munmap((void *)map.vaddr, map.bytes);
        close(fd);
        return 1;
    }

    fit_image((unsigned)iw, (unsigned)ih, info.width, info.height,
        &out_w, &out_h);
    scaled = image;
    if (out_w != (unsigned)iw || out_h != (unsigned)ih) {
        scaled = xmalloc(out_w * out_h * RGB_CHANNELS, "resized image");
        resize_rgb_nearest(image, (unsigned)iw, (unsigned)ih, scaled, out_w,
            out_h);
    }

    fb = (volatile unsigned short *)map.vaddr;
    clear_fb(fb, map.bytes / sizeof(*fb));
    x0 = (info.width - out_w) / 2;
    y0 = (info.height - out_h) / 2;
    draw_rgb(fb, info.stride / sizeof(*fb), x0, y0, out_w, out_h, scaled);

    if (scaled != image)
        free(scaled);
    fbview_stbi_image_free(image);
    munmap((void *)map.vaddr, map.bytes);
    close(fd);
    printf("%s: %dx%d -> %ux%u at %ux%u\n", argv[1], iw, ih, out_w, out_h,
        info.width, info.height);
    return 0;
}
