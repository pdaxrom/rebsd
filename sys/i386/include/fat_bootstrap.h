#ifndef _I386_FAT_BOOTSTRAP_H_
#define _I386_FAT_BOOTSTRAP_H_

#include <sys/types.h>

int i386_fat_bootstrap(dev_t, unsigned);
int i386_fat_bootstrap_init_image(const void **, unsigned *);

#endif
