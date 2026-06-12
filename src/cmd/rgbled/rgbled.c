#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <machine/n64cart_rgbled.h>

#define RGBLED_DEV "/dev/rgbled0"

static void
usage(void)
{
	fprintf(stderr, "usage: rgbled [red green blue]\n");
	exit(1);
}

static int
parse_channel(const char *name, const char *text)
{
	char *end;
	unsigned long value;

	errno = 0;
	value = strtoul(text, &end, 0);
	if (errno || *text == '\0' || *end != '\0' || value > 255) {
		fprintf(stderr, "rgbled: invalid %s value '%s'\n", name, text);
		exit(1);
	}
	return (int)value;
}

static int
open_rgbled(void)
{
	int fd;

	fd = open(RGBLED_DEV, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "rgbled: %s: %s\n", RGBLED_DEV, strerror(errno));
		exit(1);
	}
	return fd;
}

int
main(int argc, char **argv)
{
	unsigned value;
	int fd;

	if (argc != 1 && argc != 4)
		usage();

	fd = open_rgbled();

	if (argc == 4) {
		value = ((unsigned)parse_channel("red", argv[1]) << 16) |
		    ((unsigned)parse_channel("green", argv[2]) << 8) |
		    (unsigned)parse_channel("blue", argv[3]);
		if (ioctl(fd, N64RGBLEDIOC_SET, &value) < 0) {
			fprintf(stderr, "rgbled: set: %s\n", strerror(errno));
			close(fd);
			return 1;
		}
	}

	if (ioctl(fd, N64RGBLEDIOC_GET, &value) < 0) {
		fprintf(stderr, "rgbled: get: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	printf("%u %u %u\n", (value >> 16) & 0xff,
	    (value >> 8) & 0xff, value & 0xff);

	close(fd);
	return 0;
}
