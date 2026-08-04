#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(void)
{
    fprintf(stderr,
        "usage: ramctl create special size=value [backing=value] "
        "[compression]\n"
        "       ramctl destroy special\n"
        "       ramctl status special\n"
        "       ramctl list\n"
        "values: bytes, K, M, G; size may also be Nx when backing is set\n");
    exit(1);
}

static int
parse_absolute(const char *text, unsigned *result)
{
    unsigned long value;
    unsigned long multiplier;
    char *end;

    value = strtoul(text, &end, 10);
    if (end == text)
        return -1;
    multiplier = 1;
    if ((*end == 'k' || *end == 'K') && end[1] == '\0')
        multiplier = 1024UL;
    else if ((*end == 'm' || *end == 'M') && end[1] == '\0')
        multiplier = 1024UL * 1024UL;
    else if ((*end == 'g' || *end == 'G') && end[1] == '\0')
        multiplier = 1024UL * 1024UL * 1024UL;
    else if (*end != '\0')
        return -1;
    if (value == 0 || value > 0xffffffffUL / multiplier)
        return -1;
    *result = (unsigned)(value * multiplier);
    return 0;
}

static int
parse_media_size(const char *text, unsigned backing, unsigned *result)
{
    unsigned long value;
    char *end;

    value = strtoul(text, &end, 10);
    if (end != text && (*end == 'x' || *end == 'X') && end[1] == '\0') {
        if (backing == 0 || value == 0 ||
            value > 0xffffffffUL / backing)
            return -1;
        *result = (unsigned)(value * backing);
        return 0;
    }
    return parse_absolute(text, result);
}

static int
open_device(const char *path)
{
    int fd;

    fd = open(path, O_RDWR);
    if (fd < 0)
        perror(path);
    return fd;
}

static int
show_status(const char *path)
{
    struct ramdisk_info info;
    int fd;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return 1;
    }
    if (ioctl(fd, RAMDIOCGETINFO, &info) < 0) {
        perror("RAMDIOCGETINFO");
        close(fd);
        return 1;
    }
    printf("%s: %s", path, info.rdi_configured ? "configured" :
        "not configured");
    if (info.rdi_configured)
        printf(", size=%u, backing=%u, compression=%s",
            info.rdi_media_bytes, info.rdi_backing_bytes,
            (info.rdi_flags & RAMDISK_CONFIG_COMPRESSION) ? "on" :
            "off");
    putchar('\n');
    close(fd);
    return 0;
}

static int
list_devices(void)
{
    struct ramdisk_info info;
    char path[16];
    unsigned unit;
    int fd;
    int result;

    result = 0;
    for (unit = 0; unit < RAMDISK_MAX_DEVICES; ++unit) {
        sprintf(path, "/dev/ram%u", unit);
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            if (errno != ENOENT && errno != ENXIO) {
                perror(path);
                result = 1;
            }
            continue;
        }
        if (ioctl(fd, RAMDIOCGETINFO, &info) < 0) {
            if (errno != ENXIO) {
                perror(path);
                result = 1;
            }
            close(fd);
            continue;
        }
        close(fd);
        if (info.rdi_configured)
            result |= show_status(path);
    }
    return result;
}

static int
destroy_device(const char *path)
{
    int fd;

    fd = open_device(path);
    if (fd < 0)
        return 1;
    if (ioctl(fd, RAMDIOCDESTROY, 0) < 0) {
        perror("RAMDIOCDESTROY");
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}

static int
create_device(int argc, char **argv)
{
    struct ramdisk_configure config;
    const char *size_text;
    const char *backing_text;
    const char *path;
    int compression;
    int fd;
    int i;

    if (argc < 4)
        usage();
    path = argv[2];
    size_text = 0;
    backing_text = 0;
    compression = 0;
    for (i = 3; i < argc; ++i) {
        if (strncmp(argv[i], "size=", 5) == 0)
            size_text = argv[i] + 5;
        else if (strncmp(argv[i], "backing=", 8) == 0)
            backing_text = argv[i] + 8;
        else if (strcmp(argv[i], "compression") == 0)
            compression = 1;
        else
            usage();
    }
    if (size_text == 0)
        usage();
    fd = open_device(path);
    if (fd < 0)
        return 1;
    memset(&config, 0, sizeof(config));
    if (backing_text != 0) {
        if (parse_absolute(backing_text,
            &config.rdc_backing_bytes) < 0) {
            fprintf(stderr, "ramctl: invalid backing size: %s\n",
                backing_text);
            close(fd);
            return 1;
        }
    }
    if (parse_media_size(size_text, config.rdc_backing_bytes,
        &config.rdc_media_bytes) < 0) {
        fprintf(stderr, "ramctl: invalid media size: %s\n", size_text);
        close(fd);
        return 1;
    }
    if (config.rdc_backing_bytes == 0)
        config.rdc_backing_bytes = config.rdc_media_bytes;
    if (compression)
        config.rdc_flags |= RAMDISK_CONFIG_COMPRESSION;
    if (ioctl(fd, RAMDIOCCONFIGURE, &config) < 0) {
        perror("RAMDIOCCONFIGURE");
        close(fd);
        return 1;
    }
    close(fd);
    return show_status(path);
}

int
main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "list") == 0)
        return list_devices();
    if (argc < 3)
        usage();
    if (strcmp(argv[1], "create") == 0)
        return create_device(argc, argv);
    if (argc != 3)
        usage();
    if (strcmp(argv[1], "destroy") == 0)
        return destroy_device(argv[2]);
    if (strcmp(argv[1], "status") == 0)
        return show_status(argv[2]);
    usage();
    return 1;
}
