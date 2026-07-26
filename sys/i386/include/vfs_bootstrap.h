#ifndef _I386_VFS_BOOTSTRAP_H_
#define _I386_VFS_BOOTSTRAP_H_

#include <sys/types.h>

int i386_vfs_bootstrap_mount(dev_t preferred_dev, dev_t fallback_dev);
int i386_vfs_bootstrap_init_image(const void **data, unsigned *size);

#endif
