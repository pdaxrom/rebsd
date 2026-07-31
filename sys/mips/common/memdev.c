#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/memdev.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <stdint.h>

int
mips_mmrw(dev_t dev, struct uio *uio, int flag)
{
    register struct iovec *iov;
    int error;
    uintptr_t memaddr;
    uintptr_t memlast;

    (void)flag;
    if (minor(dev) == NULL_MINOR || minor(dev) == ZERO_MINOR)
        return memdev_nullzero_rw(dev, uio, flag);
    error = 0;
    while (uio->uio_resid && error == 0) {
        iov = uio->uio_iov;
        if (iov->iov_len == 0) {
            uio->uio_iov++;
            uio->uio_iovcnt--;
            if (uio->uio_iovcnt < 0)
                panic("mips_mmrw");
            continue;
        }

        switch (minor(dev)) {
        case 0:
        case 1:
            if (uio->uio_offset < 0 || uio->uio_offset > UINTPTR_MAX ||
                (off_t)(iov->iov_len - 1) >
                (off_t)UINTPTR_MAX - uio->uio_offset)
                return EFAULT;
            memaddr = (uintptr_t)uio->uio_offset;
            memlast = memaddr + iov->iov_len - 1;
            if ((badkaddr((caddr_t)memaddr) &&
                baduaddr((caddr_t)memaddr)) ||
                (badkaddr((caddr_t)memlast) &&
                baduaddr((caddr_t)memlast)))
                return EFAULT;
            error = uiomove((caddr_t)memaddr, iov->iov_len, uio);
            break;
        default:
            return EINVAL;
        }
    }

    return error;
}
