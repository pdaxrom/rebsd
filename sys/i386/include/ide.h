#ifndef _I386_IDE_H_
#define _I386_IDE_H_

#include <disk/disk.h>

/*
 * Probe the primary master through the legacy ATA compatibility ports.
 * The backend issues only IDENTIFY DEVICE and READ SECTORS; its write and
 * flush operations are deliberately absent.
 */
int i386_ide_probe(void);
const struct disk_backend_ops *i386_ide_backend_ops(void);
disk_sector_t i386_ide_sector_count(void);

#endif
