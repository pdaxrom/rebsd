#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>

int close();
int accept();
int bind();
int connect();
int fork();
int getpeername();
int getsockname();
int getsockopt();
int ioctl();
int listen();
int read();
int recv();
int recvfrom();
int send();
int sendto();
int select();
int shutdown();
int socket();
int socketpair();
int setsockopt();
int system();
int unlink();
int wait();
int write();
int getpid();
u_long htonl();
void _exit();

#define FIONREAD_SMOKE	0x40046661L
#define FIONBIO_SMOKE	0x80046660L

static fd_set *no_fds;

static int
fail(name)
	char *name;
{
	printf("%s failed errno=%d\n", name, errno);
	return (1);
}

static int
fd_is_set(fd, set)
	int fd;
	fd_set *set;
{
	return ((set->fds_bits[0] & (1L << fd)) != 0);
}

static void
fd_set_one(fd, set)
	int fd;
	fd_set *set;
{
	set->fds_bits[0] = 1L << fd;
}

static int
tcp_netstat()
{
	int status;

	status = system("/usr/bin/netstat -a -f inet");
	if (status != 0) {
		printf("netstat tcp status=%d\n", status);
		return (1);
	}
	return (0);
}

static int
tcp_reuse_bind(addr)
	struct sockaddr_in *addr;
{
	int fd, one;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return (fail("socket tcp reuse"));
	one = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
	    (char *)&one, sizeof(one)) < 0)
		return (fail("setsockopt tcp reuse"));
	if (bind(fd, (struct sockaddr *)addr, sizeof(*addr)) < 0)
		return (fail("bind tcp reuse"));
	if (listen(fd, 1) < 0)
		return (fail("listen tcp reuse"));
	if (close(fd) < 0)
		return (fail("close tcp reuse"));
	return (0);
}

static void
unix_addr(sun, path)
	struct sockaddr_un *sun;
	char *path;
{
	int i;

	sun->sun_family = AF_UNIX;
	for (i = 0; i < sizeof(sun->sun_path); i++)
		sun->sun_path[i] = 0;
	for (i = 0; path[i] && i < sizeof(sun->sun_path) - 1; i++)
		sun->sun_path[i] = path[i];
}

static int
unix_path_smoke()
{
	struct sockaddr_un sun, from;
	int lfd, cfd, afd, len;
	char path[64];
	char ch;

	sprintf(path, "/tmp/net-smoke-unix.%d", getpid());
	(void)unlink(path);
	lfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (lfd < 0)
		return (fail("socket unix listen"));
	unix_addr(&sun, path);
	if (bind(lfd, (struct sockaddr *)&sun, sizeof(sun)) < 0)
		return (fail("bind unix path"));
	if (listen(lfd, 1) < 0)
		return (fail("listen unix path"));

	cfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (cfd < 0)
		return (fail("socket unix connect"));
	if (connect(cfd, (struct sockaddr *)&sun, sizeof(sun)) < 0)
		return (fail("connect unix path"));
	len = sizeof(from);
	afd = accept(lfd, (struct sockaddr *)&from, &len);
	if (afd < 0)
		return (fail("accept unix path"));
	ch = 'u';
	if (write(cfd, &ch, 1) != 1)
		return (fail("write unix path"));
	ch = 0;
	if (read(afd, &ch, 1) != 1)
		return (fail("read unix path"));
	if (ch != 'u') {
		printf("unix path data mismatch\n");
		return (1);
	}
	if (close(afd) < 0)
		return (fail("close unix accepted"));
	if (close(cfd) < 0)
		return (fail("close unix connect"));
	if (close(lfd) < 0)
		return (fail("close unix listen"));
	if (unlink(path) < 0)
		return (fail("unlink unix path"));
	return (0);
}

static int
select_ioctl_smoke(rd, wr)
	int rd, wr;
{
	fd_set rfds, wfds;
	struct timeval tv;
	long nread;
	int n, on;
	char ch;

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	fd_set_one(rd, &rfds);
	n = select(rd + 1, &rfds, no_fds, no_fds, &tv);
	if (n < 0)
		return (fail("select unix empty"));
	if (n != 0) {
		printf("select unix empty returned %d\n", n);
		return (1);
	}

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	fd_set_one(wr, &wfds);
	n = select(wr + 1, no_fds, &wfds, no_fds, &tv);
	if (n < 0)
		return (fail("select unix write"));
	if (n != 1 || !fd_is_set(wr, &wfds)) {
		printf("select unix write returned %d\n", n);
		return (1);
	}

	ch = 'x';
	if (write(wr, &ch, 1) != 1)
		return (fail("write unix stream"));
	if (ioctl(rd, FIONREAD_SMOKE, &nread) < 0)
		return (fail("ioctl unix fionread"));
	if (nread != 1) {
		printf("ioctl unix fionread=%ld\n", nread);
		return (1);
	}
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	fd_set_one(rd, &rfds);
	n = select(rd + 1, &rfds, no_fds, no_fds, &tv);
	if (n < 0)
		return (fail("select unix read"));
	if (n != 1 || !fd_is_set(rd, &rfds)) {
		printf("select unix read returned %d\n", n);
		return (1);
	}
	ch = 0;
	if (read(rd, &ch, 1) != 1)
		return (fail("read unix stream"));
	if (ch != 'x') {
		printf("unix stream data mismatch\n");
		return (1);
	}
	if (ioctl(rd, FIONREAD_SMOKE, &nread) < 0)
		return (fail("ioctl unix fionread empty"));
	if (nread != 0) {
		printf("ioctl unix fionread empty=%ld\n", nread);
		return (1);
	}
	on = 1;
	if (ioctl(rd, FIONBIO_SMOKE, &on) < 0)
		return (fail("ioctl unix fionbio on"));
	errno = 0;
	n = read(rd, &ch, 1);
	if (n != -1 || errno != EWOULDBLOCK) {
		printf("nonblocking unix read n=%d errno=%d\n", n, errno);
		return (1);
	}
	on = 0;
	if (ioctl(rd, FIONBIO_SMOKE, &on) < 0)
		return (fail("ioctl unix fionbio off"));
	return (0);
}

static int
tcp_select_ioctl_smoke(fd, buf)
	int fd;
	char *buf;
{
	fd_set rfds, wfds;
	struct timeval tv;
	long nread;
	int n, on;

	tv.tv_sec = 5;
	tv.tv_usec = 0;
	fd_set_one(fd, &rfds);
	n = select(fd + 1, &rfds, no_fds, no_fds, &tv);
	if (n < 0)
		return (fail("select tcp read"));
	if (n != 1 || !fd_is_set(fd, &rfds)) {
		printf("select tcp read returned %d\n", n);
		return (1);
	}
	if (ioctl(fd, FIONREAD_SMOKE, &nread) < 0)
		return (fail("ioctl tcp fionread"));
	if (nread < 3) {
		printf("ioctl tcp fionread=%ld\n", nread);
		return (1);
	}
	n = recv(fd, buf, 4, 0);
	if (n != 3)
		return (fail("recv tcp loopback"));
	if (buf[0] != 't' || buf[1] != 'c' || buf[2] != 'p') {
		printf("tcp loopback data mismatch\n");
		return (1);
	}

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	fd_set_one(fd, &wfds);
	n = select(fd + 1, no_fds, &wfds, no_fds, &tv);
	if (n < 0)
		return (fail("select tcp write"));
	if (n != 1 || !fd_is_set(fd, &wfds)) {
		printf("select tcp write returned %d\n", n);
		return (1);
	}

	on = 1;
	if (ioctl(fd, FIONBIO_SMOKE, &on) < 0)
		return (fail("ioctl tcp fionbio on"));
	errno = 0;
	n = read(fd, buf, 1);
	if (n != -1 || errno != EWOULDBLOCK) {
		printf("nonblocking tcp read n=%d errno=%d\n", n, errno);
		return (1);
	}
	on = 0;
	if (ioctl(fd, FIONBIO_SMOKE, &on) < 0)
		return (fail("ioctl tcp fionbio off"));
	return (0);
}

int
main()
{
	struct sockaddr_in sin, dst, from;
	struct sockaddr_in got, peer;
	int fd, rfd, sfd, ufd, lfd, cfd, afd;
	int sv[2];
	int len, n, pid, status, optval;
	char buf[4];

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return (fail("socket udp"));

	sin.sin_family = AF_INET;
	sin.sin_port = 0;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_zero[0] = sin.sin_zero[1] = sin.sin_zero[2] = sin.sin_zero[3] = 0;
	sin.sin_zero[4] = sin.sin_zero[5] = sin.sin_zero[6] = sin.sin_zero[7] = 0;
	if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		return (fail("bind udp any"));

	len = sizeof(got);
	if (getsockname(fd, (struct sockaddr *)&got, &len) < 0)
		return (fail("getsockname udp"));

	if (close(fd) < 0)
		return (fail("close udp"));

	rfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (rfd < 0)
		return (fail("socket udp recv"));
	sin.sin_family = AF_INET;
	sin.sin_port = 0;
	sin.sin_addr.s_addr = htonl(0x7f000001L);
	sin.sin_zero[0] = sin.sin_zero[1] = sin.sin_zero[2] = sin.sin_zero[3] = 0;
	sin.sin_zero[4] = sin.sin_zero[5] = sin.sin_zero[6] = sin.sin_zero[7] = 0;
	if (bind(rfd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		return (fail("bind udp loopback"));
	len = sizeof(got);
	if (getsockname(rfd, (struct sockaddr *)&got, &len) < 0)
		return (fail("getsockname udp loopback"));

	sfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sfd < 0)
		return (fail("socket udp send"));
	dst.sin_family = AF_INET;
	dst.sin_port = got.sin_port;
	dst.sin_addr.s_addr = htonl(0x7f000001L);
	dst.sin_zero[0] = dst.sin_zero[1] = dst.sin_zero[2] = dst.sin_zero[3] = 0;
	dst.sin_zero[4] = dst.sin_zero[5] = dst.sin_zero[6] = dst.sin_zero[7] = 0;
	if (sendto(sfd, "udp", 3, 0, (struct sockaddr *)&dst, sizeof(dst)) != 3)
		return (fail("sendto udp loopback"));
	len = sizeof(from);
	n = recvfrom(rfd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &len);
	if (n != 3)
		return (fail("recvfrom udp loopback"));
	if (buf[0] != 'u' || buf[1] != 'd' || buf[2] != 'p') {
		printf("udp loopback data mismatch\n");
		return (1);
	}
	if (close(sfd) < 0)
		return (fail("close udp send"));
	if (close(rfd) < 0)
		return (fail("close udp recv"));

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0)
		return (fail("socket tcp listen"));
	sin.sin_family = AF_INET;
	sin.sin_port = 0;
	sin.sin_addr.s_addr = htonl(0x7f000001L);
	sin.sin_zero[0] = sin.sin_zero[1] = sin.sin_zero[2] = sin.sin_zero[3] = 0;
	sin.sin_zero[4] = sin.sin_zero[5] = sin.sin_zero[6] = sin.sin_zero[7] = 0;
	if (bind(lfd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		return (fail("bind tcp loopback"));
	if (listen(lfd, 1) < 0)
		return (fail("listen tcp loopback"));
	len = sizeof(got);
	if (getsockname(lfd, (struct sockaddr *)&got, &len) < 0)
		return (fail("getsockname tcp loopback"));

	pid = fork();
	if (pid < 0)
		return (fail("fork tcp loopback"));
	if (pid == 0) {
		cfd = socket(AF_INET, SOCK_STREAM, 0);
		if (cfd < 0)
			_exit(2);
		dst.sin_family = AF_INET;
		dst.sin_port = got.sin_port;
		dst.sin_addr.s_addr = htonl(0x7f000001L);
		dst.sin_zero[0] = dst.sin_zero[1] = dst.sin_zero[2] = dst.sin_zero[3] = 0;
		dst.sin_zero[4] = dst.sin_zero[5] = dst.sin_zero[6] = dst.sin_zero[7] = 0;
		if (connect(cfd, (struct sockaddr *)&dst, sizeof(dst)) < 0)
			_exit(3);
		if (send(cfd, "tcp", 3, 0) != 3)
			_exit(4);
		n = recv(cfd, buf, 2, 0);
		if (n != 2 || buf[0] != 'o' || buf[1] != 'k')
			_exit(5);
		if (shutdown(cfd, 1) < 0)
			_exit(6);
		if (close(cfd) < 0)
			_exit(7);
		_exit(0);
	}

	len = sizeof(from);
	afd = accept(lfd, (struct sockaddr *)&from, &len);
	if (afd < 0)
		return (fail("accept tcp loopback"));
	len = sizeof(peer);
	if (getpeername(afd, (struct sockaddr *)&peer, &len) < 0)
		return (fail("getpeername tcp loopback"));
	if (peer.sin_family != AF_INET ||
	    peer.sin_addr.s_addr != htonl(0x7f000001L)) {
		printf("tcp peer mismatch family=%d addr=%lx\n",
		    peer.sin_family, peer.sin_addr.s_addr);
		return (1);
	}
	len = sizeof(optval);
	if (getsockopt(afd, SOL_SOCKET, SO_TYPE,
	    (char *)&optval, &len) < 0)
		return (fail("getsockopt tcp type"));
	if (optval != SOCK_STREAM) {
		printf("tcp socket type=%d\n", optval);
		return (1);
	}
	if (tcp_select_ioctl_smoke(afd, buf) != 0)
		return (1);
	if (tcp_netstat() != 0)
		return (1);
	if (send(afd, "ok", 2, 0) != 2)
		return (fail("send tcp loopback"));
	if (close(afd) < 0)
		return (fail("close tcp accepted"));
	if (close(lfd) < 0)
		return (fail("close tcp listen"));
	if (wait(&status) != pid)
		return (fail("wait tcp loopback"));
	if (status != 0) {
		printf("tcp loopback child status=%d\n", status);
		return (1);
	}
	if (tcp_reuse_bind(&got) != 0)
		return (1);

	rfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (rfd < 0)
		return (fail("socket raw icmp"));
	if (close(rfd) < 0)
		return (fail("close raw icmp"));

	ufd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ufd < 0)
		return (fail("socket unix stream"));
	if (close(ufd) < 0)
		return (fail("close unix stream"));

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
		return (fail("socketpair unix stream"));
	if (select_ioctl_smoke(sv[1], sv[0]) != 0)
		return (1);
	if (close(sv[0]) < 0)
		return (fail("close unix stream 0"));
	if (close(sv[1]) < 0)
		return (fail("close unix stream 1"));
	if (unix_path_smoke() != 0)
		return (1);

	printf("net socket smoke ok\n");
	return (0);
}
