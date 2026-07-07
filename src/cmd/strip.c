/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#ifdef CROSS
#   include </usr/include/stdio.h>
#else
#   include <stdio.h>
#endif
#include <a.out.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "aoutio.h"
#include "elf32_mips.h"

struct  exec head;
int status;

static int
elf_get16(const unsigned char *p, int le)
{
    if (le)
        return p[0] | (p[1] << 8);
    return (p[0] << 8) | p[1];
}

static unsigned
elf_get32(const unsigned char *p, int le)
{
    if (le)
        return (unsigned)p[0] | ((unsigned)p[1] << 8) |
            ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
        ((unsigned)p[2] << 8) | (unsigned)p[3];
}

static void
elf_put16(unsigned char *p, unsigned v, int le)
{
    if (le) {
        p[0] = v;
        p[1] = v >> 8;
    } else {
        p[0] = v >> 8;
        p[1] = v;
    }
}

static void
elf_put32(unsigned char *p, unsigned v, int le)
{
    if (le) {
        p[0] = v;
        p[1] = v >> 8;
        p[2] = v >> 16;
        p[3] = v >> 24;
    } else {
        p[0] = v >> 24;
        p[1] = v >> 16;
        p[2] = v >> 8;
        p[3] = v;
    }
}

static int
elf_read_ehdr_fd(int fd, Elf32_Ehdr *eh, int *le)
{
    unsigned char b[52];

    if (lseek(fd, (off_t)0, SEEK_SET) < 0)
        return 0;
    if (read(fd, b, sizeof(b)) != sizeof(b))
        return 0;
    if (b[0] != ELFMAG0 || b[1] != ELFMAG1 ||
        b[2] != ELFMAG2 || b[3] != ELFMAG3)
        return 0;
    if (b[4] != ELFCLASS32 ||
        (b[EI_DATA] != ELFDATA2LSB && b[EI_DATA] != ELFDATA2MSB))
        return 0;
    *le = b[EI_DATA] == ELFDATA2LSB;
    memcpy(eh->e_ident, b, sizeof(eh->e_ident));
    eh->e_type = elf_get16(b + 16, *le);
    eh->e_machine = elf_get16(b + 18, *le);
    eh->e_version = elf_get32(b + 20, *le);
    eh->e_entry = elf_get32(b + 24, *le);
    eh->e_phoff = elf_get32(b + 28, *le);
    eh->e_shoff = elf_get32(b + 32, *le);
    eh->e_flags = elf_get32(b + 36, *le);
    eh->e_ehsize = elf_get16(b + 40, *le);
    eh->e_phentsize = elf_get16(b + 42, *le);
    eh->e_phnum = elf_get16(b + 44, *le);
    eh->e_shentsize = elf_get16(b + 46, *le);
    eh->e_shnum = elf_get16(b + 48, *le);
    eh->e_shstrndx = elf_get16(b + 50, *le);
    return eh->e_machine == EM_MIPS && eh->e_version == EV_CURRENT;
}

static int
elf_read_phdr_fd(int fd, const Elf32_Ehdr *eh, int le, int idx, Elf32_Phdr *ph)
{
    unsigned char b[32];

    if (eh->e_phentsize < sizeof(b))
        return 0;
    if (lseek(fd, eh->e_phoff + idx * eh->e_phentsize, SEEK_SET) < 0)
        return 0;
    if (read(fd, b, sizeof(b)) != sizeof(b))
        return 0;
    ph->p_type = elf_get32(b + 0, le);
    ph->p_offset = elf_get32(b + 4, le);
    ph->p_vaddr = elf_get32(b + 8, le);
    ph->p_paddr = elf_get32(b + 12, le);
    ph->p_filesz = elf_get32(b + 16, le);
    ph->p_memsz = elf_get32(b + 20, le);
    ph->p_flags = elf_get32(b + 24, le);
    ph->p_align = elf_get32(b + 28, le);
    return 1;
}

static int
elf_write_ehdr_fd(int fd, const Elf32_Ehdr *eh, int le)
{
    unsigned char b[52];

    memcpy(b, eh->e_ident, sizeof(eh->e_ident));
    elf_put16(b + 16, eh->e_type, le);
    elf_put16(b + 18, eh->e_machine, le);
    elf_put32(b + 20, eh->e_version, le);
    elf_put32(b + 24, eh->e_entry, le);
    elf_put32(b + 28, eh->e_phoff, le);
    elf_put32(b + 32, eh->e_shoff, le);
    elf_put32(b + 36, eh->e_flags, le);
    elf_put16(b + 40, eh->e_ehsize, le);
    elf_put16(b + 42, eh->e_phentsize, le);
    elf_put16(b + 44, eh->e_phnum, le);
    elf_put16(b + 46, eh->e_shentsize, le);
    elf_put16(b + 48, eh->e_shnum, le);
    elf_put16(b + 50, eh->e_shstrndx, le);
    if (lseek(fd, (off_t)0, SEEK_SET) < 0)
        return 0;
    return write(fd, b, sizeof(b)) == sizeof(b);
}

static int
strip_elf(char *name, int fd, Elf32_Ehdr *eh, int le)
{
    Elf32_Phdr ph;
    unsigned keep, end;
    int i;

    if (eh->e_type != ET_EXEC)
        return 1;
    keep = eh->e_ehsize ? eh->e_ehsize : 52;
    end = eh->e_phoff + eh->e_phnum * eh->e_phentsize;
    if (end > keep)
        keep = end;
    for (i = 0; i < eh->e_phnum; i++) {
        if (!elf_read_phdr_fd(fd, eh, le, i, &ph)) {
            fprintf(stderr, "strip: %s bad ELF program header\n", name);
            status = 1;
            return 1;
        }
        if (ph.p_type != PT_LOAD)
            continue;
        end = ph.p_offset + ph.p_filesz;
        if (end > keep)
            keep = end;
    }
    eh->e_shoff = 0;
    eh->e_shnum = 0;
    eh->e_shstrndx = 0;
    if (!elf_write_ehdr_fd(fd, eh, le)) {
        fprintf(stderr, "strip: ");
        perror(name);
        status = 1;
        return 1;
    }
    if (ftruncate(fd, keep) < 0) {
        fprintf(stderr, "strip: ");
        perror(name);
        status = 1;
    }
    return 1;
}

void
strip(char *name)
{
    register int f = -1;
    Elf32_Ehdr eh;
    long size;
    int le;

    f = open(name, O_RDWR);
    if (f < 0) {
        fprintf(stderr, "strip: "); perror(name);
        status = 1;
        goto out;
    }
    if (elf_read_ehdr_fd(f, &eh, &le)) {
        strip_elf(name, f, &eh, le);
        goto out;
    }
    (void) lseek(f, (off_t)0, SEEK_SET);
    if (!aout_read_exec_fd(f, &head) || N_BADMAG(head)) {
        printf("strip: %s not in a.out or ELF format\n", name);
        status = 1;
        goto out;
    }
    if (head.a_syms == 0 && (head.a_magic) != RMAGIC)
        goto out;

    size = N_DATOFF(head) + head.a_data;
    if (ftruncate(f, size) < 0) {
        fprintf(stderr, "strip: ");
        perror(name);
        status = 1;
        goto out;
    }
    head.a_midmag = OMAGIC;
    head.a_reltext = 0;
    head.a_reldata = 0;
    head.a_syms = 0;
    (void) lseek(f, (off_t)0, SEEK_SET);
    if (!aout_write_exec_fd(f, &head))
            /* ignore */;
out:
    if (f >= 0)
        close(f);
}

int
main(int argc, char *argv[])
{
    register int i;

#ifdef TARGET_BIG_ENDIAN
    aout_set_big_endian(1);
#else
    aout_set_big_endian(0);
#endif

    while ((i = getopt(argc, argv, "hE:")) != EOF) {
        switch(i) {
        case 'E':
            if (optarg[0] == 'L' && optarg[1] == 0)
                aout_set_big_endian(0);
            else if (optarg[0] == 'B' && optarg[1] == 0)
                aout_set_big_endian(1);
            else
                goto usage;
            break;
        case 'h':
        default:
usage:                  fprintf(stderr, "Usage:\n");
            fprintf(stderr, "  strip [-EL|-EB] file...\n");
            return(1);
        }
    }
    argc -= optind;
    argv += optind;
    if (argc == 0)
        goto usage;

    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    for (i = 0; i < argc; i++) {
        strip(argv[i]);
        if (status > 1)
            break;
    }
    return(status);
}
