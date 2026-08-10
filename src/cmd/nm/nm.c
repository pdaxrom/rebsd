/*
 * nm - print name list. string table version
 */
#ifdef CROSS
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#else
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/dir.h>
#include <sys/file.h>
#include <sys/types.h>
#endif
#include <nlist.h>
#include <ar.h>
#include <elf32.h>

#ifdef CROSS
#include "../ar/archive.h"
#else
#include "archive.h"
#endif

CHDR chdr;

char gflg, nflg, oflg, pflg, uflg, rflg = 1, archive;
char **xargv;

char mag_armag[SARMAG + 1];

int narg, errs;

void error(int n, char *s)
{
    fprintf(stderr, "nm: %s:", *xargv);
    if (archive) {
        fprintf(stderr, "(%s)", chdr.name);
        fprintf(stderr, ": ");
    } else
        fprintf(stderr, " ");
    fprintf(stderr, "%s\n", s);
    if (n)
        exit(2);
    errs = 1;
}

/*
 * "borrowed" from 'ar' because we didn't want to drag in everything else
 * from 'ar'.  The error checking was also ripped out, basically if any
 * of the criteria for being an archive are not met then a -1 is returned
 * and the rest of 'ld' figures out what to do.
 */

/*
 * read the archive header for this member.  Use a file pointer
 * rather than a file descriptor.
 */
int get_ar_hdr(FILE *fp)
{
    struct ar_hdr *hdr;
    register int len, nr;
    register char *p;
    char buf[20];
    static char hb[sizeof(struct ar_hdr) + 1]; /* real header */

    nr = fread(hb, 1, sizeof(struct ar_hdr), fp);
    if (nr != sizeof(struct ar_hdr))
        return (-1);

    hdr = (struct ar_hdr *)hb;
    if (strncmp(hdr->ar_fmag, ARFMAG, sizeof(ARFMAG) - 1))
        return (-1);

/* Convert ar header field to an integer. */
#define AR_ATOI(from, to, len, base)            \
    {                                           \
        bcopy(from, buf, len);                  \
        buf[len] = '\0';                        \
        to = strtoul(buf, (char **)NULL, base); \
    }

    /* Convert the header into the internal format. */
    AR_ATOI(hdr->ar_date, chdr.date, sizeof(hdr->ar_date), 10);
    AR_ATOI(hdr->ar_uid, chdr.uid, sizeof(hdr->ar_uid), 10);
    AR_ATOI(hdr->ar_gid, chdr.gid, sizeof(hdr->ar_gid), 10);
    AR_ATOI(hdr->ar_mode, chdr.mode, sizeof(hdr->ar_mode), 8);
    AR_ATOI(hdr->ar_size, chdr.size, sizeof(hdr->ar_size), 10);

    /* Leading spaces should never happen. */
    if (hdr->ar_name[0] == ' ')
        return (-1);

    /*
     * Long name support.  Set the "real" size of the file, and the
     * long name flag/size.
     */
    if (!bcmp(hdr->ar_name, AR_EFMT1, sizeof(AR_EFMT1) - 1)) {
        chdr.lname = len = atoi(hdr->ar_name + sizeof(AR_EFMT1) - 1);
        if (len <= 0 || len > MAXNAMLEN)
            return (-1);
        nr = fread(chdr.name, 1, (size_t)len, fp);
        if (nr != len)
            return (-1);
        chdr.name[len] = 0;
        chdr.size -= len;
    } else {
        chdr.lname = 0;
        bcopy(hdr->ar_name, chdr.name, sizeof(hdr->ar_name));

        /* Strip trailing spaces, null terminate. */
        for (p = chdr.name + sizeof(hdr->ar_name) - 1; *p == ' '; --p)
            ;
        *++p = '\0';
    }
    return (1);
}

off_t nextel(FILE *af, off_t off)
{
    fseek(af, off, SEEK_SET);
    if (get_ar_hdr(af) < 0)
        return 0;
    off += sizeof(struct ar_hdr) + chdr.size + (chdr.lname & 1);
    return off;
}

int compare(const void *, const void *);
void psyms(struct nlist *, int);

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
elf_read_ehdr_at(FILE *f, off_t base, Elf32_Ehdr *eh, int *le)
{
    unsigned char b[52];

    if (fseek(f, base, SEEK_SET) != 0)
        return 0;
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
    return (eh->e_machine == EM_MIPS || eh->e_machine == EM_386) &&
        eh->e_version == EV_CURRENT;
}

static int
elf_read_shdr_at(FILE *f, off_t base, const Elf32_Ehdr *eh, int le,
    int idx, Elf32_Shdr *sh)
{
    unsigned char b[40];

    if (eh->e_shentsize < sizeof(b))
        return 0;
    if (fseek(f, base + eh->e_shoff + idx * eh->e_shentsize, SEEK_SET) != 0)
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
elf_read_sym_at(FILE *f, off_t base, const Elf32_Shdr *symtab, int le,
    int idx, Elf32_Sym *sym)
{
    unsigned char b[16];
    unsigned entsize;

    entsize = symtab->sh_entsize ? symtab->sh_entsize : sizeof(b);
    if (entsize < sizeof(b))
        return 0;
    if (fseek(f, base + symtab->sh_offset + idx * entsize, SEEK_SET) != 0)
        return 0;
    if (fread(b, 1, sizeof(b), f) != sizeof(b))
        return 0;
    sym->st_name = elf_get32(b + 0, le);
    sym->st_value = elf_get32(b + 4, le);
    sym->st_size = elf_get32(b + 8, le);
    sym->st_info = b[12];
    sym->st_other = b[13];
    sym->st_shndx = elf_get16(b + 14, le);
    return 1;
}

static char *
elf_read_string(FILE *f, off_t base, const Elf32_Shdr *strtab, unsigned off)
{
    char *s;
    int c;
    unsigned len;

    if (off >= strtab->sh_size)
        return NULL;
    if (fseek(f, base + strtab->sh_offset + off, SEEK_SET) != 0)
        return NULL;
    len = 0;
    while (off + len < strtab->sh_size) {
        c = getc(f);
        if (c == EOF)
            return NULL;
        if (c == 0)
            break;
        len++;
    }
    if (off + len >= strtab->sh_size)
        return NULL;
    s = malloc(len + 1);
    if (!s)
        error(1, "out of memory");
    if (fseek(f, base + strtab->sh_offset + off, SEEK_SET) != 0) {
        free(s);
        return NULL;
    }
    if (fread(s, 1, len, f) != len) {
        free(s);
        return NULL;
    }
    s[len] = 0;
    return s;
}

static unsigned short
elf_sym_type(const Elf32_Sym *sym, const Elf32_Shdr *shdrs, int shnum)
{
    unsigned short t, bind;
    const Elf32_Shdr *sh;

    bind = ELF_ST_BIND(sym->st_info);
    if (sym->st_shndx == SHN_UNDEF)
        t = N_UNDF;
    else if (sym->st_shndx == SHN_ABS)
        t = N_ABS;
    else if (sym->st_shndx == SHN_COMMON)
        t = N_COMM;
    else if (sym->st_shndx < shnum) {
        sh = &shdrs[sym->st_shndx];
        if (sh->sh_type == SHT_NOBITS)
            t = N_BSS;
        else if (sh->sh_flags & SHF_EXECINSTR)
            t = N_TEXT;
        else if (sh->sh_flags & SHF_WRITE)
            t = N_DATA;
        else
            t = N_TEXT;
    } else
        t = N_ABS;
    if (bind == STB_GLOBAL)
        t |= N_EXT;
    else if (bind == STB_WEAK)
        t |= N_EXT | N_WEAK;
    return t;
}

static int
elf_namelist(FILE *fi, off_t base)
{
    Elf32_Ehdr eh;
    Elf32_Shdr *shdrs, symtab, strtab;
    struct nlist *symp;
    int le, i, n, nsyms, symtab_idx;

    if (!elf_read_ehdr_at(fi, base, &eh, &le))
        return 0;
    if (eh.e_shoff == 0 || eh.e_shnum == 0) {
        error(0, "no name list");
        return 1;
    }
    shdrs = malloc(eh.e_shnum * sizeof(*shdrs));
    if (!shdrs)
        error(1, "out of memory");
    for (i = 0; i < eh.e_shnum; i++) {
        if (!elf_read_shdr_at(fi, base, &eh, le, i, &shdrs[i])) {
            free(shdrs);
            error(0, "bad format");
            return 1;
        }
    }
    symtab_idx = -1;
    for (i = 0; i < eh.e_shnum; i++) {
        if (shdrs[i].sh_type == SHT_SYMTAB) {
            symtab_idx = i;
            break;
        }
    }
    if (symtab_idx < 0) {
        free(shdrs);
        error(0, "no name list");
        return 1;
    }
    symtab = shdrs[symtab_idx];
    if (symtab.sh_link >= eh.e_shnum ||
        shdrs[symtab.sh_link].sh_type != SHT_STRTAB) {
        free(shdrs);
        error(0, "bad format");
        return 1;
    }
    strtab = shdrs[symtab.sh_link];
    nsyms = symtab.sh_size / (symtab.sh_entsize ? symtab.sh_entsize : 16);
    if (nsyms == 0) {
        free(shdrs);
        error(0, "no name list");
        return 1;
    }
    symp = malloc(nsyms * sizeof(*symp));
    if (!symp)
        error(1, "out of memory");
    n = 0;
    for (i = 0; i < nsyms; i++) {
        Elf32_Sym esym;
        unsigned short type, bind, stype;
        char *name;

        if (!elf_read_sym_at(fi, base, &symtab, le, i, &esym))
            break;
        bind = ELF_ST_BIND(esym.st_info);
        stype = ELF_ST_TYPE(esym.st_info);
        if (stype == STT_SECTION || stype == STT_FILE || esym.st_name == 0)
            continue;
        if (gflg && bind != STB_GLOBAL && bind != STB_WEAK)
            continue;
        type = elf_sym_type(&esym, shdrs, eh.e_shnum);
        if (uflg && (type & N_TYPE) == N_UNDF && esym.st_value != 0)
            continue;
        name = elf_read_string(fi, base, &strtab, esym.st_name);
        if (!name)
            continue;
        symp[n].n_name = name;
        symp[n].n_len = strlen(name);
        symp[n].n_type = type;
        symp[n].n_value = esym.st_value;
        n++;
    }
    if (pflg == 0)
        qsort(symp, n, sizeof(struct nlist), compare);
    if ((archive || narg > 1) && oflg == 0)
        printf("\n%s:\n", archive ? chdr.name : *xargv);
    psyms(symp, n);
    for (i = 0; i < n; i++)
        free(symp[i].n_name);
    free(symp);
    free(shdrs);
    return 1;
}

int compare(const void *arg1, const void *arg2)
{
    const struct nlist *p1 = arg1;
    const struct nlist *p2 = arg2;
    if (nflg) {
        if (p1->n_value > p2->n_value)
            return (rflg);
        if (p1->n_value < p2->n_value)
            return (-rflg);
    }
    return (rflg * strcmp(p1->n_name, p2->n_name));
}

void psyms(struct nlist *symp, int nsyms)
{
    register int n, c;

    for (n = 0; n < nsyms; n++) {
        c = symp[n].n_type;
        if (c == N_FN)
            c = 'f';
        else
            switch (c & N_TYPE) {
            case N_UNDF:
                c = 'u';
                if (symp[n].n_value)
                    c = 'c';
                break;
            case N_ABS:
                c = 'a';
                break;
            case N_TEXT:
                c = 't';
                break;
            case N_DATA:
                c = 'd';
                break;
            case N_BSS:
                c = 'b';
                break;
            case N_STRNG:
                c = 's';
                break;
            case N_COMM:
                c = 'c';
                break;
            case N_FN:
                c = 'f';
                break;
            default:
                c = '?';
                break;
            }
        if (uflg && c != 'u')
            continue;
        if (oflg) {
            if (archive)
                printf("%s:", *xargv);
            printf("%s:", archive ? chdr.name : *xargv);
        }
        if (symp[n].n_type & N_WEAK)
            c = 'w';
        if (symp[n].n_type & N_EXT)
            c = toupper(c);
        if (!uflg) {
            if (c == 'u' || c == 'U')
                printf("        ");
            else
                printf("%08x", symp[n].n_value);
            printf(" %c ", c);
        }
        printf("%s\n", symp[n].n_name);
    }
}

void namelist()
{
    off_t off;
    char ibuf[BUFSIZ];
    Elf32_Ehdr eh;
    int ele;
    register FILE *fi;

    archive = 0;
    fi = fopen(*xargv, "r");
    if (fi == NULL) {
        error(0, "cannot open");
        return;
    }
    setbuf(fi, ibuf);

    off = 0;
    if (fread(mag_armag, 1, SARMAG, fi) != SARMAG) {
        error(0, "read error");
        goto out;
    }

    if (strncmp(mag_armag, ARMAG, SARMAG) == 0) {
        archive++;
        off = SARMAG;
    } else {
        rewind(fi);
        if (!elf_read_ehdr_at(fi, 0, &eh, &ele)) {
            error(0, "bad ELF format");
            goto out;
        }
    }
    rewind(fi);

    if (archive) {
        off = nextel(fi, off);
        if (narg > 1)
            printf("\n%s:\n", *xargv);
    }

    do {
        off_t curpos;

        curpos = ftell(fi);
        (void)elf_namelist(fi, curpos);
    } while (archive && (off = nextel(fi, off)) != 0);
out:
    fclose(fi);
}

int main(int argc, char **argv)
{
    if (--argc > 0 && argv[1][0] == '-' && argv[1][1] != 0) {
        argv++;
        while (*++*argv)
            switch (**argv) {
            case 'n':
                nflg++;
                continue;
            case 'g':
                gflg++;
                continue;
            case 'u':
                uflg++;
                continue;
            case 'r':
                rflg = -1;
                continue;
            case 'p':
                pflg++;
                continue;
            case 'o':
                oflg++;
                continue;
            case 'h':
            usage:
                fprintf(stderr, "Usage:\n");
                fprintf(stderr, "  nm [-gunrpo] file...\n");
                fprintf(stderr, "Options:\n");
                fprintf(stderr, "  -g      Display only external symbols\n");
                fprintf(stderr, "  -u      Display only undefined symbols\n");
                fprintf(stderr, "  -n      Sort symbols numerically by address\n");
                fprintf(stderr, "  -r      Reverse the order of the sort\n");
                fprintf(stderr, "  -p      Do not sort the symbols\n");
                fprintf(stderr, "  -o      Precede each symbol by the file name\n");
                return (1);
            default:
                fprintf(stderr, "nm: invalid argument -%c\n", *argv[0]);
                goto usage;
            }
        argc--;
    }
    if (argc == 0) {
        argc = 1;
        argv[1] = "a.out";
    }
    narg = argc;
    xargv = argv;
    while (argc--) {
        ++xargv;
        namelist();
    }
    return (errs);
}
