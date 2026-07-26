/*
 * Convert a compiled libmagic database to a requested byte order.
 *
 * Upstream .mgc files contain fixed-size struct magic records.  libmagic can
 * byte-swap a foreign database at load time, but doing that through a private
 * writable mapping dirties every database page.  Producing both target byte
 * orders at build time keeps the runtime mapping genuinely read-only and
 * demand-paged on both CI20 and N64.
 */
#include "file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t
swap2(uint16_t value)
{
	return (uint16_t)((value << 8) | (value >> 8));
}

static uint32_t
swap4(uint32_t value)
{
	return ((value & 0x000000ffU) << 24) |
	    ((value & 0x0000ff00U) << 8) |
	    ((value & 0x00ff0000U) >> 8) |
	    ((value & 0xff000000U) >> 24);
}

static uint64_t
swap8(uint64_t value)
{
	return ((uint64_t)swap4((uint32_t)value) << 32) |
	    swap4((uint32_t)(value >> 32));
}

static uint32_t
decode4(const unsigned char *p, int big_endian)
{
	if (big_endian)
		return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
		    (uint32_t)p[2] << 8 | p[3];
	return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[1] << 8 | p[0];
}

static void
swap_entry(struct magic *m)
{
	m->flag = swap2(m->flag);
	m->offset = (int32_t)swap4((uint32_t)m->offset);
	m->in_offset = (int32_t)swap4((uint32_t)m->in_offset);
	m->lineno = swap4(m->lineno);
	if (IS_STRING(m->type)) {
		m->str_range = swap4(m->str_range);
		m->str_flags = swap4(m->str_flags);
	} else {
		m->value.q = swap8(m->value.q);
		m->num_mask = swap8(m->num_mask);
	}
}

static void
fail(const char *name)
{
	fprintf(stderr, "magic-endian: %s: %s\n", name, strerror(errno));
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	unsigned char *data;
	struct magic *records;
	FILE *fp;
	long length;
	size_t nrecords, i;
	uint32_t n0, n1;
	int source_big, target_big;

	if (argc != 4 ||
	    (strcmp(argv[3], "little") != 0 && strcmp(argv[3], "big") != 0)) {
		fprintf(stderr,
		    "usage: magic-endian input.mgc output.mgc little|big\n");
		return EXIT_FAILURE;
	}
	target_big = strcmp(argv[3], "big") == 0;

	fp = fopen(argv[1], "rb");
	if (fp == NULL)
		fail(argv[1]);
	if (fseek(fp, 0, SEEK_END) == -1 || (length = ftell(fp)) < 0)
		fail(argv[1]);
	if (fseek(fp, 0, SEEK_SET) == -1)
		fail(argv[1]);
	if (length < FILE_MAGICSIZE ||
	    (size_t)length % FILE_MAGICSIZE != 0) {
		fprintf(stderr, "magic-endian: %s: invalid database size\n",
		    argv[1]);
		return EXIT_FAILURE;
	}

	data = malloc((size_t)length);
	if (data == NULL)
		fail(argv[1]);
	if (fread(data, 1, (size_t)length, fp) != (size_t)length)
		fail(argv[1]);
	if (fclose(fp) == EOF)
		fail(argv[1]);

	if (data[0] == 0xf1 && data[1] == 0x1e &&
	    data[2] == 0x04 && data[3] == 0x1c)
		source_big = 1;
	else if (data[0] == 0x1c && data[1] == 0x04 &&
	    data[2] == 0x1e && data[3] == 0xf1)
		source_big = 0;
	else {
		fprintf(stderr, "magic-endian: %s: bad magic\n", argv[1]);
		return EXIT_FAILURE;
	}

	nrecords = (size_t)length / FILE_MAGICSIZE;
	n0 = decode4(data + 8, source_big);
	n1 = decode4(data + 12, source_big);
	if ((uint64_t)n0 + n1 + 1 != nrecords ||
	    decode4(data + 4, source_big) != VERSIONNO) {
		fprintf(stderr, "magic-endian: %s: invalid header\n", argv[1]);
		return EXIT_FAILURE;
	}

	if (source_big != target_big) {
		uint32_t *header = (uint32_t *)(void *)data;

		for (i = 0; i < 2 + MAGIC_SETS; i++)
			header[i] = swap4(header[i]);
		records = (struct magic *)(void *)data;
		for (i = 1; i < nrecords; i++)
			swap_entry(&records[i]);
	}

	fp = fopen(argv[2], "wb");
	if (fp == NULL)
		fail(argv[2]);
	if (fwrite(data, 1, (size_t)length, fp) != (size_t)length ||
	    fclose(fp) == EOF)
		fail(argv[2]);
	free(data);
	return EXIT_SUCCESS;
}
