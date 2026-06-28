#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>

int
main()
{
	fd_set rfds;
	struct sockaddr_in sin;
	struct sockaddr_un sun;
	int fd;
	int n;
	long request;

	fd = -1;
	FD_ZERO(&rfds);
	FD_SET(0, &rfds);
	n = FD_ISSET(0, &rfds);
	request = FIONREAD;

	sin.sin_family = AF_INET;
	sin.sin_port = htons(7);
	sin.sin_addr.s_addr = htonl((127L << 24) | 1);

	sun.sun_family = AF_UNIX;
	sun.sun_path[0] = 0;

	if (fd != -1)
		(void) close(fd);
	return n != 0 && request != 0 ? 0 : 1;
}
