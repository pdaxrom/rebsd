#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/fb.h>
#include <sys/drm.h>

#define FB_DEV "/dev/fb0"

static void
usage(void)
{
    fprintf(stderr, "usage: fbset [WIDTHxHEIGHT]\n");
    fprintf(stderr, "       fbset [width height]\n");
    fprintf(stderr, "       fbset -l\n");
    fprintf(stderr, "       fbset fill pixel-value\n");
    exit(1);
}

static const char *
format_name(unsigned format)
{
    switch (format) {
    case DRM_FORMAT_XRGB8888:
        return "xrgb8888";
    case DRM_FORMAT_RGBA5551:
        return "rgba5551";
    default:
        return "unknown";
    }
}

static unsigned
parse_uint(const char *text)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno || *text == '\0' || *end != '\0')
        usage();
    return (unsigned)value;
}

static void
parse_mode(int argc, char **argv, unsigned *width, unsigned *height)
{
    char *end;

    if (argc == 2) {
        end = strchr(argv[1], 'x');
        if (end == 0)
            usage();
        errno = 0;
        *width = (unsigned)strtoul(argv[1], &end, 10);
        if (errno || *end != 'x')
            usage();
        *height = parse_uint(end + 1);
        return;
    }
    if (argc != 3)
        usage();
    *width = parse_uint(argv[1]);
    *height = parse_uint(argv[2]);
}

static int
is_fill(int argc, char **argv)
{
    return argc == 3 && strcmp(argv[1], "fill") == 0;
}

static int
open_fb(void)
{
    int fd;

    fd = open(FB_DEV, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "fbset: %s: %s\n", FB_DEV, strerror(errno));
        exit(1);
    }
    return fd;
}

static unsigned
find_mode(int fd, const struct drmfb_info *info, unsigned width,
    unsigned height)
{
    struct drmfb_mode mode;
    unsigned index;

    for (index = 0; index < info->mode_count; ++index) {
        memset(&mode, 0, sizeof(mode));
        mode.index = index;
        if (ioctl(fd, DRMFBIOC_GETMODE, &mode) < 0) {
            fprintf(stderr, "fbset: get mode %u: %s\n", index,
                strerror(errno));
            exit(1);
        }
        if (mode.width == width && mode.height == height)
            return index;
    }
    fprintf(stderr, "fbset: mode %ux%u is not available\n", width, height);
    exit(1);
    return 0;
}

static void
get_linux_info(int fd, struct fb_fix_screeninfo *fix,
    struct fb_var_screeninfo *var)
{
    memset(fix, 0, sizeof(*fix));
    memset(var, 0, sizeof(*var));
    if (ioctl(fd, FBIOGET_FSCREENINFO, fix) < 0) {
        fprintf(stderr, "fbset: Linux FBIOGET_FSCREENINFO: %s\n",
            strerror(errno));
        exit(1);
    }
    if (ioctl(fd, FBIOGET_VSCREENINFO, var) < 0) {
        fprintf(stderr, "fbset: Linux FBIOGET_VSCREENINFO: %s\n",
            strerror(errno));
        exit(1);
    }
}

static void
print_info(const struct drmfb_info *info, const struct drmfb_map *map,
    const struct fb_fix_screeninfo *fix,
    const struct fb_var_screeninfo *var)
{
    printf("%ux%u %u bpp %s stride=%u fb=0x%08x bytes=%u "
        "mode=%u/%u clock=%uKHz",
        info->width, info->height, info->bpp, format_name(info->format),
        info->stride, info->fb_phys, info->fb_bytes, info->mode_index,
        info->mode_count, info->pixel_clock_khz);
    if (info->phy_status != 0)
        printf(" phy=0x%x", info->phy_status);
    if (map != 0)
        printf(" map=0x%08x mapbytes=%u reserved=%u",
            map->vaddr, map->bytes, map->reserved_bytes);
    if (fix != 0 && var != 0)
        printf(" linux-fbdev=%s/%ux%u", fix->id, var->xres, var->yres);
    printf("\n");
}

static void
list_modes(int fd, const struct drmfb_info *info)
{
    struct drmfb_mode mode;
    unsigned index;

    for (index = 0; index < info->mode_count; ++index) {
        memset(&mode, 0, sizeof(mode));
        mode.index = index;
        if (ioctl(fd, DRMFBIOC_GETMODE, &mode) < 0) {
            fprintf(stderr, "fbset: get mode %u: %s\n", index,
                strerror(errno));
            exit(1);
        }
        printf("%c %u: %ux%u %u bpp %s clock=%uKHz stride=%u bytes=%u\n",
            index == info->mode_index ? '*' : ' ', index,
            mode.width, mode.height, mode.bpp, format_name(mode.format),
            mode.pixel_clock_khz, mode.stride, mode.stride * mode.height);
    }
}

static void
fill_fb(const struct drmfb_info *info, const struct drmfb_map *map,
    unsigned color)
{
    unsigned i;

    if (info->format == DRM_FORMAT_XRGB8888 && info->bpp == 32) {
        volatile unsigned *fb = (volatile unsigned *)map->vaddr;

        for (i = 0; i < info->fb_bytes / sizeof(*fb); ++i)
            fb[i] = color & 0x00ffffffu;
        return;
    }
    if (info->format == DRM_FORMAT_RGBA5551 && info->bpp == 16) {
        volatile unsigned short *fb =
            (volatile unsigned short *)map->vaddr;

        for (i = 0; i < info->fb_bytes / sizeof(*fb); ++i)
            fb[i] = color & 0xffffu;
        return;
    }
    fprintf(stderr, "fbset: unsupported framebuffer format %u/%u bpp\n",
        info->format, info->bpp);
    exit(1);
}

int
main(int argc, char **argv)
{
    struct drmfb_info info;
    struct drmfb_map map;
    struct drmfb_mode mode;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    unsigned width;
    unsigned height;
    void *mapping = MAP_FAILED;
    int fd;

    if (argc != 1 && argc != 2 && argc != 3)
        usage();

    fd = open_fb();
    if (argc != 1 && !is_fill(argc, argv) &&
        !(argc == 2 && strcmp(argv[1], "-l") == 0)) {
        if (ioctl(fd, DRMFBIOC_GETINFO, &info) < 0) {
            fprintf(stderr, "fbset: get info: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
        parse_mode(argc, argv, &width, &height);
        memset(&mode, 0, sizeof(mode));
        mode.index = find_mode(fd, &info, width, height);
        if (ioctl(fd, DRMFBIOC_SETMODE, &mode) < 0) {
            fprintf(stderr, "fbset: set mode: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
    }

    if (ioctl(fd, DRMFBIOC_GETINFO, &info) < 0) {
        fprintf(stderr, "fbset: get info: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    if (ioctl(fd, DRMFBIOC_GETMAP, &map) < 0) {
        fprintf(stderr, "fbset: get map: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    get_linux_info(fd, &fix, &var);
    if (fix.smem_start != info.fb_phys ||
        fix.smem_len != info.fb_bytes ||
        fix.line_length != info.stride ||
        var.xres != info.width || var.yres != info.height ||
        var.bits_per_pixel != info.bpp) {
        fprintf(stderr, "fbset: native DRM and Linux fbdev views disagree\n");
        close(fd);
        return 1;
    }
    if (is_fill(argc, argv)) {
        mapping = mmap((void *)map.vaddr, map.bytes,
            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapping == MAP_FAILED) {
            fprintf(stderr, "fbset: mmap framebuffer: %s\n",
                strerror(errno));
            close(fd);
            return 1;
        }
        map.vaddr = (unsigned)mapping;
        fill_fb(&info, &map, parse_uint(argv[2]));
    }
    if (argc == 2 && strcmp(argv[1], "-l") == 0)
        list_modes(fd, &info);
    print_info(&info, &map, &fix, &var);
    if (mapping != MAP_FAILED)
        munmap(mapping, map.bytes);
    close(fd);
    return 0;
}
