/*
 * size
 */
#ifdef CROSS
#include </usr/include/stdio.h>
#else
#include <stdio.h>
#endif
#include <a.out.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "aoutio.h"
#include "elf32_mips.h"

int header;

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

static int
elf_read_ehdr(FILE *f, Elf32_Ehdr *eh, int *le)
{
    unsigned char b[52];

    rewind(f);
    if (fread(b, 1, sizeof(b), f) != sizeof(b))
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
elf_read_shdr(FILE *f, const Elf32_Ehdr *eh, int le, int idx, Elf32_Shdr *sh)
{
    unsigned char b[40];

    if (eh->e_shentsize < sizeof(b))
        return 0;
    if (fseek(f, eh->e_shoff + idx * eh->e_shentsize, SEEK_SET) != 0)
        return 0;
    if (fread(b, 1, sizeof(b), f) != sizeof(b))
        return 0;
    sh->sh_name = elf_get32(b + 0, le);
    sh->sh_type = elf_get32(b + 4, le);
    sh->sh_flags = elf_get32(b + 8, le);
    sh->sh_addr = elf_get32(b + 12, le);
    sh->sh_offset = elf_get32(b + 16, le);
    sh->sh_size = elf_get32(b + 20, le);
    sh->sh_link = elf_get32(b + 24, le);
    sh->sh_info = elf_get32(b + 28, le);
    sh->sh_addralign = elf_get32(b + 32, le);
    sh->sh_entsize = elf_get32(b + 36, le);
    return 1;
}

static int
elf_read_phdr(FILE *f, const Elf32_Ehdr *eh, int le, int idx, Elf32_Phdr *ph)
{
    unsigned char b[32];

    if (eh->e_phentsize < sizeof(b))
        return 0;
    if (fseek(f, eh->e_phoff + idx * eh->e_phentsize, SEEK_SET) != 0)
        return 0;
    if (fread(b, 1, sizeof(b), f) != sizeof(b))
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
elf_size(FILE *f, unsigned *text, unsigned *data, unsigned *bss)
{
    Elf32_Ehdr eh;
    int le, i, saw_sections;

    if (!elf_read_ehdr(f, &eh, &le))
        return 0;
    *text = *data = *bss = 0;
    saw_sections = 0;
    if (eh.e_shoff && eh.e_shnum) {
        for (i = 0; i < eh.e_shnum; i++) {
            Elf32_Shdr sh;

            if (!elf_read_shdr(f, &eh, le, i, &sh))
                return 0;
            if ((sh.sh_flags & SHF_ALLOC) == 0 || sh.sh_size == 0)
                continue;
            saw_sections = 1;
            if (sh.sh_type == SHT_NOBITS)
                *bss += sh.sh_size;
            else if (sh.sh_flags & SHF_WRITE)
                *data += sh.sh_size;
            else
                *text += sh.sh_size;
        }
        return saw_sections;
    }
    for (i = 0; i < eh.e_phnum; i++) {
        Elf32_Phdr ph;

        if (!elf_read_phdr(f, &eh, le, i, &ph))
            return 0;
        if (ph.p_type != PT_LOAD)
            continue;
        if ((ph.p_flags & PF_W) == 0)
            *text += ph.p_filesz;
        else
            *data += ph.p_filesz;
        if (ph.p_memsz > ph.p_filesz)
            *bss += ph.p_memsz - ph.p_filesz;
        saw_sections = 1;
    }
    return saw_sections;
}

int main(int argc, char **argv)
{
    struct exec buf;
    long sum;
    unsigned text, data, bss;
    int nfiles, ch, err = 0;
    FILE *f;

#ifdef TARGET_BIG_ENDIAN
    aout_set_big_endian(1);
#else
    aout_set_big_endian(0);
#endif

    while ((ch = getopt(argc, argv, "hE:")) != EOF) {
        switch (ch) {
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
usage:
            fprintf(stderr, "Usage:\n");
            fprintf(stderr, "  size [-EL|-EB] file...\n");
            return (1);
        }
    }
    argc -= optind;
    argv += optind;

    if (argc == 0) {
        *argv = "a.out";
        argc++;
    }

    for (nfiles = argc; argc--; argv++) {
        if ((f = fopen(*argv, "r")) == NULL) {
            printf("size: %s not found\n", *argv);
            err++;
            continue;
        }
        if (elf_size(f, &text, &data, &bss)) {
            /* done */
        } else {
            rewind(f);
            if (!aout_read_exec(f, &buf) || N_BADMAG(buf)) {
                printf("size: %s not an object file\n", *argv);
                fclose(f);
                err++;
                continue;
            }
            text = buf.a_text;
            data = buf.a_data;
            bss = buf.a_bss;
        }
        if (header == 0) {
            printf("text\tdata\tbss\tdec\thex\n");
            header = 1;
        }
        printf("%u\t%u\t%u\t", text, data, bss);
        sum = (long)text + (long)data + (long)bss;
        printf("%ld\t%lx", sum, sum);
        if (nfiles > 1)
            printf("\t%s", *argv);
        printf("\n");
        fclose(f);
    }
    exit(err);
}
