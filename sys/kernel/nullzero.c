/* Common /dev/null and /dev/zero character-device operations. */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/memdev.h>
#include <sys/systm.h>
#include <sys/uio.h>

int
memdev_nullzero_open(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    if (minor(dev) != NULL_MINOR && minor(dev) != ZERO_MINOR)
        return ENXIO;
    return 0;
}

int
memdev_nullzero_rw(dev_t dev, struct uio *uio, int flag)
{
    struct iovec *iov;
    u_int count;

    (void)flag;
    if (minor(dev) == NULL_MINOR && uio->uio_rw == UIO_READ)
        return 0;
    if (minor(dev) == ZERO_MINOR && uio->uio_rw == UIO_WRITE)
        return EIO;
    if (minor(dev) != NULL_MINOR && minor(dev) != ZERO_MINOR)
        return EINVAL;

    while (uio->uio_resid != 0) {
        iov = uio->uio_iov;
        if (iov->iov_len == 0) {
            ++uio->uio_iov;
            if (--uio->uio_iovcnt < 0)
                panic("memdev_nullzero_rw");
            continue;
        }
        count = iov->iov_len;
        if (minor(dev) == ZERO_MINOR)
            bzero(iov->iov_base, count);
        iov->iov_base += count;
        iov->iov_len -= count;
        uio->uio_offset += count;
        uio->uio_resid -= count;
    }
    return 0;
}
