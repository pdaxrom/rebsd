#ifndef _I386_IDE_H_
#define _I386_IDE_H_

#include <disk/disk.h>

/* Thin i686 attachment for the common PCI IDE backend. */
int i386_ide_probe(void);
const struct disk_backend_ops *i386_ide_backend_ops(void);
void *i386_ide_backend_arg(void);
disk_sector_t i386_ide_sector_count(void);

#endif
