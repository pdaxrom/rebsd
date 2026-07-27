#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <stdint.h>

int
mips_mmrw(dev_t dev, struct uio *uio, int flag)
{
    register struct iovec *iov;
    int error;
    register u_int c;
    uintptr_t memaddr;
    uintptr_t memlast;

    (void)flag;
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
        case NULL_MINOR:
            if (uio->uio_rw == UIO_READ)
                return 0;
            c = iov->iov_len;
            iov->iov_base += c;
            iov->iov_len -= c;
            uio->uio_offset += c;
            uio->uio_resid -= c;
            break;
        case ZERO_MINOR:
            if (uio->uio_rw == UIO_WRITE)
                return EIO;
            c = iov->iov_len;
            bzero(iov->iov_base, c);
            iov->iov_base += c;
            iov->iov_len -= c;
            uio->uio_offset += c;
            uio->uio_resid -= c;
            break;
        default:
            return EINVAL;
        }
    }

    return error;
}
