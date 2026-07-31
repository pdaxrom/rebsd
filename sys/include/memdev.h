#ifndef _SYS_MEMDEV_H_
#define _SYS_MEMDEV_H_

#include <sys/types.h>

struct uio;

int memdev_nullzero_open(dev_t, int, int);
int memdev_nullzero_rw(dev_t, struct uio *, int);

#endif /* _SYS_MEMDEV_H_ */
