/*
 * Simple MBR partition editor for ReBSD.
 *
 * The original RetroBSD utility dates from 2012.  This version keeps its
 * deliberately small scope, but uses an ABI-independent MBR layout, validates
 * all input and partition ranges, and opens print-only operations read-only.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/disk.h>
#include <ioctl.h>

#include "fdisk.h"

enum fdisk_action {
    FDISK_ACTION_NONE = 0,
    FDISK_ACTION_PRINT,
    FDISK_ACTION_DELETE,
    FDISK_ACTION_NEW,
    FDISK_ACTION_ACTIVE,
    FDISK_ACTION_TYPE
};

static struct fdisk_mbr disk_mbr;
static unsigned disk_kbytes;
static unsigned disk_sectors;
static int disk_too_large;

static void
usage(void)
{
    fprintf(stderr,
        "usage: fdisk -p device\n"
        "       fdisk -d device partition\n"
        "       fdisk [-w] [-t type] -n device [size-kbytes]\n"
        "       fdisk -a device partition\n"
        "       fdisk -T -t type device partition\n"
        "\n"
        "       -p  print the MBR without opening the device for writing\n"
        "       -d  delete one partition\n"
        "       -n  append one partition, using the remaining space by default\n"
        "       -a  toggle the active flag\n"
        "       -T  change one partition type\n"
        "       -t  partition type (default 0xb7 for -n)\n"
        "       -w  initialize the partition table before -n\n");
}

static int
select_action(enum fdisk_action *action, enum fdisk_action requested)
{
    if (*action != FDISK_ACTION_NONE) {
        fprintf(stderr, "fdisk: specify exactly one operation\n");
        return -1;
    }
    *action = requested;
    return 0;
}

static int
parse_unsigned(const char *text, unsigned limit, unsigned *value)
{
    char *end;
    unsigned long parsed;

    if (text == 0 || text[0] == '\0' || text[0] == '-')
        return -1;
    end = 0;
    parsed = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || parsed > limit)
        return -1;
    *value = (unsigned)parsed;
    return 0;
}

static int
read_mbr(int fd)
{
    if (lseek(fd, 0, SEEK_SET) < 0 ||
        read(fd, &disk_mbr, sizeof(disk_mbr)) != sizeof(disk_mbr)) {
        fprintf(stderr, "fdisk: cannot read the 512-byte MBR\n");
        return -1;
    }
    return 0;
}

static int
get_media_size(int fd)
{
    disk_sector_t sectors64;
    off_t current;
    off_t end;

    sectors64 = 0;
    if (ioctl(fd, DIOCGETSECTORS64, &sectors64) == 0 && sectors64 != 0) {
        if (sectors64 > 0xffffffffull) {
            disk_too_large = 1;
            return -1;
        }
        disk_sectors = (unsigned)sectors64;
        disk_kbytes = disk_sectors >> 1;
        return disk_sectors != 0 ? 0 : -1;
    }

    /* This fallback also makes disk-image files useful for inspection. */
    current = lseek(fd, 0, SEEK_CUR);
    end = lseek(fd, 0, SEEK_END);
    if (current >= 0)
        (void)lseek(fd, current, SEEK_SET);
    if (end < (off_t)FDISK_MBR_BYTES ||
        end / 512 > (off_t)0xffffffffu) {
        if (end >= (off_t)FDISK_MBR_BYTES)
            disk_too_large = 1;
        return -1;
    }
    disk_sectors = (unsigned)(end / 512);
    disk_kbytes = disk_sectors >> 1;
    return disk_sectors != 0 ? 0 : -1;
}

static int
write_mbr(int fd)
{
    if (lseek(fd, 0, SEEK_SET) < 0 ||
        write(fd, &disk_mbr, sizeof(disk_mbr)) != sizeof(disk_mbr)) {
        fprintf(stderr, "fdisk: cannot write the 512-byte MBR\n");
        return -1;
    }
    sync();
    (void)ioctl(fd, DIOCREINIT);
    return 0;
}

static int
partition_used(const struct fdisk_partition *partition)
{
    return partition->fp_type != 0 &&
        fdisk_partition_sectors(partition) != 0;
}

static void
print_table(const char *device)
{
    const struct fdisk_partition *partition;
    unsigned start;
    unsigned sectors;
    unsigned i;

    printf("%s: %u kbytes, %u sectors of 512 bytes\n",
        device, disk_kbytes, disk_sectors);
    printf("Part       Start     Sectors      Kbytes Type Boot\n");
    for (i = 0; i < FDISK_MBR_PARTITIONS; ++i) {
        partition = &disk_mbr.fm_partitions[i];
        if (!partition_used(partition))
            continue;
        start = fdisk_partition_start(partition);
        sectors = fdisk_partition_sectors(partition);
        printf("%4u %11u %11u %11u   %02x   %c\n", i + 1,
            start, sectors, sectors >> 1, partition->fp_type,
            partition->fp_status & FDISK_PARTITION_ACTIVE ? '*' : '-');
    }
}

static int
delete_partition(unsigned number)
{
    struct fdisk_partition *partition;

    partition = &disk_mbr.fm_partitions[number - 1];
    if (!partition_used(partition)) {
        fprintf(stderr, "fdisk: partition %u does not exist\n", number);
        return -1;
    }
    fdisk_partition_clear(partition);
    return 0;
}

static int
set_partition_type(unsigned number, unsigned type)
{
    struct fdisk_partition *partition;

    partition = &disk_mbr.fm_partitions[number - 1];
    if (!partition_used(partition)) {
        fprintf(stderr, "fdisk: partition %u does not exist\n", number);
        return -1;
    }
    partition->fp_type = (unsigned char)type;
    return 0;
}

static int
toggle_active(unsigned number)
{
    struct fdisk_partition *partition;

    partition = &disk_mbr.fm_partitions[number - 1];
    if (!partition_used(partition)) {
        fprintf(stderr, "fdisk: partition %u does not exist\n", number);
        return -1;
    }
    partition->fp_status ^= FDISK_PARTITION_ACTIVE;
    return 0;
}

static int
new_partition(unsigned size_kbytes, unsigned type)
{
    struct fdisk_partition *partition;
    unsigned empty;
    unsigned start;
    unsigned end;
    unsigned sectors;
    unsigned i;

    empty = FDISK_MBR_PARTITIONS;
    start = 2;                 /* Keep the first 1 KiB for the MBR. */
    for (i = 0; i < FDISK_MBR_PARTITIONS; ++i) {
        partition = &disk_mbr.fm_partitions[i];
        if (!partition_used(partition)) {
            if (empty == FDISK_MBR_PARTITIONS)
                empty = i;
            continue;
        }
        end = fdisk_partition_start(partition) +
            fdisk_partition_sectors(partition);
        if (end > start)
            start = end;
    }
    if (empty == FDISK_MBR_PARTITIONS) {
        fprintf(stderr, "fdisk: partition table is full\n");
        return -1;
    }
    if (start >= disk_sectors) {
        fprintf(stderr, "fdisk: no free space after the last partition\n");
        return -1;
    }
    if (size_kbytes == 0) {
        sectors = disk_sectors - start;
    } else {
        if (size_kbytes > (~0u >> 1)) {
            fprintf(stderr, "fdisk: partition size is too large\n");
            return -1;
        }
        sectors = size_kbytes << 1;
        if (sectors == 0 || sectors > disk_sectors - start) {
            fprintf(stderr, "fdisk: partition does not fit on the device\n");
            return -1;
        }
    }
    fdisk_partition_set(&disk_mbr.fm_partitions[empty], 0,
        (unsigned char)type, start, sectors);
    printf("fdisk: created partition %u at sector %u, %u sectors\n",
        empty + 1, start, sectors);
    return 0;
}

int
main(int argc, char **argv)
{
    enum fdisk_action action;
    const char *device;
    unsigned number;
    unsigned size_kbytes;
    unsigned type;
    int type_seen;
    int initialize;
    int open_flags;
    int remaining;
    int fd;
    int opt;
    int result;

    action = FDISK_ACTION_NONE;
    type = PTYPE_BSDFFS;
    type_seen = 0;
    initialize = 0;
    while ((opt = getopt(argc, argv, "Twpdnt:a")) != -1) {
        switch (opt) {
        case 'p':
            if (select_action(&action, FDISK_ACTION_PRINT) != 0)
                return 2;
            break;
        case 'd':
            if (select_action(&action, FDISK_ACTION_DELETE) != 0)
                return 2;
            break;
        case 'n':
            if (select_action(&action, FDISK_ACTION_NEW) != 0)
                return 2;
            break;
        case 'a':
            if (select_action(&action, FDISK_ACTION_ACTIVE) != 0)
                return 2;
            break;
        case 'T':
            if (select_action(&action, FDISK_ACTION_TYPE) != 0)
                return 2;
            break;
        case 't':
            if (parse_unsigned(optarg, 0xffu, &type) != 0 || type == 0) {
                fprintf(stderr, "fdisk: invalid partition type: %s\n",
                    optarg);
                return 2;
            }
            type_seen = 1;
            break;
        case 'w':
            initialize = 1;
            break;
        default:
            usage();
            return 2;
        }
    }

    remaining = argc - optind;
    if (action == FDISK_ACTION_NONE || remaining < 1 ||
        (action == FDISK_ACTION_PRINT && remaining != 1) ||
        (action == FDISK_ACTION_NEW && (remaining < 1 || remaining > 2)) ||
        (action != FDISK_ACTION_PRINT && action != FDISK_ACTION_NEW &&
        remaining != 2) ||
        (initialize && action != FDISK_ACTION_NEW) ||
        (type_seen && action != FDISK_ACTION_NEW &&
        action != FDISK_ACTION_TYPE) ||
        (action == FDISK_ACTION_TYPE && !type_seen)) {
        usage();
        return 2;
    }

    device = argv[optind++];
    number = 0;
    size_kbytes = 0;
    if (action == FDISK_ACTION_NEW && optind < argc) {
        if (parse_unsigned(argv[optind], ~0u, &size_kbytes) != 0) {
            fprintf(stderr, "fdisk: invalid size: %s\n", argv[optind]);
            return 2;
        }
    } else if (action != FDISK_ACTION_PRINT &&
        parse_unsigned(argv[optind], FDISK_MBR_PARTITIONS, &number) != 0) {
        fprintf(stderr, "fdisk: invalid partition number: %s\n",
            argv[optind]);
        return 2;
    }
    if (number == 0 && action != FDISK_ACTION_PRINT &&
        action != FDISK_ACTION_NEW) {
        fprintf(stderr, "fdisk: partition number must be 1..4\n");
        return 2;
    }

    open_flags = action == FDISK_ACTION_PRINT ? O_RDONLY : O_RDWR;
    fd = open(device, open_flags);
    if (fd < 0) {
        perror(device);
        return 1;
    }
    result = 1;
    if (read_mbr(fd) != 0 || get_media_size(fd) != 0) {
        if (disk_too_large)
            fprintf(stderr,
                "fdisk: device exceeds the MBR 32-bit LBA limit; use gpt\n");
        else if (disk_sectors == 0)
            fprintf(stderr, "fdisk: cannot determine device size\n");
        goto done;
    }

    if (initialize) {
        fdisk_mbr_initialize(&disk_mbr);
    } else if (fdisk_mbr_validate(&disk_mbr, disk_sectors) != 0) {
        fprintf(stderr,
            "fdisk: invalid MBR signature, partition range, or overlap\n");
        goto done;
    }

    switch (action) {
    case FDISK_ACTION_PRINT:
        print_table(device);
        result = 0;
        break;
    case FDISK_ACTION_DELETE:
        result = delete_partition(number);
        break;
    case FDISK_ACTION_NEW:
        result = new_partition(size_kbytes, type);
        break;
    case FDISK_ACTION_ACTIVE:
        result = toggle_active(number);
        break;
    case FDISK_ACTION_TYPE:
        result = set_partition_type(number, type);
        break;
    default:
        result = -1;
        break;
    }
    if (result == 0 && action != FDISK_ACTION_PRINT) {
        if (fdisk_mbr_validate(&disk_mbr, disk_sectors) != 0) {
            fprintf(stderr, "fdisk: resulting partition table is invalid\n");
            result = -1;
        } else if (write_mbr(fd) != 0) {
            result = -1;
        } else {
            print_table(device);
        }
    }
    result = result == 0 ? 0 : 1;

done:
    close(fd);
    return result;
}
