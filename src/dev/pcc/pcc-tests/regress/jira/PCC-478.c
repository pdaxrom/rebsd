typedef char *caddr_t;
typedef long off_t;

struct inode {
	int dummy;
};

enum uio_rw;
int rdwri(enum uio_rw, struct inode *, caddr_t, int, off_t, int, int *);

enum uio_rw {
	UIO_READ,
	UIO_WRITE
};

int
rdwri(enum uio_rw rw, struct inode *ip, caddr_t base, int len, off_t off,
    int flags, int *resid)
{
	return rw != UIO_READ || ip == 0 || base == 0 || len != 1 ||
	    off != 2 || flags != 3 || resid != 0;
}

int
main(void)
{
	struct inode ip;
	char byte;

	return rdwri(UIO_READ, &ip, &byte, 1, (off_t)2, 3, 0);
}
