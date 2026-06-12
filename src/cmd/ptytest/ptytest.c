#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char master_msg[] = "master-to-slave";
static const char slave_msg[] = "slave-to-master";

static void
usage(void)
{
	fprintf(stderr, "usage: ptytest [unit]\n");
	exit(1);
}

static void
timeout(int sig)
{
	(void)sig;
	fprintf(stderr, "ptytest: timeout\n");
	exit(1);
}

static int
parse_unit(char *text)
{
	char *end;
	unsigned long value;

	errno = 0;
	value = strtoul(text, &end, 0);
	if (errno || *text == '\0' || *end != '\0' || value > 3)
		usage();
	return (int)value;
}

static void
set_raw(int fd, char *path)
{
	struct sgttyb sg;

	if (ioctl(fd, TIOCGETP, &sg) < 0) {
		fprintf(stderr, "ptytest: %s: TIOCGETP: %s\n",
		    path, strerror(errno));
		exit(1);
	}
	sg.sg_flags |= RAW;
	sg.sg_flags &= ~(CBREAK | ECHO | CRMOD);
	if (ioctl(fd, TIOCSETP, &sg) < 0) {
		fprintf(stderr, "ptytest: %s: TIOCSETP: %s\n",
		    path, strerror(errno));
		exit(1);
	}
}

static void
write_all(int fd, const char *path, const char *data, int len)
{
	int done, n;

	done = 0;
	while (done < len) {
		n = write(fd, data + done, len - done);
		if (n <= 0) {
			fprintf(stderr, "ptytest: %s: write: %s\n",
			    path, n < 0 ? strerror(errno) : "short write");
			exit(1);
		}
		done += n;
	}
}

static void
read_exact(int fd, const char *path, char *buf, int len)
{
	int done, n;

	done = 0;
	while (done < len) {
		n = read(fd, buf + done, len - done);
		if (n <= 0) {
			fprintf(stderr, "ptytest: %s: read: %s\n",
			    path, n < 0 ? strerror(errno) : "eof");
			exit(1);
		}
		done += n;
	}
}

static void
check_data(char *label, const char *expected, const char *actual, int len)
{
	if (memcmp(expected, actual, len) != 0) {
		fprintf(stderr, "ptytest: %s data mismatch\n", label);
		exit(1);
	}
}

int
main(int argc, char **argv)
{
	char master_path[16], slave_path[16];
	char buf[32];
	int unit, master, slave;

	if (argc > 2)
		usage();
	unit = argc == 2 ? parse_unit(argv[1]) : 0;

	sprintf(master_path, "/dev/ptyp%d", unit);
	sprintf(slave_path, "/dev/ttyp%d", unit);

	signal(SIGALRM, timeout);
	alarm(5);

	master = open(master_path, O_RDWR);
	if (master < 0) {
		fprintf(stderr, "ptytest: %s: %s\n",
		    master_path, strerror(errno));
		return 1;
	}
	slave = open(slave_path, O_RDWR);
	if (slave < 0) {
		fprintf(stderr, "ptytest: %s: %s\n",
		    slave_path, strerror(errno));
		return 1;
	}
	set_raw(slave, slave_path);

	write_all(master, master_path, master_msg, sizeof(master_msg) - 1);
	read_exact(slave, slave_path, buf, sizeof(master_msg) - 1);
	check_data("master-to-slave", master_msg, buf, sizeof(master_msg) - 1);

	write_all(slave, slave_path, slave_msg, sizeof(slave_msg) - 1);
	read_exact(master, master_path, buf, sizeof(slave_msg) - 1);
	check_data("slave-to-master", slave_msg, buf, sizeof(slave_msg) - 1);

	alarm(0);
	close(slave);
	close(master);
	printf("ptytest: %s <-> %s ok\n", master_path, slave_path);
	return 0;
}
