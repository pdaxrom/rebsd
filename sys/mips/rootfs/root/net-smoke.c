#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>

int close();
int read();
int recvfrom();
int sendto();
int socketpair();
int write();

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
	int fd, rfd, sfd, ufd;
	int sv[2];
	int len, n;
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
