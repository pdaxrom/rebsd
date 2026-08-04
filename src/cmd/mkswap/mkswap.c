#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <vm/swap_linux.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned char header[SWAP_LINUX_PAGE_BYTES];

int
main(int argc, char **argv)
{
    unsigned *version;
    unsigned *last_page;
    unsigned *badpages;
    unsigned pages;
    int blocks;
    int fd;

    if (argc != 2) {
        fprintf(stderr, "usage: mkswap special\n");
        return 1;
    }
    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror(argv[1]);
        return 1;
    }
    if (ioctl(fd, DIOCGETMEDIASIZE, &blocks) < 0) {
        perror("DIOCGETMEDIASIZE");
        close(fd);
        return 1;
    }
    pages = (unsigned)blocks * 1024u / SWAP_LINUX_PAGE_BYTES;
    if (pages < 2) {
        fprintf(stderr, "%s: device is too small for swap\n", argv[1]);
        close(fd);
        return 1;
    }
    memset(header, 0, sizeof(header));
    version = (unsigned *)(header + 1024);
    last_page = (unsigned *)(header + 1028);
    badpages = (unsigned *)(header + 1032);
    *version = 1;
    *last_page = pages - 1;
    *badpages = 0;
    memcpy(header + sizeof(header) - SWAP_LINUX_MAGIC_BYTES,
        SWAP_LINUX_MAGIC, SWAP_LINUX_MAGIC_BYTES);
    if (write(fd, header, sizeof(header)) != sizeof(header)) {
        perror(argv[1]);
        close(fd);
        return 1;
    }
    close(fd);
    printf("%s: Linux swap v1, %u pages\n", argv[1], pages - 1);
    return 0;
}
