#ifndef _MIPS_COMMON_DEVSW_H_
#define _MIPS_COMMON_DEVSW_H_

#include <sys/types.h>

struct uio;

int mips_mmrw(dev_t dev, struct uio *uio, int flag);

#endif
