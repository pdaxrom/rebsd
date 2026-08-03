#include "disk_bootstrap.h"
#include "ide.h"

#include <disk/disk.h>
#include <pci/pciide.h>
#include <sys/systm.h>

int
i386_disk_attach_ide(void)
{
    struct disk_attach_args args;

    bzero(&args, sizeof(args));
    args.da_ops = i386_ide_backend_ops();
    args.da_arg = i386_ide_backend_arg();
    args.da_sector_count = i386_ide_sector_count();
    args.da_sector_size = DISK_SECTOR_SIZE;
    args.da_flags = DISK_FLAG_READ_ONLY;
    args.da_read_ahead_sectors = PCIIDE_DMA_MAX_SECTORS;
    return disk_attach(&args, 0);
}
