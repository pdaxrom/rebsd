#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>

int close();
int accept();
int bind();
int connect();
int fork();
int getsockname();
int listen();
int read();
int recvfrom();
int sendto();
int socketpair();
int wait();
int write();
void _exit();

static int
fail(name)
	char *name;
{
	printf("%s failed errno=%d\n", name, errno);
	return (1);
}

int
main()
{
	struct sockaddr_in sin, dst, from;
	struct sockaddr_in got;
	int fd, rfd, sfd, ufd, lfd, cfd, afd;
	int sv[2];
	int len, n, pid, status;
	char ch, buf[4];

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
		if (write(cfd, "tcp", 3) != 3)
			_exit(4);
		n = read(cfd, buf, 2);
		if (n != 2 || buf[0] != 'o' || buf[1] != 'k')
			_exit(5);
		if (close(cfd) < 0)
			_exit(6);
		_exit(0);
	}

	len = sizeof(from);
	afd = accept(lfd, (struct sockaddr *)&from, &len);
	if (afd < 0)
		return (fail("accept tcp loopback"));
	n = read(afd, buf, sizeof(buf));
	if (n != 3)
		return (fail("read tcp loopback"));
	if (buf[0] != 't' || buf[1] != 'c' || buf[2] != 'p') {
		printf("tcp loopback data mismatch\n");
		return (1);
	}
	if (write(afd, "ok", 2) != 2)
		return (fail("write tcp loopback"));
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
	ch = 'x';
	if (write(sv[0], &ch, 1) != 1)
		return (fail("write unix stream"));
	ch = 0;
	if (read(sv[1], &ch, 1) != 1)
		return (fail("read unix stream"));
	if (ch != 'x') {
		printf("unix stream data mismatch\n");
		return (1);
	}
	if (close(sv[0]) < 0)
		return (fail("close unix stream 0"));
	if (close(sv[1]) < 0)
		return (fail("close unix stream 1"));

	printf("net socket smoke ok\n");
	return (0);
}
