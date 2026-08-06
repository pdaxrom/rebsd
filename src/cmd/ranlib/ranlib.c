/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Hugh Smith at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifdef CROSS
#   include <stdint.h>
#   include <sys/types.h>
#   include <sys/select.h>
#   include <sys/stat.h>
#   include <sys/time.h>
#   include <time.h>
#   include <sys/fcntl.h>
#   include <sys/signal.h>
#   include <stdio.h>
#   include <string.h>
#   include <stdlib.h>
#   include <unistd.h>
#   include <errno.h>
#   define _PATH_RANTMP "/tmp/ranlib.XXXXXX"
#else
#   include <sys/types.h>
#   include <sys/dir.h>
#   include <sys/file.h>
#   include <sys/stat.h>
#   include <sys/param.h>
#   include <stdio.h>
#   include <errno.h>
#   include <stdlib.h>
#   include <string.h>
#   include <strings.h>
#   include <fcntl.h>
#   include <unistd.h>
#   include <signal.h>
#   include <time.h>
#   include <paths.h>
#endif
#include <ar.h>
#include <ranlib.h>
#include <a.out.h>
#include "../aoutio.h"
#include <elf32.h>

#ifdef CROSS
#   include "../ar/archive.h"
#else
#   include "archive.h"
#endif

u_int options;				/* UNUSED -- keep open_archive happy */

CHDR chdr;                              /* converted header */
char *archive;                          /* archive name */
char *tname = "temporary file";		/* temporary file "name" */

typedef struct _rlib {
	struct _rlib *next;		/* next structure */
	off_t pos;			/* offset of defining archive file */
	char *sym;			/* symbol */
	int symlen;			/* strlen(sym) */
} RLIB;
RLIB *rhead, **pnext;

FILE *fp;
long symcnt;				/* symbol count */
long tsymlen;				/* total string length */
int verbose;

void error(
	char *name)
{
	(void)fprintf(stderr, "ranlib: %s: %s\n", name, strerror(errno));
	exit(1);
}

void *emalloc(
	int len)
{
	char *p;

	p = malloc((u_int)len);
	if (! p)
		error(archive);
	return((void *)p);
}

int sgets(
	char *buf,
	int n,
	register FILE *fp)
{
	register int i, c;

	n--;				/* room for null */
	for (i = 0; i < n; i++) {
		c = getc(fp);
		if (c == EOF || c == 0)
			break;
		*buf++ = c;
	}
	*buf = '\0';
	return(i + 1);
}

unsigned int fgetword (
    register FILE *f)
{
        return aout_get32(f);
}

static unsigned
elf_get16(const unsigned char *p, int big)
{
	if (big)
		return ((unsigned)p[0] << 8) | p[1];
	return p[0] | ((unsigned)p[1] << 8);
}

static unsigned
elf_get32(const unsigned char *p, int big)
{
	if (big)
		return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
		    ((unsigned)p[2] << 8) | p[3];
	return p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
	    ((unsigned)p[3] << 24);
}

static int
elf_range_ok(unsigned size, unsigned off, unsigned len)
{
	return off <= size && len <= size - off;
}

static void
ranlib_add(const char *name, int len, off_t member_off)
{
	RLIB *rp;

	if (len <= 0)
		return;
	if (len > 255) {
		errno = EINVAL;
		error(archive);
	}
	rp = (RLIB *)emalloc(sizeof(RLIB));
	rp->next = NULL;
	rp->pos = member_off;
	rp->symlen = len;
	rp->sym = (char *)emalloc(len + 1);
	bcopy(name, rp->sym, len);
	rp->sym[len] = 0;
	tsymlen += len;
	*pnext = rp;
	pnext = &rp->next;
	++symcnt;
}

/*
 * Add global and weak definitions from an ELF32 archive member.
 * Return zero for a non-ELF member and one for a recognized ELF member.
 */
static int
relf(int rfd, off_t r_off, off_t w_off)
{
	unsigned char *data;
	unsigned size, machine, shoff, shnum, shentsize;
	unsigned i;
	int big;
	ssize_t got;
	unsigned done;

	size = chdr.size;
	if (size < sizeof(Elf32_Ehdr))
		return 0;
	data = emalloc(size);
	if (lseek(rfd, r_off, SEEK_SET) == (off_t)-1)
		error(archive);
	for (done = 0; done < size; done += got) {
		got = read(rfd, data + done, size - done);
		if (got <= 0) {
			free(data);
			if (got == 0)
				errno = EINVAL;
			error(archive);
		}
	}
	if (data[0] != ELFMAG0 || data[1] != ELFMAG1 ||
	    data[2] != ELFMAG2 || data[3] != ELFMAG3) {
		free(data);
		return 0;
	}
	if (data[4] != ELFCLASS32 ||
	    (data[EI_DATA] != ELFDATA2LSB && data[EI_DATA] != ELFDATA2MSB)) {
		free(data);
		errno = EINVAL;
		error(archive);
	}
	big = data[EI_DATA] == ELFDATA2MSB;
	machine = elf_get16(data + 18, big);
	if (elf_get16(data + 16, big) != ET_REL ||
	    (machine != EM_MIPS && machine != EM_386)) {
		free(data);
		errno = EINVAL;
		error(archive);
	}
	shoff = elf_get32(data + 32, big);
	shentsize = elf_get16(data + 46, big);
	shnum = elf_get16(data + 48, big);
	if (shentsize < sizeof(Elf32_Shdr) ||
	    !elf_range_ok(size, shoff, shnum * shentsize)) {
		free(data);
		errno = EINVAL;
		error(archive);
	}
	for (i = 0; i < shnum; i++) {
		unsigned char *sh = data + shoff + i * shentsize;
		unsigned symoff, symsize, symentsize, link;
		unsigned stroff, strsize, nsym, j;

		if (elf_get32(sh + 4, big) != SHT_SYMTAB)
			continue;
		symoff = elf_get32(sh + 16, big);
		symsize = elf_get32(sh + 20, big);
		link = elf_get32(sh + 24, big);
		symentsize = elf_get32(sh + 36, big);
		if (symentsize < sizeof(Elf32_Sym) || link >= shnum ||
		    !elf_range_ok(size, symoff, symsize))
			continue;
		sh = data + shoff + link * shentsize;
		if (elf_get32(sh + 4, big) != SHT_STRTAB)
			continue;
		stroff = elf_get32(sh + 16, big);
		strsize = elf_get32(sh + 20, big);
		if (!elf_range_ok(size, stroff, strsize))
			continue;
		nsym = symsize / symentsize;
		for (j = 1; j < nsym; j++) {
			unsigned char *sym = data + symoff + j * symentsize;
			unsigned name = elf_get32(sym, big);
			unsigned shndx = elf_get16(sym + 14, big);
			unsigned bind = ELF_ST_BIND(sym[12]);
			const char *sname, *end;

			if ((bind != STB_GLOBAL && bind != STB_WEAK) ||
			    shndx == SHN_UNDEF || name == 0 || name >= strsize)
				continue;
			sname = (const char *)data + stroff + name;
			end = memchr(sname, 0, strsize - name);
			if (!end)
				continue;
			ranlib_add(sname, end - sname, w_off);
		}
	}
	free(data);
	return 1;
}

/*
 * Read a symbol table entry.
 * Return a number of bytes read, 0 on end of table or -1 on EOF.
 * Format of symbol record:
 *  1 byte: length of name in bytes
 *  1 byte: type of symbol (N_UNDF, N_ABS, N_TEXT, etc)
 *  4 bytes: value
 *  N bytes: name
 */
int fgetsym (
        register FILE *fi,
        register char *name,
        unsigned *value,
        unsigned short *type)
{
        register int len;
        unsigned nbytes;

        len = getc (fi);
        if (len < 0) {
                return -1;
        }
        if (len == 0) {
                return 0;
        }
        *type = getc (fi);
        *value = fgetword (fi);
        nbytes = len + 6;
        if (name) {
                while (len-- > 0)
                        *name++ = getc (fi);
                *name = '\0';
        } else
                fseek (fi, len, SEEK_CUR);
        return nbytes;
}

/*
 * Read the exec structure; ignore any files that don't look exactly right.
 */
void rexec(
	int rfd,
	int wfd)
{
	long nsyms;
	register int nr, symlen;
	struct exec ebuf;
	off_t r_off, w_off;

	/* Get current offsets for original and tmp files. */
	r_off = lseek(rfd, (off_t)0, SEEK_CUR);
	w_off = lseek(wfd, (off_t)0, SEEK_CUR);
	if (relf(rfd, r_off, w_off))
		goto bad1;

	/* Read in exec structure. */
	nr = aout_read_exec_fd(rfd, &ebuf);
	if (! nr)
		goto bad1;

	/* Check magic number and symbol count. */
	if (N_BADMAG(ebuf) || ebuf.a_syms == 0)
		goto bad1;

	/* Seek to symbol table. */
	if (fseek(fp, (off_t)N_SYMOFF(ebuf) + r_off, SEEK_SET) == (off_t)-1)
		goto bad1;

	/* For each symbol read the nlist entry and save it as necessary. */
	nsyms = ebuf.a_syms;
	while (nsyms > 4) {
                unsigned value;
                unsigned short type;
	        char name [256];

                int c = fgetsym(fp, name, &value, &type);
                if (c < 0) {
			if (feof(fp)) {
				/* Bad file format. */
                                errno = EINVAL;
                        }
			error(archive);
                }
                nsyms -= c;
		symlen = c - 6;

		/* Ignore if no name or local. */
		if (symlen <= 0 || ! (type & N_EXT))
			continue;

		/*
		 * If the symbol is an undefined external and the n_value
		 * field is non-zero, keep it.
		 */
		if ((type & N_TYPE) == N_UNDF && ! value)
			continue;

		ranlib_add(name, symlen, w_off);
	}
bad1:	(void)lseek(rfd, (off_t)r_off, SEEK_SET);
}

/*
 * symobj --
 *	Write the symbol table into the archive, computing offsets as
 *	writing.
 */
void symobj(void)
{
	register RLIB *rp;
	char hb[sizeof(struct ar_hdr) + 1 + 64];
	long ransize, baseoff;

	/* Rewind the archive, leaving the magic number. */
	if (fseek(fp, (off_t)SARMAG, SEEK_SET) == (off_t)-1)
		error(archive);

        /* Compute the size of rantab. */
        ransize = 0;
	for (rp = rhead; rp; rp = rp->next) {
	        ransize += rp->symlen + 5;
        }
        ransize += 4 - (ransize & 3);

	/* Put out the ranlib archive file header. */
	(void)sprintf(hb, HDR2, RANLIBMAG, (long long)0, getuid(), getgid(),
	    0666 & ~umask(0), (long long)ransize, ARFMAG);
	if (! fwrite(hb, sizeof(struct ar_hdr), 1, fp))
		error(tname);

	/* Offset of the first archive file. */
	baseoff = SARMAG + sizeof(struct ar_hdr) + ransize;

	/*
	 * Write out the ranlib structures.  The offset into the archive
         * is the base value plus the offset to the first archive file.
	 */
	for (rp = rhead; rp; rp = rp->next) {
                /* Write struct ranlib to file.
                 * 1 byte - length of name.
                 * 4 bytes - seek in archive.
                 * 'len' bytes - symbol name. */
	        unsigned offset = baseoff + rp->pos;
	        fputc (rp->symlen, fp);
		aout_put32(offset, fp);
		if (! fwrite(rp->sym, rp->symlen, 1, fp))
			error(tname);
                if (verbose)
                        fprintf (stderr, "%8u %s\n", offset, rp->sym);
	        ransize -= rp->symlen + 5;
	}

        /* Align to word boundary. */
	while (ransize-- > 0)
                fputc (0, fp);

	(void)fflush(fp);
}

void settime(
	int afd)
{
	struct ar_hdr *hdr;
	off_t size;
	char buf[50];

	size = SARMAG + sizeof(hdr->ar_name);
	if (lseek(afd, size, SEEK_SET) == (off_t)-1)
		error(archive);
	(void)sprintf(buf, "%-12lld",
	    (long long)(time(NULL) + RANLIBSKEW));
	if (write(afd, buf, sizeof(hdr->ar_date)) != sizeof(hdr->ar_date))
		error(archive);
}

int tmp(void)
{
#ifndef CROSS
	long set, oset;
#endif
	int fd;
	char path[MAXPATHLEN];

	bcopy(_PATH_RANTMP, path, sizeof(_PATH_RANTMP));

#ifndef CROSS
	set = sigmask(SIGHUP) | sigmask(SIGINT) |
             sigmask(SIGQUIT) | sigmask(SIGTERM);
	oset = sigsetmask(set);
#endif
	fd = mkstemp(path);
	if (fd < 0)
		error(tname);
        (void)unlink(path);
#ifndef CROSS
	(void)sigsetmask(oset);
#endif
	return(fd);
}

int build(void)
{
	CF cf;
	int afd, tfd;
	off_t size;

	afd = open_archive(O_RDWR);
	fp = fdopen(afd, "r+");
	tfd = tmp();

	SETCF(afd, archive, tfd, tname, RPAD|WPAD);

	/* Read through the archive, creating list of symbols. */
	pnext = &rhead;
	symcnt = tsymlen = 0;
	while(get_arobj(afd)) {
		if (strcmp(chdr.name, RANLIBMAG) == 0) {
			skip_arobj(afd);
			continue;
		}
		rexec(afd, tfd);
		put_arobj(&cf, (struct stat *)NULL);
	}
	*pnext = NULL;

	/* Create the symbol table. */
	symobj();

	/* Copy the saved objects into the archive. */
	size = lseek(tfd, (off_t)0, SEEK_CUR);
	(void)lseek(tfd, (off_t)0, SEEK_SET);
	SETCF(tfd, tname, afd, archive, RPAD|WPAD);
	copy_ar(&cf, size);
	if (ftruncate(afd, lseek(afd, (off_t)0, SEEK_CUR)) < 0)
	        /* ignore */;
	(void)close(tfd);

	/* Set the time. */
	settime(afd);
	close_archive(afd);
	return(0);
}

int touch()
{
	int afd;

	afd = open_archive(O_RDWR);

	if (!get_arobj(afd) ||
	    strncmp(RANLIBMAG, chdr.name, sizeof(RANLIBMAG) - 1)) {
		(void)fprintf(stderr,
		    "ranlib: %s: no symbol table.\n", archive);
		return(1);
	}
	settime(afd);
	return(0);
}

void usage()
{
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  ranlib [-t] file...\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  -t      Update the timestamp of the symbol map\n");
        fprintf(stderr, "  -v      Verbose: print the resulting symbol map\n");
	exit(1);
}

int main(
	int argc,
	char **argv)
{
	int ch, eval, tflag;

#ifdef TARGET_BIG_ENDIAN
	aout_set_big_endian(1);
#else
	aout_set_big_endian(0);
#endif

	tflag = 0;
	while ((ch = getopt(argc, argv, "tv")) != EOF)
		switch(ch) {
		case 't':
			tflag = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (! *argv)
		usage();

	for (eval = 0; (archive = *argv++);)
		eval |= tflag ? touch() : build();
        return eval;
}

/*
 * For archive.c.
 */
char *rname(
	char *path)
{
	register char *ind;

	ind = rindex(path, '/');
	if (! ind)
	        return path;
	return ind + 1;
}

void badfmt(void)
{
	errno = EINVAL;
	error(archive);
}
