#include <sys/types.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/swap.h>
#include <sys/sysctl.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(void)
{
    fprintf(stderr, "usage: swapon special\n       swapon -s\n");
}

static void
device_path(dev_t dev, char *path, size_t path_size)
{
    struct direct *entry;
    struct stat status;
    DIR *directory;

    directory = opendir("/dev");
    if (directory != NULL) {
        while ((entry = readdir(directory)) != NULL) {
            if (strlen(entry->d_name) + 6 > path_size)
                continue;
            strcpy(path, "/dev/");
            strcat(path, entry->d_name);
            if (stat(path, &status) == 0 && S_ISBLK(status.st_mode) &&
                status.st_rdev == dev) {
                closedir(directory);
                return;
            }
        }
        closedir(directory);
    }
    sprintf(path, "(%u,%u)", (unsigned)major(dev),
        (unsigned)minor(dev));
}

static int
show_swaps(void)
{
    struct swap_device_info *devices;
    char path[MAXPATHLEN];
    int mib[2];
    size_t length;
    size_t count;
    size_t index;
    unsigned long total;
    unsigned long used;

    mib[0] = CTL_VM;
    mib[1] = VM_SWAPDEVICES;
    length = 0;
    if (sysctl(mib, 2, NULL, &length, NULL, 0) < 0) {
        perror("vm.swap_devices");
        return 1;
    }
    if (length % sizeof(*devices) != 0) {
        fprintf(stderr, "swapon: incompatible vm.swap_devices snapshot\n");
        return 1;
    }
    devices = length != 0 ? malloc(length) : NULL;
    if (length != 0 && devices == NULL) {
        fprintf(stderr, "swapon: out of memory\n");
        return 1;
    }
    if (length != 0 && sysctl(mib, 2, devices, &length, NULL, 0) < 0) {
        perror("vm.swap_devices");
        free(devices);
        return 1;
    }
    count = length / sizeof(*devices);
    printf("%-24s %-10s %10s %10s %10s\n",
        "Device", "Format", "Size-KB", "Used-KB", "Free-KB");
    for (index = 0; index < count; ++index) {
        device_path(devices[index].sdi_dev, path, sizeof(path));
        total = devices[index].sdi_total_blocks * DEV_BSIZE / 1024;
        used = devices[index].sdi_used_blocks * DEV_BSIZE / 1024;
        if (used > total)
            used = total;
        printf("%-24s %-10s %10lu %10lu %10lu\n", path,
            (devices[index].sdi_flags & SWAP_DEVICE_INFO_LINUX) ?
            "linux-v1" : "raw", total, used, total - used);
    }
    free(devices);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "-s") == 0)
        return show_swaps();
    if (argc != 2) {
        usage();
        return 1;
    }
    if (swapon(argv[1]) < 0) {
        perror(argv[1]);
        return 1;
    }
    return 0;
}
