#ifndef _I386_IDE_H_
#define _I386_IDE_H_

/*
 * Probe the primary master through the legacy ATA compatibility ports.
 * The bootstrap probe issues only IDENTIFY DEVICE and READ SECTORS.
 */
int i386_ide_probe(void);

#endif
