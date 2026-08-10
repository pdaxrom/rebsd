#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>

int accept();
int bind();
int close();
int connect();
int getpid();
int listen();
int read();
int recvfrom();
int sendto();
int socket();
int socketpair();
int unlink();
int write();

static char stream_path[64];
static char dgram_recv_path[64];
static char dgram_send_path[64];

static void
cleanup()
{
	if (stream_path[0] != 0)
		(void)unlink(stream_path);
	if (dgram_recv_path[0] != 0)
		(void)unlink(dgram_recv_path);
	if (dgram_send_path[0] != 0)
		(void)unlink(dgram_send_path);
}

static int
fail(name)
	char *name;
{
	printf("unixdomain-smoke: %s failed errno=%d\n", name, errno);
	cleanup();
	return (1);
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
socketpair_smoke()
{
	int sv[2];
	char ch;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
		return (fail("socketpair stream"));
	ch = 'p';
	if (write(sv[0], &ch, 1) != 1)
		return (fail("socketpair write 0"));
	ch = 0;
	if (read(sv[1], &ch, 1) != 1 || ch != 'p')
		return (fail("socketpair read 1"));
	ch = 'q';
	if (write(sv[1], &ch, 1) != 1)
		return (fail("socketpair write 1"));
	ch = 0;
	if (read(sv[0], &ch, 1) != 1 || ch != 'q')
		return (fail("socketpair read 0"));
	if (close(sv[0]) < 0 || close(sv[1]) < 0)
		return (fail("socketpair close"));
	return (0);
}

static int
stream_smoke()
{
	struct sockaddr_un sun, from;
	int lfd, cfd, afd, len;
	char ch;

	sprintf(stream_path, "/tmp/unix-stream.%d", getpid());
	(void)unlink(stream_path);
	lfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (lfd < 0)
		return (fail("socket stream listener"));
	unix_addr(&sun, stream_path);
	if (bind(lfd, (struct sockaddr *)&sun, sizeof(sun)) < 0)
		return (fail("bind stream"));
	if (listen(lfd, 1) < 0)
		return (fail("listen stream"));
	cfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (cfd < 0)
		return (fail("socket stream client"));
	if (connect(cfd, (struct sockaddr *)&sun, sizeof(sun)) < 0)
		return (fail("connect stream"));
	len = sizeof(from);
	afd = accept(lfd, (struct sockaddr *)&from, &len);
	if (afd < 0)
		return (fail("accept stream"));
	ch = 's';
	if (write(cfd, &ch, 1) != 1)
		return (fail("write stream"));
	ch = 0;
	if (read(afd, &ch, 1) != 1 || ch != 's')
		return (fail("read stream"));
	if (close(afd) < 0 || close(cfd) < 0 || close(lfd) < 0)
		return (fail("close stream"));
	if (unlink(stream_path) < 0)
		return (fail("unlink stream"));
	stream_path[0] = 0;
	return (0);
}

static int
datagram_smoke()
{
	struct sockaddr_un recv_addr, send_addr, from;
	int rfd, sfd, len, n;
	char buf[4];

	sprintf(dgram_recv_path, "/tmp/unix-dgram-r.%d", getpid());
	sprintf(dgram_send_path, "/tmp/unix-dgram-s.%d", getpid());
	(void)unlink(dgram_recv_path);
	(void)unlink(dgram_send_path);
	rfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (rfd < 0)
		return (fail("socket datagram receiver"));
	sfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sfd < 0)
		return (fail("socket datagram sender"));
	unix_addr(&recv_addr, dgram_recv_path);
	unix_addr(&send_addr, dgram_send_path);
	if (bind(rfd, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0)
		return (fail("bind datagram receiver"));
	if (bind(sfd, (struct sockaddr *)&send_addr, sizeof(send_addr)) < 0)
		return (fail("bind datagram sender"));
	if (sendto(sfd, "dgm", 3, 0, (struct sockaddr *)&recv_addr,
	    sizeof(recv_addr)) != 3)
		return (fail("sendto datagram"));
	len = sizeof(from);
	n = recvfrom(rfd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &len);
	if (n != 3 || buf[0] != 'd' || buf[1] != 'g' || buf[2] != 'm')
		return (fail("recvfrom datagram"));
	if (from.sun_family != AF_UNIX)
		return (fail("datagram sender address"));
	if (close(sfd) < 0 || close(rfd) < 0)
		return (fail("close datagram"));
	if (unlink(dgram_recv_path) < 0 || unlink(dgram_send_path) < 0)
		return (fail("unlink datagram"));
	dgram_recv_path[0] = 0;
	dgram_send_path[0] = 0;
	return (0);
}

int
main()
{
	if (socketpair_smoke() != 0)
		return (1);
	if (stream_smoke() != 0)
		return (1);
	if (datagram_smoke() != 0)
		return (1);
	printf("unixdomain socket smoke ok\n");
	return (0);
}
