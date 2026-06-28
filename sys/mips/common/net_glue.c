/*
 * MIPS glue for the imported 2.11BSD networking code.
 */
#include <sys/param.h>
#ifdef INET
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/file.h>
#include <sys/socketvar.h>
#include <sys/inode.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/mbuf.h>
#include <netinet/in.h>

u_short
htons(x)
	u_short x;
{
	return (x);
}

u_short
ntohs(x)
	u_short x;
{
	return (x);
}

u_long
htonl(x)
	u_long x;
{
	return (x);
}

u_long
ntohl(x)
	u_long x;
{
	return (x);
}

cpfromkern(src, dst, len)
	caddr_t src, dst;
	int len;
{
	bcopy(src, dst, len);
}

caddr_t
m_clalloc(ncl, how, canwait)
	int ncl, how, canwait;
{
	(void)ncl;
	(void)how;
	(void)canwait;
	return (0);
}

netcopyout(m, dst, lenp)
	struct mbuf *m;
	caddr_t dst;
	int *lenp;
{
	int error, copied, len, n;

	copied = 0;
	len = *lenp;
	error = 0;
	while (m != 0 && len > 0) {
		n = MIN(m->m_len, len);
		if (n > 0) {
			error = copyout(mtod(m, caddr_t), dst, n);
			if (error)
				break;
			dst += n;
			len -= n;
			copied += n;
		}
		m = m->m_next;
	}
	*lenp = copied;
	return (error);
}

#ifdef UNIXDOMAIN
void
fpflags(fp, set, clear)
	struct file *fp;
	int set, clear;
{
	fp->f_flag |= set;
	fp->f_flag &= ~clear;
}

void
fadjust(fp, msg, cnt)
	struct file *fp;
	int msg, cnt;
{
	fp->f_msgcount += msg;
	fp->f_count += cnt;
}

fpfetch(fp, fpp)
	struct file *fp, *fpp;
{
	*fpp = *fp;
	return (fp->f_count);
}

ufavail()
{
	int i, avail;

	avail = 0;
	for (i = 0; i < NOFILE; i++)
		if (u.u_ofile[i] == 0)
			avail++;
	return (avail);
}

ufalloc(i)
	int i;
{
	for (; i < NOFILE; i++)
		if (u.u_ofile[i] == 0) {
			u.u_rval = i;
			u.u_pofile[i] = 0;
			if (i > u.u_lastfile)
				u.u_lastfile = i;
			return (i);
		}
	u.u_error = EMFILE;
	return (-1);
}

void
unpdet(ip)
	struct inode *ip;
{
	ip->i_socket = 0;
	irele(ip);
}

unpbind(path, len, ipp, unpsock)
	char *path;
	int len;
	struct inode **ipp;
	struct socket *unpsock;
{
	struct inode *ip;
	char pth[MLEN];
	int error;
	struct nameidata nd;

	bcopy(path, pth, len);
	NDINIT(&nd, CREATE, FOLLOW, pth);
	nd.ni_dirp[len - 2] = 0;
	*ipp = 0;
	ip = namei(&nd);
	if (ip) {
		iput(ip);
		return (EADDRINUSE);
	}
	if (u.u_error || (ip = maknode(IFSOCK | 0777, &nd)) == 0) {
		error = u.u_error;
		u.u_error = 0;
		return (error);
	}
	*ipp = ip;
	ip->i_socket = unpsock;
	iunlock(ip);
	return (0);
}

unpconn(path, len, so2, ipp)
	char *path;
	int len;
	struct socket **so2;
	struct inode **ipp;
{
	struct inode *ip;
	char pth[MLEN];
	int error;
	struct nameidata nd;

	bcopy(path, pth, len);
	if (len == 0)
		return (EINVAL);
	NDINIT(&nd, LOOKUP, FOLLOW, pth);
	nd.ni_dirp[len - 2] = 0;
	ip = namei(&nd);
	*ipp = ip;
	if (ip == 0 || access(ip, IWRITE)) {
		error = u.u_error;
		u.u_error = 0;
		return (error);
	}
	if ((ip->i_mode & IFMT) != IFSOCK)
		return (ENOTSOCK);
	*so2 = ip->i_socket;
	if (*so2 == 0)
		return (ECONNREFUSED);
	return (0);
}

unpgc1(beginf, endf)
	struct file **beginf, **endf;
{
	struct file *fp;

	for (*beginf = fp = file; fp < file + NFILE; fp++)
		fp->f_flag &= ~(FMARK | FDEFER);
	*endf = file + NFILE;
}

unpdisc(fp)
	struct file *fp;
{
	--fp->f_msgcount;
	return (closef(fp));
}
#endif
#endif
