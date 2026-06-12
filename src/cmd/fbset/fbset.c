#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <machine/video.h>

#define FB_DEV "/dev/fb0"

static void
usage(void)
{
    fprintf(stderr, "usage: fbset [320x240|640x480]\n");
    fprintf(stderr, "       fbset [width height]\n");
    exit(1);
}

static const char *
tv_name(unsigned tv_type)
{
    switch (tv_type) {
    case N64FB_TV_PAL:
        return "pal";
    case N64FB_TV_NTSC:
        return "ntsc";
    case N64FB_TV_MPAL:
        return "mpal";
    default:
        return "unknown";
    }
}

static unsigned
mode_from_size(unsigned width, unsigned height)
{
    if (width == 320 && height == 240)
        return N64FB_MODE_320X240;
    if (width == 640 && height == 480)
        return N64FB_MODE_640X480;
    usage();
    return N64FB_MODE_320X240;
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

static unsigned
parse_mode(int argc, char **argv)
{
    if (argc == 2) {
        if (strcmp(argv[1], "320x240") == 0)
            return N64FB_MODE_320X240;
        if (strcmp(argv[1], "640x480") == 0)
            return N64FB_MODE_640X480;
        usage();
    }
    if (argc == 3)
        return mode_from_size(parse_uint(argv[1]), parse_uint(argv[2]));
    usage();
    return N64FB_MODE_320X240;
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

static void
print_info(const struct n64fb_info *info)
{
    printf("%ux%u %u bpp rgba5551 fb=0x%08x bytes=%u tv=%s rdram=0x%08x\n",
        info->width, info->height, info->bpp, info->fb_phys,
        info->fb_bytes, tv_name(info->tv_type), info->rdram_bytes);
}

int
main(int argc, char **argv)
{
    struct n64fb_info info;
    struct n64fb_mode mode;
    int fd;

    if (argc != 1 && argc != 2 && argc != 3)
        usage();

    fd = open_fb();
    if (argc != 1) {
        mode.mode = parse_mode(argc, argv);
        if (ioctl(fd, N64FBIOC_SETMODE, &mode) < 0) {
            fprintf(stderr, "fbset: set mode: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
    }

    if (ioctl(fd, N64FBIOC_GETINFO, &info) < 0) {
        fprintf(stderr, "fbset: get info: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    print_info(&info);
    close(fd);
    return 0;
}
