/*
 * Linker for RetroBSD a.out and ELF32 targets.
 *
 * Copyright (C) 2011 Serge Vakulenko, <serge@vak.ru>
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that the copyright notice and this
 * permission notice and warranty disclaimer appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * The author disclaim all warranties with regard to this
 * software, including all implied warranties of merchantability
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */
#ifdef CROSS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/select.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#define MAXNAMLEN 63
#else
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#include <a.out.h>
#include <ar.h>
#include <ranlib.h>
#include <stdarg.h>
#include "../aoutio.h"
#include <elf32.h>

#define W 4              /* word size in bytes */
#define ELF_LOAD_ALIGN 0x1000
#if defined(TARGET_VR4300) || defined(TARGET_MIPS_STRICT_ALIGN64)
#define TEXT_ALIGN 8     /* Preserve 8-byte alignment of in-text FPU literals. */
#define DATA_ALIGN 8     /* Preserve 8-byte alignment across linked data input. */
#define BSS_ALIGN 8      /* Preserve 8-byte alignment across linked BSS input. */
#else
#define TEXT_ALIGN W
#define DATA_ALIGN W
#define BSS_ALIGN W
#endif
#define BADDR 0x00400000 /* ReBSD/MIPS user text base */
#define SYMDEF "__.SYMDEF"
#define IS_LOCSYM(s) ((s)->n_name[0] == 'L' || (s)->n_name[0] == '.')
#define REBSD_CTORS_SIZE_SYM ".__rebsd_ctors_size"
#define REBSD_DTORS_SIZE_SYM ".__rebsd_dtors_size"
#define hexdig(c) ((c) <= '9' ? (c) - '0' : ((c) & 7) + 9)

struct exec filhdr; /* aout header */

#define HDRSZ sizeof(struct exec)

struct archdr { /* archive header */
    char *ar_name;
    long ar_date;
    int ar_uid;
    int ar_gid;
    int ar_mode;
    long ar_size;
} archdr;

FILE *text, *reloc; /* input management */

/* output management */
FILE *outb, *toutb, *doutb, *ctoroutb, *dtoroutb;
FILE *troutb, *droutb, *ctorroutb, *dtorroutb, *soutb;

/* symbol management */
struct local {
    unsigned locindex;       /* index to symbol in file */
    struct nlist *locsymbol; /* ptr to symbol table */
};

struct data_sections {
    unsigned normal;
    unsigned ctors;
    unsigned dtors;
};

#define NSYM 8192
#define NSYMPR 4096
#define NLIBS 256
#define NLIBDIRS 64
#define RANTAB_CHUNK 512

struct nlist cursym;            /* current symbol */
struct nlist symtab[NSYM];      /* table of symbols */
struct nlist **symhash[NSYM];   /* pointers to hash table */
struct nlist *lastsym;          /* last entered symbol */
struct nlist *hshtab[NSYM + 2]; /* hash table for symbols */
struct local local[NSYMPR];
int symindex;             /* next free entry of symbol table */
unsigned basaddr = BADDR; /* base address of loading */
struct ranlib *rantab;
int rancount; /* number of elements in rantab */
int rantabsz;

/*
 * library management
 */
unsigned liblist[NLIBS], *libp;
char *libdirs[NLIBDIRS];
int nlibdirs;
char *stdlibdirs[] = { "/lib", "/usr/lib", "/usr/local/lib", 0 };
char *ld_program;

/*
 * internal symbols
 */
struct nlist *p_etext, *p_edata, *p_end;
struct nlist *p_c_etext, *p_c_edata, *p_c_end;
struct nlist *p_gp, *entrypt;
struct nlist *p_ctor_list, *p_ctor_end, *p_dtor_list, *p_dtor_end;

/*
 * options
 */
int trace; /* internal trace flag */
int xflag; /* discard local symbols */
int Xflag; /* discard locals starting with 'L' or '.' */
int Sflag; /* discard all except locals and globals*/
int rflag; /* preserve relocation bits, don't define commons */
int output_relinfo;
int sflag;   /* discard all symbols */
int dflag;   /* define common even with rflag */
int verbose; /* verbose mode */
int final_layout;
char *sysroot = "/"; /* root for standard library search directories */

/*
 * cumulative sizes set in pass 1
 */
unsigned tsize, dsize, ctorsize, dtorsize, bsize, ssize, nsym;
unsigned normal_dsize;

/*
 * symbol relocation; both passes
 */
unsigned ctrel, cdrel, cctorrel, cdtorrel, cbrel;

/*
 * used after pass 1
 */
unsigned torigin, dorigin, ctorigin, dtorigin, borigin;
unsigned data_origin, ctor_origin, dtor_origin;

/* gp control, MIPS specific */
unsigned gpoffset = 0x8000; /* offset from data start */
unsigned gp;                /* allocated address */

int ofilfnd;
char *ofilename = "l.out";
char *filname;
int errlev;
int delarg = 4;
char tfname[] = "/tmp/ldaXXXXXX";

#define ALIGN(x, y) ((x) + (y) - 1 - ((x) + (y) - 1) % (y))

static void aout_handle_t_option(char *arg);

unsigned int fgetword(FILE *f)
{
    return aout_get32(f);
}

void fputword(unsigned h, FILE *f)
{
    aout_put32(h, f);
}

int fgethdr(FILE *text, struct exec *h)
{
    return aout_read_exec(text, h);
}

void fputhdr(struct exec *hdr, FILE *coutb)
{
    aout_write_exec(coutb, hdr);
}

/*
 * Read a relocation record: 1 to 6 bytes.
 */
void fgetrel(FILE *f, struct reloc *r)
{
    r->flags = getc(f);
    if ((r->flags & RSMASK) == REXT) {
        r->index = aout_get24(f);
    }
    if ((r->flags & RFMASK) == RHIGH16 || (r->flags & RFMASK) == RHIGH16S) {
        r->offset = aout_get16(f);
    }
}

/*
 * Emit a relocation record: 1 to 6 bytes.
 * Return a written length.
 */
unsigned fputrel(struct reloc *r, FILE *f)
{
    register unsigned nbytes = 1;

    putc(r->flags, f);
    if ((r->flags & RSMASK) == REXT) {
        aout_put24(r->index, f);
        nbytes += 3;
    }
    if ((r->flags & RFMASK) == RHIGH16 || (r->flags & RFMASK) == RHIGH16S) {
        aout_put16(r->offset, f);
        nbytes += 2;
    }
    return nbytes;
}

void chmod_executable_output(void)
{
    int mask;

    mask = umask(0);
    umask(mask);
    chmod(ofilename, 0777 & ~mask);
}

void delexit(int sig)
{
    unlink("l.out");
    if (!delarg && !rflag)
        chmod_executable_output();
    exit(delarg);
}

void error(int n, const char *s, ...)
{
    va_list ap;

    va_start(ap, s);
    if (!errlev)
        printf("ld: ");
    if (filname)
        printf("%s: ", filname);
    vprintf(s, ap);
    va_end(ap);
    printf("\n");
    if (n > 1)
        delexit(0);
    errlev = n;
}

int fgetsym(FILE *text, struct nlist *sym)
{
    int c;

    c = aout_read_sym(text, sym, 1);
    if (c < 0)
        error(2, "out of memory");
    return c;
}

void fputsym(struct nlist *s, FILE *file)
{
    aout_write_sym(file, s);
}

int is_rebsd_metadata_symbol(struct nlist *sp)
{
    return strcmp(sp->n_name, REBSD_CTORS_SIZE_SYM) == 0 ||
        strcmp(sp->n_name, REBSD_DTORS_SIZE_SYM) == 0;
}

void read_data_sections(unsigned loc, struct data_sections *sec)
{
    int symlen;

    sec->ctors = 0;
    sec->dtors = 0;
    fseek(text, loc + N_SYMOFF(filhdr), 0);
    for (;;) {
        symlen = fgetsym(text, &cursym);
        if (symlen == 0)
            break;
        if (is_rebsd_metadata_symbol(&cursym)) {
            if ((cursym.n_type & N_TYPE) == N_ABS) {
                if (strcmp(cursym.n_name, REBSD_CTORS_SIZE_SYM) == 0)
                    sec->ctors = cursym.n_value;
                else
                    sec->dtors = cursym.n_value;
            }
        }
        free(cursym.n_name);
    }
    if (sec->ctors > filhdr.a_data || sec->dtors > filhdr.a_data - sec->ctors)
        error(2, "bad .ctors/.dtors metadata");
    sec->normal = filhdr.a_data - sec->ctors - sec->dtors;
}

/*
 * Read the file header of the archive.
 */
int fgetarhdr(FILE *fd, struct archdr *h)
{
    struct ar_hdr hdr;
    register int len, nr;
    register char *p;
    char buf[20];

    /* Read arhive name.  Spaces should never happen. */
    nr = fread(buf, 1, sizeof(hdr.ar_name), fd);
    if (nr != sizeof(hdr.ar_name) || buf[0] == ' ')
        return 0;
    buf[nr] = 0;

    /* Long name support.  Set the "real" size of the file,
     * and the long name flag/size. */
    h->ar_size = 0;
    if (strncmp(buf, AR_EFMT1, sizeof(AR_EFMT1) - 1) == 0) {
        len = atoi(buf + sizeof(AR_EFMT1) - 1);
        if (len <= 0 || len > MAXNAMLEN)
            return 0;
        h->ar_name = malloc(len + 1);
        if (!h->ar_name)
            return 0;
        nr = fread(h->ar_name, 1, len, fd);
        if (nr != len) {
        failed:
            free(h->ar_name);
            return 0;
        }
        h->ar_name[len] = 0;
        h->ar_size -= len;
    } else {
        /* Strip trailing spaces, null terminate. */
        p = buf + nr - 1;
        while (*p == ' ')
            --p;
        *++p = '\0';

        len = p - buf;
        h->ar_name = malloc(len + 1);
        if (!h->ar_name)
            return 0;
        strcpy(h->ar_name, buf);
    }

    /* Read arhive date. */
    nr = fread(buf, 1, sizeof(hdr.ar_date), fd);
    if (nr != sizeof(hdr.ar_date))
        goto failed;
    buf[nr] = 0;
    h->ar_date = strtol(buf, 0, 10);

    /* Read user id. */
    nr = fread(buf, 1, sizeof(hdr.ar_uid), fd);
    if (nr != sizeof(hdr.ar_uid))
        goto failed;
    buf[nr] = 0;
    h->ar_uid = strtol(buf, 0, 10);

    /* Read group id. */
    nr = fread(buf, 1, sizeof(hdr.ar_gid), fd);
    if (nr != sizeof(hdr.ar_gid))
        goto failed;
    buf[nr] = 0;
    h->ar_gid = strtol(buf, 0, 10);

    /* Read mode (octal). */
    nr = fread(buf, 1, sizeof(hdr.ar_mode), fd);
    if (nr != sizeof(hdr.ar_mode))
        goto failed;
    buf[nr] = 0;
    h->ar_mode = strtol(buf, 0, 8);

    /* Read archive size. */
    nr = fread(buf, 1, sizeof(hdr.ar_size), fd);
    if (nr != sizeof(hdr.ar_size))
        goto failed;
    buf[nr] = 0;
    h->ar_size = strtol(buf, 0, 10);

    /* Check secondary magic. */
    nr = fread(buf, 1, sizeof(hdr.ar_fmag), fd);
    if (nr != sizeof(hdr.ar_fmag))
        goto failed;
    buf[nr] = 0;
    if (strcmp(buf, ARFMAG) != 0)
        goto failed;

    return 1;
}

void freerantab()
{
    register struct ranlib *p;

    for (p = rantab; p < rantab + rancount; ++p)
        free(p->ran_name);
    free(rantab);
    rantab = 0;
    rancount = 0;
    rantabsz = 0;
}

int fgetran(FILE *text, struct ranlib *sym)
{
    register int c;

    /* read struct ranlib from file */
    /* 1 byte - length of name */
    /* 4 bytes - seek in archive */
    /* 'len' bytes - symbol name */
    /* if len == 0 then eof */
    /* return 1 if ok, 0 on eof */

    sym->ran_len = getc(text);
    if (sym->ran_len <= 0)
        return (0);
    sym->ran_name = malloc(sym->ran_len + 1);
    if (!sym->ran_name)
        error(2, "out of memory");
    sym->ran_off = aout_get32(text);
    for (c = 0; c < sym->ran_len; c++)
        sym->ran_name[c] = getc(text);
    sym->ran_name[sym->ran_len] = '\0';
    return (1);
}

void getrantab()
{
    struct ranlib ent;
    struct ranlib *newtab;
    int newsz;

    rancount = 0;
    for (;;) {
        if (!fgetran(text, &ent)) {
            if (trace > 1)
                printf("ranlib entries=%d\n", rancount);
            return;
        }
        if (rancount >= rantabsz) {
            newsz = rantabsz ? rantabsz * 2 : RANTAB_CHUNK;
            newtab = (struct ranlib *)realloc(rantab,
                newsz * sizeof(struct ranlib));
            if (newtab == 0) {
                free(ent.ran_name);
                error(2, "out of memory");
            }
            rantab = newtab;
            rantabsz = newsz;
        }
        rantab[rancount++] = ent;
    }
}

void ldrsym(struct nlist *sp, unsigned val, int type)
{
    if (sp == 0)
        return;
    if (sp->n_type != N_EXT + N_UNDF) {
        printf("%s: ", sp->n_name);
        error(1, "name redefined");
        return;
    }
    sp->n_type = type;
    sp->n_value = val;
}

void tcreat(FILE **buf, int tempflg)
{
    *buf = fopen(tempflg ? tfname : ofilename, "w+");
    if (!*buf)
        error(2, tempflg ? "cannot create temporary file" : "cannot create output file");
    if (tempflg)
        unlink(tfname);
}

int reltype(int stype)
{
    switch (stype & N_TYPE) {
    case N_UNDF:
        return (0);
    case N_ABS:
        return (RABS);
    case N_TEXT:
        return (RTEXT);
    case N_DATA:
        return (RDATA);
    case N_CTORS:
        return (RCTORS);
    case N_DTORS:
        return (RDTORS);
    case N_BSS:
        return (RBSS);
    case N_STRNG:
        return (RDATA);
    case N_COMM:
        return (RBSS);
    case N_FN:
        return (0);
    default:
        return (0);
    }
}

struct nlist *lookloc(struct local *lp, int sn)
{
    register struct local *clp;

    for (clp = local; clp < lp; clp++)
        if (clp->locindex == sn)
            return (clp->locsymbol);
    if (trace) {
        fprintf(stderr, "*** %d ***\n", sn);
        for (clp = local; clp < lp; clp++)
            fprintf(stderr, "%u, ", clp->locindex);
        fprintf(stderr, "\n");
    }
    error(2, "bad symbol reference");
    return 0;
}

void printrel(unsigned word, struct reloc *rel)
{
    printf("%08x %02x ", word, rel->flags);

    if ((rel->flags & RSMASK) == REXT)
        printf("%-3d ", rel->index);
    else
        printf("    ");

    if ((rel->flags & RFMASK) == RHIGH16 || (rel->flags & RFMASK) == RHIGH16S)
        printf("%08x", rel->offset);
    else
        printf("        ");
}

/*
 * Relocate the word by a given offset.
 * Return the new value of word and update rel.
 */
unsigned relword(struct local *lp, unsigned word, struct reloc *rel, unsigned offset)
{
    register unsigned addr, delta;
    register struct nlist *sp = 0;

    if (trace > 2)
        printrel(word, rel);
    /*
     * Extract an address field from the instruction.
     */
    switch (rel->flags & RFMASK) {
    case RBYTE16:
        addr = word & 0xffff;
        break;
    case RBYTE32:
        addr = word;
        break;
    case RWORD16:
        addr = (word & 0xffff) << 2;
        break;
    case RWORD26:
        addr = (word & 0x3ffffff) << 2;
        break;
    case RHIGH16:
        addr = (word & 0xffff) << 16;
        addr += rel->offset;
        break;
    case RHIGH16S:
        addr = (word & 0xffff) << 16;
        addr += (signed short)rel->offset;
        break;
    default:
        addr = 0;
        break;
    }

    /*
     * Compute a delta for address.
     * Update the relocation info, if needed.
     */
    switch (rel->flags & RSMASK) {
    case RTEXT:
        delta = ctrel;
        break;
    case RDATA:
        delta = cdrel;
        break;
    case RCTORS:
        delta = cctorrel;
        break;
    case RDTORS:
        delta = cdtorrel;
        break;
    case RBSS:
        delta = cbrel;
        break;
    case REXT:
        sp = lookloc(lp, rel->index);
        if (sp->n_type == N_EXT + N_UNDF || sp->n_type == N_EXT + N_COMM) {
            rel->index = nsym + (sp - symtab);
            sp = 0;
            delta = 0;
        } else {
            rel->flags &= RFMASK | RGPREL;
            rel->flags |= reltype(sp->n_type);
            delta = sp->n_value;
        }
        break;
    default:
        delta = 0;
        break;
    }

    if ((rel->flags & RGPREL) && !output_relinfo) {
        /*
         * GP relative address.
         */
        delta -= gp;
        rel->flags &= ~RGPREL;
    }

    /*
     * Update the address field of the instruction.
     * Update the relocation info, if needed.
     */
    switch (rel->flags & RFMASK) {
    case RBYTE16:
        addr += delta;
        word &= ~0xffff;
        word |= addr & 0xffff;
        break;
    case RBYTE32:
        word = addr + delta;
        break;
    case RWORD16:
        if (!sp)
            break;
        addr += delta - offset - 4;
        word &= ~0xffff;
        word |= (addr >> 2) & 0xffff;
        rel->flags = RABS;
        break;
    case RWORD26:
        addr += delta;
        word &= ~0x3ffffff;
        word |= (addr >> 2) & 0x3ffffff;
        break;
    case RHIGH16:
        addr += delta;
        word &= ~0xffff;
        word |= (addr >> 16) & 0xffff;
        break;
    case RHIGH16S:
        addr += delta;
        word &= ~0xffff;
        word |= ((addr + 0x8000) >> 16) & 0xffff;
        break;
    }
    if (trace > 2) {
        // printf (" +%#x ", delta);
        printf(" -> ");
        printrel(word, rel);
        printf("\n");
    }
    return word;
}

void relocate(struct local *lp, FILE *b1, FILE *b2, unsigned len, unsigned origin)
{
    unsigned word, offset;
    struct reloc rel;

    for (offset = 0; offset < len; offset += W) {
        word = fgetword(text);
        fgetrel(reloc, &rel);
        word = relword(lp, word, &rel, offset + origin);
        fputword(word, b1);
        if (output_relinfo)
            fputrel(&rel, b2);
    }
}

void relocate_data_sections(struct local *lp, struct data_sections *sec)
{
    unsigned word, offset, outoff, origin;
    unsigned ctor_start, dtor_start;
    FILE *data, *rdata;
    struct reloc rel;

    ctor_start = sec->normal;
    dtor_start = sec->normal + sec->ctors;
    for (offset = 0; offset < filhdr.a_data; offset += W) {
        word = fgetword(text);
        fgetrel(reloc, &rel);
        if (offset < ctor_start) {
            outoff = offset;
            origin = dorigin;
            data = doutb;
            rdata = droutb;
        } else if (offset < dtor_start) {
            outoff = offset - ctor_start;
            origin = ctorigin;
            data = ctoroutb;
            rdata = ctorroutb;
        } else {
            outoff = offset - dtor_start;
            origin = dtorigin;
            data = dtoroutb;
            rdata = dtorroutb;
        }
        word = relword(lp, word, &rel, outoff + origin);
        fputword(word, data);
        if (output_relinfo)
            fputrel(&rel, rdata);
    }
}

unsigned copy_and_close(FILE *buf)
{
    register int c;
    unsigned nbytes;

    rewind(buf);
    nbytes = 0;
    while ((c = getc(buf)) != EOF) {
        putc(c, outb);
        nbytes++;
    }
    fclose(buf);
    return nbytes;
}

int mkfsym(char *s, int wflag)
{
    register char *p;

    if (sflag || xflag)
        return (0);
    for (p = s; *p;)
        if (*p++ == '/')
            s = p;
    if (!wflag)
        return (p - s + 6);
    cursym.n_len = p - s;
    cursym.n_name = malloc(cursym.n_len + 1);
    if (!cursym.n_name)
        error(2, "out of memory");
    for (p = cursym.n_name; *s; p++, s++)
        *p = *s;
    cursym.n_type = N_FN;
    cursym.n_value = torigin;
    fputsym(&cursym, soutb);
    free(cursym.n_name);
    return (cursym.n_len + 6);
}

char *savestr(const char *s)
{
    char *p;

    p = malloc(strlen(s) + 1);
    if (!p)
        error(2, "out of memory");
    strcpy(p, s);
    return p;
}

void addlibdir(char *dir)
{
    if (nlibdirs >= NLIBDIRS)
        error(2, "too many -L directories");
    libdirs[nlibdirs++] = savestr(dir);
}

int consumelongopt(char *ap, int *cp, int argc, char ***pp)
{
    if (strcmp(ap, "--fatal-warnings") == 0)
        return 1;
    if (strcmp(ap, "--aout") == 0)
        return 1;
    if (strncmp(ap, "--sysroot=", 10) == 0) {
        sysroot = ap + 10;
        return 1;
    }
    if (strcmp(ap, "--sysroot") == 0) {
        if (++*cp >= argc)
            error(2, "--sysroot: argument missing");
        sysroot = **pp;
        (*pp)++;
        return 1;
    }
    return 0;
}

void collectlibdirs(int argc, char **argv)
{
    int c, i;
    char *ap, **p;

    /*
     * Make all -L directories visible before resolving any -l option.
     * This matches the usual linker semantics and lets compiler drivers put
     * default -L options after user objects/libraries.
     */
    p = argv + 1;
    nlibdirs = 0;
    for (c = 1; c < argc; c++) {
        ap = *p++;
        if (*ap != '-')
            continue;
        if (consumelongopt(ap, &c, argc, &p))
            continue;
        for (i = 1; ap[i]; i++) {
            switch (ap[i]) {
            case 'L':
                if (ap[i + 1]) {
                    addlibdir(&ap[i + 1]);
                    while (ap[i + 1])
                        i++;
                } else {
                    if (++c >= argc)
                        error(2, "-L: argument missing");
                    addlibdir(*p++);
                }
                continue;

            case 'l':
                if (ap[i + 1]) {
                    while (ap[i + 1])
                        i++;
                } else {
                    if (++c >= argc)
                        error(2, "-l: argument missing");
                    p++;
                }
                continue;

            case 'o':
            case 'u':
            case 'e':
                if (++c >= argc)
                    error(2, "option argument missing");
                p++;
                continue;

            case 'E':
                while (ap[i + 1])
                    i++;
                continue;

            case 'T':
                if (ap[i + 1]) {
                    while (ap[i + 1])
                        i++;
                } else {
                    if (++c >= argc)
                        error(2, "-%c: argument missing", ap[i]);
                    p++;
                }
                continue;

            case 'G':
                while (ap[i + 1])
                    i++;
                continue;

            default:
                continue;
            }
        }
    }
}

char *makelibpath(const char *dir, const char *name)
{
    char *path;
    int need_slash;
    size_t len;

    need_slash = dir[0] != '\0' && dir[strlen(dir) - 1] != '/';
    len = strlen(dir) + need_slash + 3 + strlen(name) + 2 + 1;
    path = malloc(len);
    if (!path)
        error(2, "out of memory");
    strcpy(path, dir);
    if (need_slash)
        strcat(path, "/");
    strcat(path, "lib");
    strcat(path, name);
    strcat(path, ".a");
    return path;
}

char *makesyslibpath(const char *dir, const char *name)
{
    char *dirpath, *path;
    int need_slash;
    size_t len;

    if (sysroot[0] == '\0' || strcmp(sysroot, "/") == 0)
        return makelibpath(dir, name);

    need_slash = sysroot[strlen(sysroot) - 1] != '/';
    if (dir[0] == '/')
        dir++;
    len = strlen(sysroot) + need_slash + strlen(dir) + 1;
    dirpath = malloc(len);
    if (!dirpath)
        error(2, "out of memory");
    strcpy(dirpath, sysroot);
    if (need_slash)
        strcat(dirpath, "/");
    strcat(dirpath, dir);
    path = makelibpath(dirpath, name);
    free(dirpath);
    return path;
}

int openfile(char *path)
{
    text = fopen(path, "r");
    if (!text)
        return 0;
    reloc = fopen(path, "r");
    if (!reloc) {
        fclose(text);
        text = 0;
        return 0;
    }
    filname = path;
    return 1;
}

int openlib(char *name)
{
    char *path;
    int i;

    if (*name == '\0')
        error(2, "-l: argument missing");
    for (i = 0; i < nlibdirs; i++) {
        path = makelibpath(libdirs[i], name);
        if (openfile(path))
            return 1;
        free(path);
    }
    for (i = 0; stdlibdirs[i]; i++) {
        path = makesyslibpath(stdlibdirs[i], name);
        if (openfile(path))
            return 1;
        free(path);
    }
    filname = name;
    return 0;
}

int getfile(char *cp)
{
    struct stat x;
    long symdef_date;
    char magic[SARMAG];

    text = 0;
    if (cp[0] == '-' && cp[1] == 'l') {
        if (!openlib(cp + 2))
            error(2, "cannot find library");
    } else if (!openfile(cp)) {
        filname = cp;
        error(2, "cannot open");
    }

    /* Read file magic. */
    if (fread(magic, 1, SARMAG, text) != SARMAG)
        return (0); /* regular file */
    if (strncmp(magic, ARMAG, SARMAG) != 0)
        return (0); /* regular file */
    if (!fgetarhdr(text, &archdr))
        return (0); /* regular file */
    if (strncmp(archdr.ar_name, SYMDEF, sizeof(SYMDEF)) != 0) {
        if (trace > 1)
            printf("archive '%s': regular first='%s'\n",
                filname, archdr.ar_name);
        free(archdr.ar_name);
        return (1); /* regular archive */
    }
    symdef_date = archdr.ar_date;
    if (trace > 1)
        printf("archive '%s': symdef size=%ld date=%ld\n",
            filname, archdr.ar_size, symdef_date);
    free(archdr.ar_name);
    fstat(fileno(text), &x);
    if (trace > 1)
        printf("archive '%s': mtime=%ld symdef+2=%ld\n",
            filname, (long)x.st_mtime, symdef_date + 2);
    if (x.st_mtime > symdef_date + 2) {
        if (trace > 1)
            printf("archive '%s': out-of-date ranlib\n", filname);
        return (3); /* out of date archive */
    }
    if (trace > 1)
        printf("archive '%s': randomized ranlib\n", filname);
    return (2);     /* randomized archive */
}

int enter(struct nlist **hp)
{
    register struct nlist *sp;

    if (*hp) {
        lastsym = *hp;
        return (0);
    }
    if (symindex >= NSYM)
        error(2, "symbol table overflow");

    symhash[symindex] = hp;
    *hp = lastsym = sp = &symtab[symindex++];
    sp->n_len = cursym.n_len;
    sp->n_name = cursym.n_name;
    sp->n_type = cursym.n_type;
    sp->n_value = cursym.n_value;
    return (1);
}

void symreloc()
{
    switch (cursym.n_type & N_TYPE) {
    case N_TEXT:
        cursym.n_value += ctrel;
        return;
    case N_DATA:
        cursym.n_value += cdrel;
        return;
    case N_CTORS:
        cursym.n_value += cctorrel;
        if (final_layout)
            cursym.n_type = (cursym.n_type & ~N_TYPE) | N_DATA;
        return;
    case N_DTORS:
        cursym.n_value += cdtorrel;
        if (final_layout)
            cursym.n_type = (cursym.n_type & ~N_TYPE) | N_DATA;
        return;
    case N_BSS:
        cursym.n_value += cbrel;
        return;
    case N_UNDF:
    case N_COMM:
        return;
    }
    if (cursym.n_type & N_EXT)
        cursym.n_type = (cursym.n_type & ~N_TYPE) | N_ABS;
}

/*
 * Suboptimal 32-bit hash function.
 * Copyright (C) 2006 Serge Vakulenko.
 */
unsigned hash_rot13(const char *s)
{
    register unsigned hash, c;

    hash = 0;
    while ((c = (unsigned char)*s++) != 0) {
        hash += c;
        hash -= (hash << 13) | (hash >> 19);
    }
    return hash;
}

struct nlist **lookup()
{
    register struct nlist **hp;

    hp = &hshtab[hash_rot13(cursym.n_name) % NSYM + 2];
    while (*hp != 0) {
        if (strcmp(cursym.n_name, (*hp)->n_name) == 0)
            break;
        if (++hp >= &hshtab[NSYM + 2])
            hp = hshtab;
    }
    return (hp);
}

struct nlist **slookup(char *s)
{
    cursym.n_len = strlen(s) + 1;
    cursym.n_name = s;
    cursym.n_type = N_EXT + N_UNDF;
    cursym.n_value = 0;
    return (lookup());
}

void readhdr(unsigned loc)
{
    fseek(text, loc, 0);
    if (!fgethdr(text, &filhdr))
        error(2, "bad format");
    if (N_GETMAGIC(filhdr) != RMAGIC)
        error(2, "bad magic");
    if (filhdr.a_text % W)
        error(2, "bad length of text");
    if (filhdr.a_data % W)
        error(2, "bad length of data");
    /* BSS segment is allowed to be unaligned. */
}

/*
 * single file
 */
int load1(unsigned loc, int libflg, int nloc)
{
    register struct nlist *sp;
    int savindex, ndef, type, symlen, nsymbol;
    struct data_sections sec;

    readhdr(loc);
    if (N_GETMAGIC(filhdr) != RMAGIC) {
        error(1, "file not relocatable");
        return (0);
    }
    read_data_sections(loc, &sec);
    fseek(reloc, loc + N_SYMOFF(filhdr), 0);

    ctrel = tsize;
    cdrel = dsize - filhdr.a_text;
    cctorrel = ctorsize - filhdr.a_text - sec.normal;
    cdtorrel = dtorsize - filhdr.a_text - sec.normal - sec.ctors;
    cbrel = bsize - (filhdr.a_text + filhdr.a_data);

    loc += HDRSZ + filhdr.a_text + filhdr.a_data + filhdr.a_reltext + filhdr.a_reldata;
    fseek(text, loc, 0);
    ndef = 0;
    savindex = symindex;
    if (nloc)
        nsymbol = 1;
    else
        nsymbol = 0;
    for (;;) {
        symlen = fgetsym(text, &cursym);
        if (symlen == 0)
            break;
        type = cursym.n_type;
        if (Sflag && ((type & N_TYPE) == N_ABS || (type & N_TYPE) > N_COMM)) {
            free(cursym.n_name);
            continue;
        }
        if (!(type & N_EXT)) {
            if (is_rebsd_metadata_symbol(&cursym)) {
                free(cursym.n_name);
                continue;
            }
            if (!(sflag || xflag || (Xflag && IS_LOCSYM(&cursym)))) {
                nsymbol++;
                nloc += symlen;
            }
            free(cursym.n_name);
            continue;
        }
        symreloc();
        if (enter(lookup()))
            continue;
        free(cursym.n_name);
        if (cursym.n_type == N_EXT + N_UNDF)
            continue;
        sp = lastsym;
        if ((sp->n_type & N_TYPE) == N_UNDF || (sp->n_type & N_TYPE) == N_COMM) {
            if ((cursym.n_type & N_TYPE) == N_COMM) {
                sp->n_type = cursym.n_type;
                if (cursym.n_value > sp->n_value)
                    sp->n_value = cursym.n_value;
            } else if ((sp->n_type & N_TYPE) == N_UNDF ||
                       (cursym.n_type & N_TYPE) != N_UNDF) {
                ndef++;
                sp->n_type = cursym.n_type;
                sp->n_value = cursym.n_value;
            }
        } else if ((sp->n_type & N_WEAK) && !(cursym.n_type & N_WEAK) &&
                   (cursym.n_type & N_TYPE) != N_UNDF &&
                   (cursym.n_type & N_TYPE) != N_COMM) {
            ndef++;
            sp->n_type = cursym.n_type;
            sp->n_value = cursym.n_value;
        }
    }
    if (!libflg || ndef) {
        tsize += filhdr.a_text;
        dsize += sec.normal;
        ctorsize += sec.ctors;
        dtorsize += sec.dtors;
        bsize += filhdr.a_bss;
        ssize += nloc;
        nsym += nsymbol;

        /* Alignment. */
        tsize = ALIGN(tsize, TEXT_ALIGN);
        dsize = ALIGN(dsize, DATA_ALIGN);
        ctorsize = ALIGN(ctorsize, W);
        dtorsize = ALIGN(dtorsize, W);
        bsize = ALIGN(bsize, BSS_ALIGN);
        return (1);
    }

    /*
     * No symbols defined by this library member.
     * Rip out the hash table entries and reset the symbol table.
     */
    while (symindex > savindex) {
        register struct nlist **p;

        p = symhash[--symindex];
        free((*p)->n_name);
        *p = 0;
    }
    return (0);
}

void addlibp(unsigned nloc)
{
    *libp++ = nloc;
    if (libp >= &liblist[NLIBS])
        error(2, "library table overflow");
}

int step(unsigned nloc)
{
    fseek(text, nloc, 0);
    if (!fgetarhdr(text, &archdr)) {
        return (0);
    }
    if (load1(nloc + ARHDRSZ, 1, mkfsym(archdr.ar_name, 0))) {
        addlibp(nloc);
        if (trace)
            printf("load '%s' offset %08x\n", archdr.ar_name, nloc);
    }
    free(archdr.ar_name);
    return (1);
}

int ldrand()
{
    register struct ranlib *p;
    struct nlist **pp;
    int oldn, loaded;

    loaded = 0;
    for (p = rantab; p < rantab + rancount; ++p) {
        pp = slookup(p->ran_name);
        if (!*pp)
            continue;
        if ((*pp)->n_type == N_EXT + N_UNDF) {
            if (trace > 2)
                printf("ranlib need '%s' offset %08x\n",
                    p->ran_name, p->ran_off);
            oldn = libp - liblist;
            step(p->ran_off);
            if ((int)(libp - liblist) != oldn) {
                if (trace > 2)
                    printf("ranlib loaded '%s'\n", p->ran_name);
                loaded = 1;
            }
        }
    }
    if (trace > 1)
        printf("ranlib pass loaded=%d\n", loaded);
    return loaded;
}

/*
 * scan a library to find defined symbols
 */
void load1lib(unsigned off0)
{
    register unsigned offset;
    register unsigned *oldp;

    /* repeat while any symbols found */
    do {
        oldp = libp;
        offset = off0;
        while (step(offset))
            offset += archdr.ar_size + ARHDRSZ;
    } while (libp != oldp);
    addlibp(-1);
}

/*
 * scan file to find defined symbols
 */
void load1arg(char *cp)
{
    unsigned symdef_size;

    symdef_size = 0;
    switch (getfile(cp)) {
    case 0: /* regular file */
        load1(0L, 0, mkfsym(cp, 0));
        break;
    case 1: /* regular archive */
        load1lib(SARMAG);
        break;
    case 2: /* archive with table of contents */
        symdef_size = archdr.ar_size;
        getrantab();
        while (ldrand())
            continue;
        freerantab();
        /*
         * The ranlib table is only an accelerator.  Do a normal archive
         * sweep afterwards so newly-created undefined symbols are resolved
         * even if a target runtime misses a randomized archive iteration.
         */
        if (trace > 1)
            printf("archive '%s': fallback linear sweep\n", filname);
        load1lib(SARMAG + symdef_size + ARHDRSZ);
        break;
    case 3: /* out of date table of contents */
        error(0, "out of date (warning)");
        load1lib(SARMAG + archdr.ar_size + ARHDRSZ);
        break;
    }
    fclose(text);
    fclose(reloc);
}

void load1libarg(char *name)
{
    char *arg;

    arg = malloc(strlen(name) + 3);
    if (!arg)
        error(2, "out of memory");
    strcpy(arg, "-l");
    strcat(arg, name);
    load1arg(arg);
    free(arg);
}

void pass1(int argc, char **argv)
{
    int c, i;
    char *ap, **p;

    /* scan files once to find symdefs */

    p = argv + 1;
    libp = liblist;
    for (c = 1; c < argc; ++c) {
        filname = 0;
        ap = *p++;

        if (*ap != '-') {
            load1arg(ap);
            continue;
        }
        if (consumelongopt(ap, &c, argc, &p))
            continue;
        for (i = 1; ap[i]; i++) {
            switch (ap[i]) {
                /* output file name */
            case 'o':
                if (++c >= argc)
                    error(2, "-o: argument missing");
                ofilename = *p++;
                ofilfnd++;
                continue;

                /* 'use' */
            case 'u':
                if (++c >= argc)
                    error(2, "-u: argument missing");
                enter(slookup(*p++));
                continue;

                /* 'entry' */
            case 'e':
                if (++c >= argc)
                    error(2, "-e: argument missing");
                enter(slookup(*p++));
                entrypt = lastsym;
                continue;

                /* base address of loading */
            case 'T':
                if (ap[i + 1]) {
                    aout_handle_t_option(&ap[i + 1]);
                    while (ap[i + 1])
                        i++;
                } else {
                    if (++c >= argc)
                        error(2, "-T: argument missing");
                    aout_handle_t_option(*p++);
                }
                continue;

                /* endianness */
            case 'E':
                if (ap[i + 1] == 'L')
                    aout_set_big_endian(0);
                else if (ap[i + 1] == 'B')
                    aout_set_big_endian(1);
                else
                    error(2, "bad endian option");
                while (ap[i + 1])
                    i++;
                continue;

                /* library */
            case 'l':
                if (ap[i + 1]) {
                    load1libarg(&ap[i + 1]);
                    while (ap[i + 1])
                        i++;
                } else {
                    if (++c >= argc)
                        error(2, "-l: argument missing");
                    load1libarg(*p++);
                }
                continue;

                /* library search path */
            case 'L':
                if (ap[i + 1]) {
                    while (ap[i + 1])
                        i++;
                } else {
                    if (++c >= argc)
                        error(2, "-L: argument missing");
                    p++;
                }
                continue;

                /* discard local symbols */
            case 'x':
                xflag++;
                continue;

                /* discard locals starting with 'L' or '.' */
            case 'X':
                Xflag++;
                continue;

                /* discard all except locals and globals*/
            case 'S':
                Sflag++;
                continue;

                /* preserve rel. bits, don't define common */
            case 'r':
                rflag++;
                output_relinfo++;
                continue;

                /* discard all symbols */
            case 's':
                sflag++;
                xflag++;
                continue;

                /* define common even with rflag */
            case 'd':
                dflag++;
                continue;

                /* tracing */
            case 't':
                trace++;
                continue;

                /* verbose */
            case 'v':
                verbose++;
                continue;

            default:
                error(2, "unknown flag");
            }
            break;
        }
    }
}

void middle()
{
    register struct nlist *sp, *symp;
    register unsigned t, cmsize;
    int nund;
    unsigned cmorigin;

    p_etext = *slookup("_etext");
    p_edata = *slookup("_edata");
    p_end = *slookup("_end");
    p_c_etext = *slookup("etext");
    p_c_edata = *slookup("edata");
    p_c_end = *slookup("end");
    p_gp = *slookup("_gp");
    p_ctor_list = *slookup("__CTOR_LIST__");
    p_ctor_end = *slookup("__CTOR_END__");
    p_dtor_list = *slookup("__DTOR_LIST__");
    p_dtor_end = *slookup("__DTOR_END__");

    /*
     * If there are any undefined symbols, save the relocation bits.
     */
    symp = &symtab[symindex];
    if (!output_relinfo) {
        for (sp = symtab; sp < symp; sp++)
            if (sp->n_type == N_EXT + N_UNDF && sp != p_end && sp != p_edata && sp != p_etext &&
                sp != p_c_end && sp != p_c_edata && sp != p_c_etext &&
                sp != p_gp && sp != p_ctor_list && sp != p_ctor_end &&
                sp != p_dtor_list && sp != p_dtor_end) {
                output_relinfo++;
                dflag = 0;
                break;
            }
    }
    if (output_relinfo)
        sflag = 0;

    /*
     * Assign common locations.
     * Align text size to 16 bytes.
     */
    cmsize = 0;
    tsize = (tsize + 15) & ~15;
    normal_dsize = dsize;
    if (dflag || !output_relinfo) {
        ldrsym(p_etext, tsize, N_EXT + N_TEXT);
        ldrsym(p_c_etext, tsize, N_EXT + N_TEXT);

        ldrsym(p_ctor_list, dsize, N_EXT + N_DATA);
        dsize += ctorsize;
        ldrsym(p_ctor_end, dsize, N_EXT + N_DATA);
        ldrsym(p_dtor_list, dsize, N_EXT + N_DATA);
        dsize += dtorsize;
        ldrsym(p_dtor_end, dsize, N_EXT + N_DATA);
        dsize = ALIGN(dsize, DATA_ALIGN);

        ldrsym(p_edata, dsize, N_EXT + N_DATA);
        ldrsym(p_end, bsize, N_EXT + N_BSS);
        ldrsym(p_c_edata, dsize, N_EXT + N_DATA);
        ldrsym(p_c_end, bsize, N_EXT + N_BSS);

        /* Set GP as offset from the start of data segment. */
        ldrsym(p_gp, gpoffset, N_EXT + N_DATA);

        for (sp = symtab; sp < symp; sp++) {
            if ((sp->n_type & N_TYPE) == N_COMM) {
                t = sp->n_value;
                sp->n_value = cmsize;
                cmsize += t;
                cmsize = ALIGN(cmsize, BSS_ALIGN);
            }
        }
    } else {
        dsize = ALIGN(dsize + ctorsize + dtorsize, DATA_ALIGN);
    }

    /*
     * Now set symbols to their final value.
     */
    torigin = basaddr;
    dorigin = torigin + tsize;
    data_origin = dorigin;
    ctor_origin = data_origin + normal_dsize;
    dtor_origin = ctor_origin + ctorsize;
    ctorigin = ctor_origin;
    dtorigin = dtor_origin;
    gp = dorigin + gpoffset;
    cmorigin = dorigin + dsize;
    borigin = cmorigin + cmsize;
    nund = 0;
    for (sp = symtab; sp < symp; sp++) {
        switch (sp->n_type) {
        case N_EXT + N_UNDF:
            if (!rflag) {
                errlev |= 1;
                if (sp == p_end || sp == p_edata || sp == p_etext || sp == p_gp ||
                    sp == p_c_end || sp == p_c_edata || sp == p_c_etext ||
                    sp == p_ctor_list || sp == p_ctor_end ||
                    sp == p_dtor_list || sp == p_dtor_end)
                    break;
                if (!nund)
                    printf("Undefined:\n");
                nund++;
                printf("\t%s\n", sp->n_name);
            }
            break;
        default:
        case N_EXT + N_ABS:
            break;
        case N_EXT + N_TEXT:
            sp->n_value += torigin;
            break;
        case N_EXT + N_DATA:
            sp->n_value += dorigin;
            break;
        case N_EXT + N_CTORS:
            sp->n_type = N_EXT + N_DATA;
            sp->n_value += ctor_origin;
            break;
        case N_EXT + N_DTORS:
            sp->n_type = N_EXT + N_DATA;
            sp->n_value += dtor_origin;
            break;
        case N_EXT + N_BSS:
            sp->n_value += borigin;
            break;
        case N_COMM:
        case N_EXT + N_COMM:
            sp->n_type = N_EXT + N_BSS;
            sp->n_value += cmorigin;
            break;
        }
    }
    final_layout = 1;
    if (sflag || xflag)
        ssize = 0;
    bsize += cmsize;

    /*
     * Compute ssize; add length of local symbols, if need,
     * and one more zero byte. Alignment will be taken at setupout.
     */
    if (sflag)
        ssize = 0;
    else {
        if (xflag)
            ssize = 0;
        for (sp = symtab; sp < &symtab[symindex]; sp++)
            ssize += sp->n_len + 6;
        ssize++;
    }
}

void setupout()
{
    tcreat(&outb, 0);
    int fd = mkstemp(tfname);
    if (fd == -1) {
        error(2, "internal error: unable to create temporary file %s", tfname);
    } else {
        close(fd);
    }

    tcreat(&toutb, 1);
    tcreat(&doutb, 1);
    tcreat(&ctoroutb, 1);
    tcreat(&dtoroutb, 1);

    if (!sflag || !xflag)
        tcreat(&soutb, 1);
    if (output_relinfo) {
        tcreat(&troutb, 1);
        tcreat(&droutb, 1);
        tcreat(&ctorroutb, 1);
        tcreat(&dtorroutb, 1);
    }
    fseek(outb, sizeof(filhdr), 0);
}

void load2(unsigned loc)
{
    register struct nlist *sp;
    register struct local *lp;
    register int symno;
    int type;
    unsigned count;
    struct data_sections sec;

    readhdr(loc);
    read_data_sections(loc, &sec);
    ctrel = torigin;
    cdrel = dorigin - filhdr.a_text;
    cctorrel = ctorigin - filhdr.a_text - sec.normal;
    cdtorrel = dtorigin - filhdr.a_text - sec.normal - sec.ctors;
    cbrel = borigin - (filhdr.a_text + filhdr.a_data);

    if (trace > 1)
        printf("ctrel=%08x, cdrel=%08x, cctorrel=%08x, cdtorrel=%08x, cbrel=%08x\n",
            ctrel, cdrel, cctorrel, cdtorrel, cbrel);
    /*
     * Reread the symbol table, recording the numbering
     * of symbols for fixing external references.
     */
    lp = local;
    symno = -1;
    loc += HDRSZ;
    fseek(text, loc + filhdr.a_text + filhdr.a_data + filhdr.a_reltext + filhdr.a_reldata, 0);
    for (;;) {
        symno++;
        count = fgetsym(text, &cursym);
        if (count == 0)
            break;
        symreloc();
        type = cursym.n_type;
        if (Sflag && ((type & N_TYPE) == N_ABS || (type & N_TYPE) > N_COMM)) {
            free(cursym.n_name);
            continue;
        }
        if (!(type & N_EXT)) {
            if (is_rebsd_metadata_symbol(&cursym)) {
                free(cursym.n_name);
                continue;
            }
            if (!(sflag || xflag || (Xflag && IS_LOCSYM(&cursym))))
                fputsym(&cursym, soutb);
            free(cursym.n_name);
            continue;
        }
        if (!(sp = *lookup()))
            error(2, "internal error: symbol not found");
        if (cursym.n_type == N_EXT + N_UNDF || cursym.n_type == N_EXT + N_COMM) {
            if (lp >= &local[NSYMPR])
                error(2, "local symbol table overflow");
            lp->locindex = symno;
            lp++->locsymbol = sp;
            free(cursym.n_name);
            continue;
        }
        if (cursym.n_type != sp->n_type || cursym.n_value != sp->n_value) {
            if (cursym.n_type & N_WEAK) {
                free(cursym.n_name);
                continue;
            }
            printf("%s: ", cursym.n_name);
            error(1, "name redefined");
        }
        free(cursym.n_name);
    }

    count = loc + filhdr.a_text + filhdr.a_data;

    if (trace > 1)
        printf("-- text --\n");
    fseek(text, loc, 0);
    fseek(reloc, count, 0);
    relocate(lp, toutb, troutb, filhdr.a_text, torigin);

    if (trace > 1)
        printf("-- data --\n");
    fseek(text, loc + filhdr.a_text, 0);
    fseek(reloc, count + filhdr.a_reltext, 0);
    relocate_data_sections(lp, &sec);

    torigin += filhdr.a_text;
    while (torigin % TEXT_ALIGN) {
        struct reloc relabs = { RABS };

        fputword(0, toutb);
        if (output_relinfo)
            fputrel(&relabs, troutb);
        torigin += W;
    }
    dorigin += sec.normal;
    while (dorigin % DATA_ALIGN) {
        struct reloc relabs = { RABS };

        fputword(0, doutb);
        if (output_relinfo)
            fputrel(&relabs, droutb);
        dorigin += W;
    }
    ctorigin += sec.ctors;
    dtorigin += sec.dtors;
    borigin += filhdr.a_bss;

    /* Alignment. */
    torigin = ALIGN(torigin, TEXT_ALIGN);
    dorigin = ALIGN(dorigin, DATA_ALIGN);
    ctorigin = ALIGN(ctorigin, W);
    dtorigin = ALIGN(dtorigin, W);
    borigin = ALIGN(borigin, BSS_ALIGN);
}

void load2arg(char *arname)
{
    register unsigned *lp;

    if (getfile(arname) == 0) {
        if (trace || verbose)
            printf("%s:\n", arname);
        mkfsym(arname, 1);
        load2(0L);
    } else {
        /* scan archive members referenced */
        for (lp = libp; *lp != -1; lp++) {
            fseek(text, *lp, 0);
            fgetarhdr(text, &archdr);
            if (trace || verbose)
                printf("%s(%s):\n", arname, archdr.ar_name);
            mkfsym(archdr.ar_name, 1);
            free(archdr.ar_name);
            load2(*lp + ARHDRSZ);
        }
        libp = ++lp;
    }
    fclose(text);
    fclose(reloc);
}

void load2libarg(char *name)
{
    char *arg;

    arg = malloc(strlen(name) + 3);
    if (!arg)
        error(2, "out of memory");
    strcpy(arg, "-l");
    strcat(arg, name);
    load2arg(arg);
    free(arg);
}

void pass2(int argc, char **argv)
{
    int c, i;
    char *ap, **p;

    p = argv + 1;
    libp = liblist;
    for (c = 1; c < argc; c++) {
        ap = *p++;
        if (*ap != '-') {
            load2arg(ap);
            continue;
        }
        if (consumelongopt(ap, &c, argc, &p))
            continue;
        for (i = 1; ap[i]; i++) {
            switch (ap[i]) {
            case 'u':
            case 'e':
            case 'o':
                ++c;
                ++p;

            default:
                continue;

            case 'E':
                while (ap[i + 1])
                    i++;
                continue;

            case 'T':
                if (ap[i + 1]) {
                    while (ap[i + 1])
                        i++;
                } else {
                    if (++c >= argc)
                        error(2, "-T: argument missing");
                    p++;
                }
                continue;

            case 'L':
                if (ap[i + 1]) {
                    while (ap[i + 1])
                        i++;
                } else {
                    if (++c >= argc)
                        error(2, "-L: argument missing");
                    p++;
                }
                continue;

            case 'l':
                if (ap[i + 1]) {
                    load2libarg(&ap[i + 1]);
                    while (ap[i + 1])
                        i++;
                } else {
                    if (++c >= argc)
                        error(2, "-l: argument missing");
                    load2libarg(*p++);
                }
                continue;
            }
            break;
        }
    }
}

void finishout()
{
    register struct nlist *p;
    unsigned rtsize = 0, rdsize = 0;
    register unsigned n;

    n = copy_and_close(toutb);
    while (n++ < tsize) {
        /* Align text size. */
        putc(0, outb);
    }
    n = copy_and_close(doutb);
    n += copy_and_close(ctoroutb);
    n += copy_and_close(dtoroutb);
    while (n++ < dsize) {
        /* Align data size. */
        putc(0, outb);
    }
    if (output_relinfo) {
        rtsize = copy_and_close(troutb);
        while (rtsize % W) {
            putc(0, outb);
            rtsize++;
        }
        rdsize = copy_and_close(droutb);
        rdsize += copy_and_close(ctorroutb);
        rdsize += copy_and_close(dtorroutb);
        while (rdsize % W) {
            putc(0, outb);
            rdsize++;
        }
    }
    if (!sflag) {
        if (!xflag)
            copy_and_close(soutb);
        for (p = symtab; p < &symtab[symindex]; ++p)
            fputsym(p, outb);
        putc(0, outb);
        while (ssize++ % W)
            putc(0, outb);
    }
    filhdr.a_midmag = output_relinfo ? RMAGIC : OMAGIC;
    filhdr.a_text = tsize;
    filhdr.a_data = dsize;
    filhdr.a_bss = bsize;
    filhdr.a_reltext = rtsize;
    filhdr.a_reldata = rdsize;
    filhdr.a_syms = ALIGN(ssize, W);
    if (entrypt) {
        if (entrypt->n_type != N_EXT + N_TEXT && entrypt->n_type != N_EXT + N_UNDF)
            error(1, "entry out of text");
        else
            filhdr.a_entry = entrypt->n_value;
    } else
        filhdr.a_entry = basaddr;

    fseek(outb, 0, 0);
    fputhdr(&filhdr, outb);
    fclose(outb);
}

#define ELF_MAX_INPUTS   2048
#define ELF_MAX_INSECS   16384
#define ELF_MAX_OUTSECS  512
#define ELF_MAX_STMTS    8192
#define ELF_MAX_PATTERNS 16384
#define ELF_MAX_MEM      64
#define ELF_MAX_SYMS     65536
#define ELF_MAX_ARCHIVES 1024

struct elf_input {
    char *name;
    unsigned char *data;
    unsigned size;
    int big;
    Elf32_Ehdr ehdr;
    Elf32_Shdr *shdr;
    int shnum;
    char *shstr;
    Elf32_Sym *symtab;
    int nsym;
    char *strtab;
    int *secmap;
    int *symmap;
};

struct elf_insec {
    int input;
    int shndx;
    char *name;
    unsigned type;
    unsigned flags;
    unsigned align;
    unsigned size;
    unsigned char *data;
    int discarded;
    int placed;
    int out;
    unsigned outoff;
    unsigned addr;
};

struct elf_lsym {
    char *name;
    int defined;
    int weak;
    int common;
    int input;
    int symndx;
    int insec;
    unsigned value;
    unsigned size;
    unsigned align;
};

struct elf_pattern {
    char *pat;
    int common;
};

struct elf_stmt {
    int kind;
    char *name;
    char *expr;
    int firstpat;
    int npat;
};

struct elf_outsec {
    char *name;
    char *region;
    char *phdr;
    char *addr_expr;
    int has_addr;
    int noload;
    int firststmt;
    int nstmt;
    int firstpost;
    int npost;
    unsigned addr;
    unsigned size;
    unsigned flags;
    unsigned type;
    unsigned align;
    unsigned char *buf;
    unsigned fileoff;
    int shndx;
    int phndx;
};

struct elf_mem {
    char *name;
    unsigned origin;
    unsigned length;
    unsigned cursor;
};

struct elf_assertion {
    char *expr;
    char *message;
};

struct elf_phdr_script {
    char *name;
    unsigned flags;
    int phndx;
};

struct elf_archive {
    char *path;
};

enum {
    ESTMT_INPUT,
    ESTMT_ASSIGN,
    ESTMT_DOTASSIGN,
};

static struct elf_input *einput;
static int neinput, ceinput;
static struct elf_insec *einsec;
static int neinsec, ceinsec;
static struct elf_lsym *elsym;
static int nelsym, celsym;
static struct elf_pattern *epat;
static int nepat, cepat;
static struct elf_stmt *estmt;
static int nestmt, cestmt;
static struct elf_outsec *eout;
static int neout, ceout;
static struct elf_mem *emem;
static int nemem, cemem;
static struct elf_assertion *eassert;
static int neassert, ceassert;
static struct elf_phdr_script *ephdr;
static int nephdr, cephdr;
static struct elf_archive *earchive;
static int nearchive, cearchive;
static struct elf_stmt *eprestmt;
static int neprestmt, ceprestmt;
static struct elf_stmt *eglobstmt;
static int neglobstmt, ceglobstmt;
static int ediscard_first = -1, nediscard;
static char *elf_script_file;
static char *elf_entry_symbol;
static int elf_mode;
static unsigned elf_last_dot;
static unsigned elf_target_machine;
static int elf_target_big;
static int elf_target_endian_known;

static void
elf_select_endian(int big, const char *source)
{
    if (elf_target_endian_known && elf_target_big != big)
        error(2, "%s selects an incompatible ELF byte order", source);
    if (elf_target_machine == EM_386 && big)
        error(2, "%s: ELF32/i386 is little-endian", source);
    elf_target_big = big;
    elf_target_endian_known = 1;
    aout_set_big_endian(big);
}

static void
elf_select_machine(unsigned machine, int big, const char *source)
{
    if (machine != EM_MIPS && machine != EM_386)
        error(2, "%s: unsupported ELF32 machine %u", source, machine);
    if (elf_target_machine && elf_target_machine != machine)
        error(2, "%s: cannot mix ELF32 machine types", source);
    if (machine == EM_386 && big)
        error(2, "%s: big-endian ELF32/i386 object", source);
    elf_target_machine = machine;
    elf_select_endian(big, source);
}

static void
elf_select_emulation(const char *name)
{
    if (strcmp(name, "elf_i386") == 0 || strcmp(name, "elf32-i386") == 0) {
        elf_select_machine(EM_386, 0, name);
        return;
    }
    if (strcmp(name, "elf32btsmip") == 0 ||
        strcmp(name, "elf32-bigmips") == 0) {
        elf_select_machine(EM_MIPS, 1, name);
        return;
    }
    if (strcmp(name, "elf32ltsmip") == 0 ||
        strcmp(name, "elf32-littlemips") == 0) {
        elf_select_machine(EM_MIPS, 0, name);
        return;
    }
    error(2, "unsupported ELF emulation %s", name);
}

static void *
elf_reserve_array(void *ptr, int *cap, int need, int max, size_t size,
    const char *what)
{
    char *p;
    int oldcap, newcap;

    if (need > max)
        error(2, "too many %s", what);
    if (*cap >= need)
        return ptr;
    oldcap = *cap;
    newcap = oldcap ? oldcap : 16;
    while (newcap < need) {
        if (newcap > max / 2) {
            newcap = max;
            break;
        }
        newcap *= 2;
    }
    p = realloc(ptr, (size_t)newcap * size);
    if (!p)
        error(2, "out of memory");
    memset(p + (size_t)oldcap * size, 0, (size_t)(newcap - oldcap) * size);
    *cap = newcap;
    return p;
}

#define ELF_RESERVE(tab, cap, need, max, what) \
    ((tab) = elf_reserve_array((tab), &(cap), (need), (max), \
        sizeof(*(tab)), (what)))

static unsigned
elf_get16p(const unsigned char *p, int big)
{
    if (big)
        return ((unsigned)p[0] << 8) | p[1];
    return p[0] | ((unsigned)p[1] << 8);
}

static unsigned
elf_get32p(const unsigned char *p, int big)
{
    if (big)
        return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
            ((unsigned)p[2] << 8) | p[3];
    return p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
        ((unsigned)p[3] << 24);
}

static void
elf_put16p(unsigned char *p, unsigned value, int big)
{
    if (big) {
        p[0] = value >> 8;
        p[1] = value;
    } else {
        p[0] = value;
        p[1] = value >> 8;
    }
}

static void
elf_put32p(unsigned char *p, unsigned value, int big)
{
    if (big) {
        p[0] = value >> 24;
        p[1] = value >> 16;
        p[2] = value >> 8;
        p[3] = value;
    } else {
        p[0] = value;
        p[1] = value >> 8;
        p[2] = value >> 16;
        p[3] = value >> 24;
    }
}

static unsigned
elf_load16buf(unsigned char *p)
{
    return elf_get16p(p, elf_target_big);
}

static unsigned
elf_load32buf(unsigned char *p)
{
    return elf_get32p(p, elf_target_big);
}

static void
elf_store16buf(unsigned char *p, unsigned value)
{
    elf_put16p(p, value, elf_target_big);
}

static void
elf_store32buf(unsigned char *p, unsigned value)
{
    elf_put32p(p, value, elf_target_big);
}

static unsigned
elf_align(unsigned value, unsigned align)
{
    if (align <= 1)
        return value;
    return (value + align - 1) & ~(align - 1);
}

static unsigned
elf_align_mod(unsigned value, unsigned align, unsigned mod)
{
    unsigned rem;

    if (align <= 1)
        return value;
    mod &= align - 1;
    rem = value & (align - 1);
    if (rem <= mod)
        return value + mod - rem;
    return value + align - rem + mod;
}

static size_t
elf_strlen(const char *s)
{
    const char *p;

    p = s;
    while (*p)
        p++;
    return p - s;
}

static int
elf_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int
elf_streq(const char *a, const char *b)
{
    return elf_strcmp(a, b) == 0;
}

static char *
elf_strdup(const char *s)
{
    char *p;
    size_t len, i;

    len = elf_strlen(s) + 1;
    p = malloc(len);
    if (!p)
        error(2, "out of memory");
    for (i = 0; i < len; i++)
        p[i] = s[i];
    return p;
}

static void
elf_copy_bytes(void *dst, const void *src, size_t len)
{
    unsigned char *d;
    const unsigned char *s;

    d = dst;
    s = src;
    while (len--)
        *d++ = *s++;
}

static char *
elf_strndup_trim(const char *s, unsigned len)
{
    char *p;
    unsigned i;

    while (len && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) {
        s++;
        len--;
    }
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                   s[len - 1] == '\n' || s[len - 1] == '\r'))
        len--;
    p = malloc(len + 1);
    if (!p)
        error(2, "out of memory");
    for (i = 0; i < len; i++)
        p[i] = s[i];
    p[len] = 0;
    return p;
}

static int
elf_match_pattern(const char *pat, const char *name)
{
    const char *star;
    unsigned prefix, suffix;

    star = strchr(pat, '*');
    if (!star)
        return strcmp(pat, name) == 0;
    prefix = star - pat;
    suffix = strlen(star + 1);
    if (strncmp(pat, name, prefix) != 0)
        return 0;
    if (strlen(name) < prefix + suffix)
        return 0;
    return strcmp(name + strlen(name) - suffix, star + 1) == 0;
}

static int
elf_find_mem(const char *name)
{
    int i;

    for (i = 0; i < nemem; i++)
        if (elf_streq(emem[i].name, name))
            return i;
    return -1;
}

static int
elf_find_lsym(const char *name)
{
    int i;

    for (i = 0; i < nelsym; i++)
        if (elf_streq(elsym[i].name, name))
            return i;
    return -1;
}

static int
elf_add_lsym(const char *name)
{
    int i;

    i = elf_find_lsym(name);
    if (i >= 0)
        return i;
    ELF_RESERVE(elsym, celsym, nelsym + 1, ELF_MAX_SYMS, "ELF symbols");
    i = nelsym++;
    memset(&elsym[i], 0, sizeof(elsym[i]));
    elsym[i].name = elf_strdup(name);
    elsym[i].input = -1;
    elsym[i].symndx = -1;
    elsym[i].insec = -1;
    elsym[i].align = W;
    return i;
}

static void
elf_define_abs(const char *name, unsigned value)
{
    int i;

    i = elf_add_lsym(name);
    if (elsym[i].defined && !elsym[i].weak)
        elsym[i].value = value;
    else {
        elsym[i].defined = 1;
        elsym[i].common = 0;
        elsym[i].value = value;
    }
}

static unsigned
elf_number(const char *s, char **endp)
{
    unsigned value;
    char *e;

    value = strtoul(s, &e, 0);
    if (*e == 'K' || *e == 'k') {
        value *= 1024;
        e++;
    } else if (*e == 'M' || *e == 'm') {
        value *= 1024 * 1024;
        e++;
    }
    *endp = e;
    return value;
}

struct expr_parser {
    const char *p;
    unsigned dot;
};

static void
expr_skip(struct expr_parser *e)
{
    while (*e->p == ' ' || *e->p == '\t' || *e->p == '\n' || *e->p == '\r')
        e->p++;
}

static void
expr_name(struct expr_parser *e, char *buf, unsigned len)
{
    char *p;

    expr_skip(e);
    p = buf;
    while ((*e->p == '_' || *e->p == '.' || *e->p == '/' ||
            (*e->p >= 'a' && *e->p <= 'z') ||
            (*e->p >= 'A' && *e->p <= 'Z') ||
            (*e->p >= '0' && *e->p <= '9')) && p + 1 < buf + len)
        *p++ = *e->p++;
    *p = 0;
}

static unsigned expr_relation(struct expr_parser *e);

static unsigned
expr_primary(struct expr_parser *e)
{
    char namebuf[256], *endp;
    unsigned v, a;
    int i;

    expr_skip(e);
    if (*e->p == '(') {
        e->p++;
        v = expr_relation(e);
        expr_skip(e);
        if (*e->p == ')')
            e->p++;
        return v;
    }
    if ((*e->p >= '0' && *e->p <= '9')) {
        v = elf_number(e->p, &endp);
        e->p = endp;
        return v;
    }
    expr_name(e, namebuf, sizeof(namebuf));
    if (namebuf[0] == 0)
        return 0;
    expr_skip(e);
    if (*e->p == '(') {
        char funcbuf[256];

        strcpy(funcbuf, namebuf);
        e->p++;
        if (strcmp(funcbuf, "ALIGN") == 0) {
            a = expr_relation(e);
            expr_skip(e);
            if (*e->p == ')')
                e->p++;
            return elf_align(e->dot, a);
        }
        expr_name(e, namebuf, sizeof(namebuf));
        expr_skip(e);
        if (*e->p == ')')
            e->p++;
        i = elf_find_mem(namebuf);
        if (i < 0)
            error(2, "unknown memory region %s", namebuf);
        return strcmp(funcbuf, "ORIGIN") == 0 ? emem[i].origin : emem[i].length;
    }
    if (strcmp(namebuf, ".") == 0)
        return e->dot;
    i = elf_find_lsym(namebuf);
    if (i < 0 || !elsym[i].defined)
        return 0;
    return elsym[i].value;
}

static unsigned
expr_add(struct expr_parser *e)
{
    unsigned v, r;

    v = expr_primary(e);
    for (;;) {
        expr_skip(e);
        if (*e->p == '+') {
            e->p++;
            r = expr_primary(e);
            v += r;
        } else if (*e->p == '-') {
            e->p++;
            r = expr_primary(e);
            v -= r;
        } else
            return v;
    }
}

static unsigned
expr_shift(struct expr_parser *e)
{
    unsigned v, r;

    v = expr_add(e);
    for (;;) {
        expr_skip(e);
        if (e->p[0] == '<' && e->p[1] == '<') {
            e->p += 2;
            r = expr_add(e);
            v = r < sizeof(v) * 8 ? v << r : 0;
        } else if (e->p[0] == '>' && e->p[1] == '>') {
            e->p += 2;
            r = expr_add(e);
            v = r < sizeof(v) * 8 ? v >> r : 0;
        } else
            return v;
    }
}

static unsigned
expr_relation(struct expr_parser *e)
{
    unsigned v, r;

    v = expr_shift(e);
    expr_skip(e);
    if (e->p[0] == '<' && e->p[1] == '=') {
        e->p += 2;
        r = expr_shift(e);
        return v <= r;
    }
    return v;
}

static unsigned
elf_eval_expr(const char *expr, unsigned dot)
{
    struct expr_parser e;

    e.p = expr;
    e.dot = dot;
    return expr_relation(&e);
}

static char *script_base, *script_p, script_tok[256];
static int script_unget_tok, script_unget_kind;

enum {
    STOK_EOF = 256,
    STOK_NAME,
    STOK_STRING,
};

static void
script_skip(void)
{
    for (;;) {
        while (*script_p == ' ' || *script_p == '\t' ||
               *script_p == '\n' || *script_p == '\r')
            script_p++;
        if (script_p[0] == '/' && script_p[1] == '*') {
            script_p += 2;
            while (script_p[0] && !(script_p[0] == '*' && script_p[1] == '/'))
                script_p++;
            if (script_p[0])
                script_p += 2;
            continue;
        }
        return;
    }
}

static int
script_next(void)
{
    char *p;
    int c;

    if (script_unget_tok) {
        script_unget_tok = 0;
        return script_unget_kind;
    }
    script_skip();
    c = *script_p;
    if (c == 0)
        return STOK_EOF;
    if (strchr("{}():;=,*>!+", c)) {
        script_p++;
        return c;
    }
    if (c == '"') {
        script_p++;
        p = script_tok;
        while (*script_p && *script_p != '"' && p + 1 < script_tok + sizeof(script_tok))
            *p++ = *script_p++;
        if (*script_p == '"')
            script_p++;
        *p = 0;
        return STOK_STRING;
    }
    p = script_tok;
    while (*script_p && !strchr(" \t\r\n{}():;=,*>!+\"", *script_p) &&
           p + 1 < script_tok + sizeof(script_tok))
        *p++ = *script_p++;
    *p = 0;
    return STOK_NAME;
}

static void
script_unget(int kind)
{
    script_unget_tok = 1;
    script_unget_kind = kind;
}

static void
script_expect(int kind)
{
    int got;

    got = script_next();
    if (got != kind)
        error(2, "bad linker script syntax");
}

static void
script_skip_parens(void)
{
    int k, depth;

    script_expect('(');
    depth = 1;
    while (depth > 0) {
        k = script_next();
        if (k == STOK_EOF)
            error(2, "unterminated linker script command");
        if (k == '(')
            depth++;
        else if (k == ')')
            depth--;
    }
}

static char *
script_collect_until(int delim)
{
    char *start;
    int depth, quote;

    script_skip();
    start = script_p;
    depth = 0;
    quote = 0;
    while (*script_p) {
        if (quote) {
            if (*script_p == quote)
                quote = 0;
            script_p++;
            continue;
        }
        if (*script_p == '"' || *script_p == '\'') {
            quote = *script_p++;
            continue;
        }
        if (*script_p == delim && depth == 0) {
            char *s = elf_strndup_trim(start, script_p - start);
            script_p++;
            return s;
        }
        if (*script_p == '(')
            depth++;
        else if (*script_p == ')') {
            if (depth > 0)
                depth--;
        }
        script_p++;
    }
    return elf_strndup_trim(start, script_p - start);
}

static char *
script_collect_from_until(char *start, int delim)
{
    int depth, quote;

    depth = 0;
    quote = 0;
    while (*script_p) {
        if (quote) {
            if (*script_p == quote)
                quote = 0;
            script_p++;
            continue;
        }
        if (*script_p == '"' || *script_p == '\'') {
            quote = *script_p++;
            continue;
        }
        if (*script_p == delim && depth == 0) {
            char *s = elf_strndup_trim(start, script_p - start);
            script_p++;
            return s;
        }
        if (*script_p == '(')
            depth++;
        else if (*script_p == ')') {
            if (depth > 0)
                depth--;
        }
        script_p++;
    }
    return elf_strndup_trim(start, script_p - start);
}

static void
script_add_pattern(const char *pat)
{
    ELF_RESERVE(epat, cepat, nepat + 1, ELF_MAX_PATTERNS,
        "linker script patterns");
    epat[nepat].pat = elf_strdup(pat);
    epat[nepat].common = strcmp(pat, "COMMON") == 0;
    nepat++;
}

static void
script_parse_input_stmt(struct elf_stmt *st)
{
    memset(st, 0, sizeof(*st));
    st->kind = ESTMT_INPUT;
    st->firstpat = nepat;
    script_expect('*');
    script_expect('(');
    for (;;) {
        char *start;

        script_skip();
        if (*script_p == ')') {
            script_p++;
            break;
        }
        start = script_p;
        while (*script_p && *script_p != ')' &&
               *script_p != ' ' && *script_p != '\t' &&
               *script_p != '\n' && *script_p != '\r')
            script_p++;
        if (script_p == start)
            error(2, "bad input section pattern");
        {
            char *pat = elf_strndup_trim(start, script_p - start);
            script_add_pattern(pat);
            free(pat);
        }
    }
    st->npat = nepat - st->firstpat;
}

static void
script_add_stmt(struct elf_stmt *st)
{
    ELF_RESERVE(estmt, cestmt, nestmt + 1, ELF_MAX_STMTS,
        "linker script statements");
    estmt[nestmt++] = *st;
}

static void
script_parse_provide_stmt(struct elf_stmt *st)
{
    int k;

    memset(st, 0, sizeof(*st));
    script_expect('(');
    if (script_next() != STOK_NAME)
        error(2, "bad PROVIDE");
    st->kind = ESTMT_ASSIGN;
    st->name = elf_strdup(script_tok);
    script_expect('=');
    st->expr = script_collect_until(')');
    k = script_next();
    if (k != ';')
        script_unget(k);
}

static void
script_add_toplevel_stmt(struct elf_stmt *st, int outidx)
{
    if (outidx >= 0) {
        if (eout[outidx].npost == 0)
            eout[outidx].firstpost = neglobstmt;
        ELF_RESERVE(eglobstmt, ceglobstmt, neglobstmt + 1, 1024,
            "global linker script statements");
        eglobstmt[neglobstmt++] = *st;
        eout[outidx].npost++;
    } else {
        ELF_RESERVE(eprestmt, ceprestmt, neprestmt + 1, 1024,
            "pre-section assignments");
        eprestmt[neprestmt++] = *st;
    }
}

static void
script_parse_output_body(int outidx)
{
    int k, k2;
    struct elf_stmt st;

    eout[outidx].firststmt = nestmt;
    for (;;) {
        k = script_next();
        if (k == '}')
            break;
        if (k == '*') {
            script_unget(k);
            script_parse_input_stmt(&st);
            script_add_stmt(&st);
            continue;
        }
        if (k == STOK_NAME && strcmp(script_tok, "KEEP") == 0) {
            script_expect('(');
            script_parse_input_stmt(&st);
            script_expect(')');
            script_add_stmt(&st);
            continue;
        }
        if (k == STOK_NAME && strcmp(script_tok, "PROVIDE") == 0) {
            script_parse_provide_stmt(&st);
            script_add_stmt(&st);
            continue;
        }
        if (k == STOK_NAME || k == '.') {
            char lhs[256];

            if (k == '.')
                strcpy(lhs, ".");
            else
                strcpy(lhs, script_tok);
            k2 = script_next();
            if (k2 != '=')
                error(2, "bad output section statement");
            memset(&st, 0, sizeof(st));
            st.kind = strcmp(lhs, ".") == 0 ? ESTMT_DOTASSIGN : ESTMT_ASSIGN;
            st.name = elf_strdup(lhs);
            st.expr = script_collect_until(';');
            script_add_stmt(&st);
            continue;
        }
        error(2, "bad output section statement");
    }
    eout[outidx].nstmt = nestmt - eout[outidx].firststmt;
}

static void
script_parse_memory(void)
{
    int k;

    script_expect('{');
    for (;;) {
        char namebuf[256], *expr;
        int idx;

        k = script_next();
        if (k == '}')
            return;
        if (k != STOK_NAME)
            error(2, "bad MEMORY entry");
        strcpy(namebuf, script_tok);
        k = script_next();
        if (k == '(') {
            while ((k = script_next()) != ')' && k != STOK_EOF)
                ;
        } else
            script_unget(k);
        script_expect(':');
        if (script_next() != STOK_NAME || strcmp(script_tok, "ORIGIN") != 0)
            error(2, "bad MEMORY ORIGIN");
        script_expect('=');
        expr = script_collect_until(',');
        ELF_RESERVE(emem, cemem, nemem + 1, ELF_MAX_MEM, "MEMORY regions");
        idx = nemem++;
        emem[idx].name = elf_strdup(namebuf);
        emem[idx].origin = elf_eval_expr(expr, 0);
        emem[idx].cursor = emem[idx].origin;
        free(expr);
        if (script_next() != STOK_NAME || strcmp(script_tok, "LENGTH") != 0)
            error(2, "bad MEMORY LENGTH");
        script_expect('=');
        script_skip();
        {
            char *start = script_p;
            while (*script_p && *script_p != '\n' && *script_p != '\r' &&
                   *script_p != '}')
                script_p++;
            expr = elf_strndup_trim(start, script_p - start);
        }
        emem[idx].length = elf_eval_expr(expr, 0);
        free(expr);
    }
}

static void
script_parse_phdrs(void)
{
    int k;

    script_expect('{');
    for (;;) {
        int idx;

        k = script_next();
        if (k == '}')
            return;
        if (k != STOK_NAME)
            error(2, "bad PHDRS entry");
        ELF_RESERVE(ephdr, cephdr, nephdr + 1, ELF_MAX_OUTSECS,
            "program headers");
        idx = nephdr++;
        memset(&ephdr[idx], 0, sizeof(ephdr[idx]));
        ephdr[idx].name = elf_strdup(script_tok);
        ephdr[idx].flags = PF_R | PF_W | PF_X;
        ephdr[idx].phndx = -1;
        if (script_next() != STOK_NAME)
            error(2, "bad PHDRS type");
        for (;;) {
            k = script_next();
            if (k == ';')
                break;
            if (k == STOK_EOF || k == '}')
                error(2, "bad PHDRS entry");
            if (k == STOK_NAME && strcmp(script_tok, "FLAGS") == 0) {
                char *expr;

                script_expect('(');
                expr = script_collect_until(')');
                ephdr[idx].flags = elf_eval_expr(expr, 0);
                free(expr);
            } else if (k == STOK_NAME) {
                k = script_next();
                if (k == '(')
                    script_unget(k);
            }
        }
    }
}

static void
script_parse_sections(void)
{
    int k, k2, outidx, last_out;
    struct elf_stmt st;

    last_out = -1;
    script_expect('{');
    for (;;) {
        k = script_next();
        if (k == '}')
            return;
        if (k == STOK_NAME && strcmp(script_tok, "PROVIDE") == 0) {
            script_parse_provide_stmt(&st);
            script_add_toplevel_stmt(&st, last_out);
            continue;
        }
        if (k == STOK_NAME && strcmp(script_tok, "ASSERT") == 0) {
            script_expect('(');
            ELF_RESERVE(eassert, ceassert, neassert + 1, 128,
                "ASSERT statements");
            eassert[neassert].expr = script_collect_until(',');
            k2 = script_next();
            if (k2 == STOK_STRING)
                eassert[neassert].message = elf_strdup(script_tok);
            else
                eassert[neassert].message = elf_strdup("linker script assertion failed");
            script_expect(')');
            neassert++;
            continue;
        }
        if (k == STOK_NAME && strcmp(script_tok, "/DISCARD/") == 0) {
            script_expect(':');
            script_expect('{');
            if (ediscard_first < 0)
                ediscard_first = nepat;
            while ((k2 = script_next()) != '}' && k2 != STOK_EOF) {
                if (k2 == '*') {
                    script_unget(k2);
                    script_parse_input_stmt(&st);
                    nediscard += st.npat;
                }
            }
            continue;
        }
        if (k != STOK_NAME && k != '.')
            error(2, "bad SECTIONS command");
        {
            char lhs[256];
            int noload;

            noload = 0;
            if (k == '.')
                strcpy(lhs, ".");
            else
                strcpy(lhs, script_tok);
            k2 = script_next();
            if (k2 == '=') {
                memset(&st, 0, sizeof(st));
                st.kind = strcmp(lhs, ".") == 0 ? ESTMT_DOTASSIGN : ESTMT_ASSIGN;
                st.name = elf_strdup(lhs);
                st.expr = script_collect_until(';');
                script_add_toplevel_stmt(&st, last_out);
                continue;
            }
            if (k2 == '(') {
                k2 = script_next();
                if (k2 != STOK_NAME || strcmp(script_tok, "NOLOAD") != 0)
                    error(2, "bad output section attribute");
                noload = 1;
                script_expect(')');
                k2 = script_next();
            }
            if (k2 != ':') {
                if (k2 != STOK_NAME && k2 != '.')
                    error(2, "bad output section");
                st.expr = script_collect_from_until(
                    script_p - strlen(script_tok), ':');
            } else
                st.expr = 0;
            ELF_RESERVE(eout, ceout, neout + 1, ELF_MAX_OUTSECS,
                "output sections");
            outidx = neout++;
            memset(&eout[outidx], 0, sizeof(eout[outidx]));
            eout[outidx].name = elf_strdup(lhs);
            eout[outidx].addr_expr = st.expr;
            eout[outidx].has_addr = st.expr != 0;
            eout[outidx].noload = noload;
            eout[outidx].align = W;
            script_expect('{');
            script_parse_output_body(outidx);
            for (;;) {
                k2 = script_next();
                if (k2 == '>') {
                    if (script_next() != STOK_NAME)
                        error(2, "bad output section region");
                    eout[outidx].region = elf_strdup(script_tok);
                    continue;
                }
                if (k2 == ':') {
                    if (script_next() != STOK_NAME)
                        error(2, "bad output section phdr");
                    if (!eout[outidx].phdr && strcmp(script_tok, "NONE") != 0)
                        eout[outidx].phdr = elf_strdup(script_tok);
                    continue;
                }
                script_unget(k2);
                break;
            }
            last_out = outidx;
        }
    }
}

static void
script_parse_file(const char *path)
{
    FILE *f;
    long len;
    int k;

    f = fopen(path, "r");
    if (!f)
        error(2, "cannot open linker script %s", path);
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    script_base = malloc(len + 1);
    if (!script_base)
        error(2, "out of memory");
    if (fread(script_base, 1, len, f) != (size_t)len)
        error(2, "cannot read linker script");
    fclose(f);
    script_base[len] = 0;
    script_p = script_base;
    script_unget_tok = 0;
    while ((k = script_next()) != STOK_EOF) {
        if (k == STOK_NAME && strcmp(script_tok, "OUTPUT_ARCH") == 0) {
            script_skip_parens();
        } else if (k == STOK_NAME && strcmp(script_tok, "OUTPUT_FORMAT") == 0) {
            script_skip_parens();
        } else if (k == STOK_NAME && strcmp(script_tok, "ENTRY") == 0) {
            script_expect('(');
            if (script_next() != STOK_NAME)
                error(2, "bad ENTRY");
            elf_entry_symbol = elf_strdup(script_tok);
            script_expect(')');
        } else if (k == STOK_NAME && strcmp(script_tok, "PHDRS") == 0) {
            script_parse_phdrs();
        } else if (k == STOK_NAME && strcmp(script_tok, "MEMORY") == 0) {
            script_parse_memory();
        } else if (k == STOK_NAME && strcmp(script_tok, "SECTIONS") == 0) {
            script_parse_sections();
        } else if (k == STOK_NAME) {
            char lhs[256];
            struct elf_stmt st;

            strcpy(lhs, script_tok);
            if (script_next() != '=')
                error(2, "bad linker script command");
            memset(&st, 0, sizeof(st));
            st.kind = ESTMT_ASSIGN;
            st.name = elf_strdup(lhs);
            st.expr = script_collect_until(';');
            ELF_RESERVE(eprestmt, ceprestmt, neprestmt + 1, 1024,
                "pre-section assignments");
            eprestmt[neprestmt++] = st;
        } else {
            error(2, "bad linker script command");
        }
    }
}

static int
aout_parse_t_address(char *arg, unsigned *addrp)
{
    char *endp;
    unsigned addr;

    addr = elf_number(arg, &endp);
    if (endp == arg)
        return 0;
    while (*endp == ' ' || *endp == '\t' || *endp == '\n' ||
        *endp == '\r')
        endp++;
    if (*endp)
        return 0;
    *addrp = addr;
    return 1;
}

static void
aout_apply_script_base(int prestart, int outstart)
{
    unsigned seqdot, candidate;
    int i, have_candidate, saw_dot;

    seqdot = basaddr;
    candidate = basaddr;
    have_candidate = 0;
    saw_dot = 0;
    for (i = prestart; i < neprestmt; i++) {
        if (eprestmt[i].kind == ESTMT_DOTASSIGN) {
            seqdot = elf_eval_expr(eprestmt[i].expr, seqdot);
            candidate = seqdot;
            have_candidate = 1;
            saw_dot = 1;
            continue;
        }
        if (eprestmt[i].kind == ESTMT_ASSIGN && eprestmt[i].name &&
            strcmp(eprestmt[i].name, "__executable_start") == 0 &&
            !have_candidate) {
            candidate = elf_eval_expr(eprestmt[i].expr, seqdot);
            have_candidate = 1;
        }
    }
    if (saw_dot || have_candidate) {
        basaddr = candidate;
        return;
    }

    for (i = outstart; i < neout; i++) {
        if (eout[i].has_addr) {
            basaddr = elf_eval_expr(eout[i].addr_expr, basaddr);
            return;
        }
    }
}

static void
aout_handle_t_option(char *arg)
{
    unsigned addr;
    int prestart, outstart;

    if (aout_parse_t_address(arg, &addr)) {
        basaddr = addr;
        return;
    }

    prestart = neprestmt;
    outstart = neout;
    script_parse_file(arg);
    aout_apply_script_base(prestart, outstart);
    if (elf_entry_symbol && !entrypt) {
        enter(slookup(elf_entry_symbol));
        entrypt = lastsym;
    }
    if (trace)
        printf("linker script '%s': a.out text base %#x\n", arg, basaddr);
}

static void
elf_read_ehdr(struct elf_input *in)
{
    unsigned char *p;

    if (in->size < sizeof(Elf32_Ehdr))
        error(2, "%s: truncated ELF header", in->name);
    p = in->data;
    if (p[0] != ELFMAG0 || p[1] != ELFMAG1 || p[2] != ELFMAG2 ||
        p[3] != ELFMAG3 || p[4] != ELFCLASS32)
        error(2, "%s: not ELF32", in->name);
    in->big = p[EI_DATA] == ELFDATA2MSB;
    in->ehdr.e_type = elf_get16p(p + 16, in->big);
    in->ehdr.e_machine = elf_get16p(p + 18, in->big);
    in->ehdr.e_version = elf_get32p(p + 20, in->big);
    in->ehdr.e_entry = elf_get32p(p + 24, in->big);
    in->ehdr.e_phoff = elf_get32p(p + 28, in->big);
    in->ehdr.e_shoff = elf_get32p(p + 32, in->big);
    in->ehdr.e_flags = elf_get32p(p + 36, in->big);
    in->ehdr.e_ehsize = elf_get16p(p + 40, in->big);
    in->ehdr.e_phentsize = elf_get16p(p + 42, in->big);
    in->ehdr.e_phnum = elf_get16p(p + 44, in->big);
    in->ehdr.e_shentsize = elf_get16p(p + 46, in->big);
    in->ehdr.e_shnum = elf_get16p(p + 48, in->big);
    in->ehdr.e_shstrndx = elf_get16p(p + 50, in->big);
    if (in->ehdr.e_type != ET_REL ||
        (in->ehdr.e_machine != EM_MIPS && in->ehdr.e_machine != EM_386))
        error(2, "%s: unsupported ELF object", in->name);
    elf_select_machine(in->ehdr.e_machine, in->big, in->name);
}

static Elf32_Shdr
elf_read_shdr(struct elf_input *in, int idx)
{
    unsigned char *p;
    Elf32_Shdr sh;

    p = in->data + in->ehdr.e_shoff + idx * in->ehdr.e_shentsize;
    sh.sh_name = elf_get32p(p + 0, in->big);
    sh.sh_type = elf_get32p(p + 4, in->big);
    sh.sh_flags = elf_get32p(p + 8, in->big);
    sh.sh_addr = elf_get32p(p + 12, in->big);
    sh.sh_offset = elf_get32p(p + 16, in->big);
    sh.sh_size = elf_get32p(p + 20, in->big);
    sh.sh_link = elf_get32p(p + 24, in->big);
    sh.sh_info = elf_get32p(p + 28, in->big);
    sh.sh_addralign = elf_get32p(p + 32, in->big);
    sh.sh_entsize = elf_get32p(p + 36, in->big);
    return sh;
}

static Elf32_Sym
elf_read_sym(struct elf_input *in, unsigned char *p)
{
    Elf32_Sym s;

    s.st_name = elf_get32p(p + 0, in->big);
    s.st_value = elf_get32p(p + 4, in->big);
    s.st_size = elf_get32p(p + 8, in->big);
    s.st_info = p[12];
    s.st_other = p[13];
    s.st_shndx = elf_get16p(p + 14, in->big);
    return s;
}

static void
elf_read_file_image(const char *path, unsigned char **datap, unsigned *sizep)
{
    FILE *f;
    long len;
    unsigned char *data;

    f = fopen(path, "r");
    if (!f)
        error(2, "%s: cannot open", path);
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0)
        error(2, "%s: cannot stat", path);
    data = malloc(len ? (unsigned)len : 1);
    if (!data)
        error(2, "out of memory");
    if (len && fread(data, 1, len, f) != (size_t)len)
        error(2, "%s: cannot read", path);
    fclose(f);
    *datap = data;
    *sizep = (unsigned)len;
}

static int
elf_is_archive_image(unsigned char *data, unsigned size)
{
    return size >= SARMAG && memcmp(data, ARMAG, SARMAG) == 0;
}

static int
elf_is_archive_file(const char *path)
{
    FILE *f;
    char magic[SARMAG];
    int ok;

    f = fopen(path, "r");
    if (!f)
        return 0;
    ok = fread(magic, 1, SARMAG, f) == SARMAG &&
        memcmp(magic, ARMAG, SARMAG) == 0;
    fclose(f);
    return ok;
}

static void
elf_load_object_image(const char *name, unsigned char *data, unsigned size)
{
    struct elf_input *in;
    int i, secidx;

    ELF_RESERVE(einput, ceinput, neinput + 1, ELF_MAX_INPUTS,
        "input files");
    in = &einput[neinput++];
    memset(in, 0, sizeof(*in));
    in->name = elf_strdup(name);
    in->size = size;
    in->data = data;
    elf_read_ehdr(in);
    in->shnum = in->ehdr.e_shnum;
    in->shdr = calloc(in->shnum, sizeof(Elf32_Shdr));
    in->secmap = calloc(in->shnum, sizeof(int));
    if (!in->shdr || !in->secmap)
        error(2, "out of memory");
    for (i = 0; i < in->shnum; i++) {
        in->shdr[i] = elf_read_shdr(in, i);
        in->secmap[i] = -1;
    }
    if (in->ehdr.e_shstrndx >= (unsigned)in->shnum)
        error(2, "%s: bad shstrndx", name);
    in->shstr = (char *)(in->data + in->shdr[in->ehdr.e_shstrndx].sh_offset);
    for (i = 1; i < in->shnum; i++) {
        Elf32_Shdr *sh = &in->shdr[i];
        if (sh->sh_type == SHT_SYMTAB) {
            int j;
            unsigned char *p;

            in->nsym = sh->sh_size / sizeof(Elf32_Sym);
            in->symtab = calloc(in->nsym, sizeof(Elf32_Sym));
            if (!in->symtab)
                error(2, "out of memory");
            if (sh->sh_link >= (unsigned)in->shnum)
                error(2, "%s: bad symtab link", name);
            in->strtab = (char *)(in->data + in->shdr[sh->sh_link].sh_offset);
            p = in->data + sh->sh_offset;
            for (j = 0; j < in->nsym; j++)
                in->symtab[j] = elf_read_sym(in, p + j * sizeof(Elf32_Sym));
        }
        if (sh->sh_type != SHT_PROGBITS && sh->sh_type != SHT_NOBITS)
            continue;
        if ((sh->sh_flags & SHF_ALLOC) == 0 && sh->sh_size == 0)
            continue;
        ELF_RESERVE(einsec, ceinsec, neinsec + 1, ELF_MAX_INSECS,
            "input sections");
        secidx = neinsec++;
        in->secmap[i] = secidx;
        memset(&einsec[secidx], 0, sizeof(einsec[secidx]));
        einsec[secidx].input = in - einput;
        einsec[secidx].shndx = i;
        einsec[secidx].name = elf_strdup(in->shstr + sh->sh_name);
        einsec[secidx].type = sh->sh_type;
        einsec[secidx].flags = sh->sh_flags;
        einsec[secidx].align = sh->sh_addralign ? sh->sh_addralign : 1;
        einsec[secidx].size = sh->sh_size;
        if (sh->sh_type != SHT_NOBITS)
            einsec[secidx].data = in->data + sh->sh_offset;
    }
}

static void
elf_load_object(char *path)
{
    unsigned char *data;
    unsigned size;

    elf_read_file_image(path, &data, &size);
    elf_load_object_image(path, data, size);
}

static long
elf_ar_atol(const char *s, int n)
{
    char buf[32];

    if (n >= (int)sizeof(buf))
        n = sizeof(buf) - 1;
    elf_copy_bytes(buf, s, n);
    buf[n] = 0;
    return strtol(buf, 0, 10);
}

static void
elf_ar_name(char *dst, int dstlen, struct ar_hdr *hdr,
    unsigned char *base, unsigned size, unsigned *datap, unsigned *objsizep)
{
    int n, i;

    n = sizeof(hdr->ar_name);
    if (n >= dstlen)
        n = dstlen - 1;
    elf_copy_bytes(dst, hdr->ar_name, n);
    dst[n] = 0;
    for (i = n - 1; i >= 0 && dst[i] == ' '; i--)
        dst[i] = 0;

    if (strncmp(hdr->ar_name, AR_EFMT1, sizeof(AR_EFMT1) - 1) == 0) {
        n = atoi(hdr->ar_name + sizeof(AR_EFMT1) - 1);
        if (n <= 0 || (unsigned)n > *objsizep || *datap + (unsigned)n > size)
            error(2, "bad archive long name");
        if (n >= dstlen)
            n = dstlen - 1;
        elf_copy_bytes(dst, base + *datap, n);
        dst[n] = 0;
        *datap += n;
        *objsizep -= n;
    }
}

static int
elf_range_ok(unsigned size, unsigned off, unsigned len)
{
    return off <= size && len <= size - off;
}

static int
elf_member_defines_needed(unsigned char *data, unsigned size)
{
    int big, shnum, shentsize, shoff, i;

    if (size < sizeof(Elf32_Ehdr) ||
        data[0] != ELFMAG0 || data[1] != ELFMAG1 ||
        data[2] != ELFMAG2 || data[3] != ELFMAG3 ||
        data[4] != ELFCLASS32)
        return 0;
    big = data[EI_DATA] == ELFDATA2MSB;
    if (elf_get16p(data + 16, big) != ET_REL ||
        (elf_get16p(data + 18, big) != EM_MIPS &&
         elf_get16p(data + 18, big) != EM_386) ||
        (elf_target_machine &&
         elf_get16p(data + 18, big) != elf_target_machine) ||
        (elf_target_endian_known && big != elf_target_big))
        return 0;
    shoff = elf_get32p(data + 32, big);
    shentsize = elf_get16p(data + 46, big);
    shnum = elf_get16p(data + 48, big);
    if (shentsize < (int)sizeof(Elf32_Shdr) ||
        !elf_range_ok(size, shoff, shnum * shentsize))
        return 0;
    for (i = 0; i < shnum; i++) {
        unsigned char *shp = data + shoff + i * shentsize;
        unsigned type = elf_get32p(shp + 4, big);
        unsigned symoff, symsize, entsize, link;
        unsigned stroff, strsize, n, j;

        if (type != SHT_SYMTAB)
            continue;
        symoff = elf_get32p(shp + 16, big);
        symsize = elf_get32p(shp + 20, big);
        link = elf_get32p(shp + 24, big);
        entsize = elf_get32p(shp + 36, big);
        if (entsize < sizeof(Elf32_Sym) || link >= (unsigned)shnum ||
            !elf_range_ok(size, symoff, symsize))
            continue;
        shp = data + shoff + link * shentsize;
        if (elf_get32p(shp + 4, big) != SHT_STRTAB)
            continue;
        stroff = elf_get32p(shp + 16, big);
        strsize = elf_get32p(shp + 20, big);
        if (!elf_range_ok(size, stroff, strsize))
            continue;
        n = symsize / entsize;
        for (j = 1; j < n; j++) {
            unsigned char *sp = data + symoff + j * entsize;
            unsigned name = elf_get32p(sp + 0, big);
            unsigned shndx = elf_get16p(sp + 14, big);
            int bind = ELF_ST_BIND(sp[12]);
            const char *sname;
            int idx;

            if (name >= strsize || name == 0)
                continue;
            if (bind != STB_GLOBAL && bind != STB_WEAK)
                continue;
            if (shndx == SHN_UNDEF)
                continue;
            sname = (const char *)data + stroff + name;
            idx = elf_find_lsym(sname);
            if (idx >= 0 && !elsym[idx].defined)
                return 1;
        }
    }
    return 0;
}

static void
elf_collect_symbols_from(int first);

static int
elf_load_archive_pass(const char *path, unsigned char *data, unsigned size)
{
    unsigned off;
    int loaded;

    if (!elf_is_archive_image(data, size))
        error(2, "%s: not an archive", path);
    off = SARMAG;
    loaded = 0;
    while (off + sizeof(struct ar_hdr) <= size) {
        struct ar_hdr *hdr = (struct ar_hdr *)(data + off);
        unsigned arsize, dataoff, objoff, objsize, next;
        char name[MAXNAMLEN + 1];

        if (strncmp(hdr->ar_fmag, ARFMAG, sizeof(ARFMAG) - 1) != 0)
            error(2, "%s: bad archive header", path);
        arsize = (unsigned)elf_ar_atol(hdr->ar_size, sizeof(hdr->ar_size));
        dataoff = off + sizeof(struct ar_hdr);
        if (!elf_range_ok(size, dataoff, arsize))
            error(2, "%s: truncated archive member", path);
        objoff = dataoff;
        objsize = arsize;
        elf_ar_name(name, sizeof(name), hdr, data, size, &objoff, &objsize);
        next = dataoff + arsize + (arsize & 1);
        if (next < dataoff || next > size + 1)
            error(2, "%s: bad archive member size", path);
        if (strncmp(name, SYMDEF, sizeof(SYMDEF) - 1) != 0 &&
            elf_member_defines_needed(data + objoff, objsize)) {
            unsigned char *copy;
            char mname[512];
            int first;

            copy = malloc(objsize ? objsize : 1);
            if (!copy)
                error(2, "out of memory");
            if (objsize)
                elf_copy_bytes(copy, data + objoff, objsize);
            snprintf(mname, sizeof(mname), "%s(%s)", path, name);
            first = neinput;
            elf_load_object_image(mname, copy, objsize);
            elf_collect_symbols_from(first);
            loaded = 1;
        }
        off = next;
    }
    return loaded;
}

static void
elf_load_archive(const char *path)
{
    unsigned char *data;
    unsigned size;

    elf_read_file_image(path, &data, &size);
    while (elf_load_archive_pass(path, data, size))
        ;
    free(data);
}

static void
elf_load_archives(void)
{
    int i, loaded, before;

    do {
        loaded = 0;
        for (i = 0; i < nearchive; i++) {
            before = neinput;
            elf_load_archive(earchive[i].path);
            if (neinput != before)
                loaded = 1;
        }
    } while (loaded);
}

static char *
elf_find_library(const char *name)
{
    char *path;
    int i;

    if (*name == '\0')
        error(2, "-l: argument missing");
    for (i = 0; i < nlibdirs; i++) {
        path = makelibpath(libdirs[i], name);
        if (access(path, R_OK) == 0)
            return path;
        free(path);
    }
    for (i = 0; stdlibdirs[i]; i++) {
        path = makesyslibpath(stdlibdirs[i], name);
        if (access(path, R_OK) == 0)
            return path;
        free(path);
    }
    error(2, "cannot find -l%s", name);
    return 0;
}

static void
elf_add_archive(const char *path)
{
    ELF_RESERVE(earchive, cearchive, nearchive + 1, ELF_MAX_ARCHIVES,
        "ELF archives");
    earchive[nearchive++].path = elf_strdup(path);
}

static void
elf_collect_symbols_from(int first)
{
    int i, j, idx, sec;

    for (i = first; i < neinput; i++) {
        struct elf_input *in = &einput[i];
        for (j = 1; j < in->nsym; j++) {
            Elf32_Sym *s = &in->symtab[j];
            const char *name;
            int bind;

            bind = ELF_ST_BIND(s->st_info);
            if (bind == STB_LOCAL || s->st_name == 0)
                continue;
            name = in->strtab + s->st_name;
            idx = elf_add_lsym(name);
            if (s->st_shndx == SHN_UNDEF) {
                if (bind == STB_WEAK)
                    elsym[idx].weak = 1;
                continue;
            }
            if (s->st_shndx == SHN_COMMON) {
                if (!elsym[idx].defined || elsym[idx].common) {
                    elsym[idx].defined = 1;
                    elsym[idx].common = 1;
                    if (s->st_size > elsym[idx].size)
                        elsym[idx].size = s->st_size;
                    if (s->st_value > elsym[idx].align)
                        elsym[idx].align = s->st_value;
                }
                continue;
            }
            if (s->st_shndx == SHN_ABS) {
                elf_define_abs(name, s->st_value);
                continue;
            }
            sec = in->secmap[s->st_shndx];
            if (sec < 0)
                continue;
            if (elsym[idx].defined && !elsym[idx].weak && bind != STB_WEAK)
                error(1, "%s: multiple definition", name);
            if (!elsym[idx].defined ||
                (elsym[idx].weak && bind != STB_WEAK)) {
                elsym[idx].defined = 1;
                elsym[idx].weak = bind == STB_WEAK;
                elsym[idx].common = 0;
                elsym[idx].input = i;
                elsym[idx].symndx = j;
                elsym[idx].insec = sec;
                elsym[idx].value = s->st_value;
                elsym[idx].size = s->st_size;
            }
        }
    }
}

static void
elf_collect_symbols(void)
{
    elf_collect_symbols_from(0);
}

static int
elf_section_matches_stmt(int sec, struct elf_stmt *st)
{
    int i;

    for (i = 0; i < st->npat; i++) {
        struct elf_pattern *p = &epat[st->firstpat + i];
        if (!p->common && elf_match_pattern(p->pat, einsec[sec].name))
            return 1;
    }
    return 0;
}

static int
elf_stmt_has_common(struct elf_stmt *st)
{
    int i;

    for (i = 0; i < st->npat; i++)
        if (epat[st->firstpat + i].common)
            return 1;
    return 0;
}

static void
elf_discard_sections(void)
{
    int p, s;

    if (ediscard_first < 0)
        return;
    for (s = 0; s < neinsec; s++) {
        for (p = ediscard_first; p < ediscard_first + nediscard; p++) {
            if (!epat[p].common && elf_match_pattern(epat[p].pat, einsec[s].name)) {
                einsec[s].discarded = 1;
                break;
            }
        }
    }
}

static void
elf_place_common(struct elf_outsec *out, unsigned *dot)
{
    int i, j;

    /*
     * PCC emits function-local static storage as local SHN_COMMON symbols.
     * They cannot be merged through the global symbol table because equal
     * local names in different input files denote different objects.  Give
     * each one storage when the linker script places COMMON, and turn it
     * into an absolute input value for relocation processing below.
     */
    for (i = 0; i < neinput; i++) {
        struct elf_input *in = &einput[i];

        for (j = 1; j < in->nsym; j++) {
            Elf32_Sym *s = &in->symtab[j];
            unsigned align;

            if (ELF_ST_BIND(s->st_info) != STB_LOCAL ||
                s->st_shndx != SHN_COMMON)
                continue;
            align = s->st_value ? s->st_value : 1;
            *dot = elf_align(*dot, align);
            s->st_value = *dot;
            s->st_shndx = SHN_ABS;
            *dot += s->st_size;
            if (out->align < align)
                out->align = align;
            out->flags |= SHF_ALLOC | SHF_WRITE;
        }
    }

    for (i = 0; i < nelsym; i++) {
        if (!elsym[i].common)
            continue;
        *dot = elf_align(*dot, elsym[i].align ? elsym[i].align : W);
        elsym[i].value = *dot;
        elsym[i].defined = 1;
        elsym[i].common = 0;
        *dot += elsym[i].size;
        if (out->align < elsym[i].align)
            out->align = elsym[i].align;
        out->flags |= SHF_ALLOC | SHF_WRITE;
    }
}

static void
elf_apply_stmt_range(int first, int n, unsigned *dot)
{
    int i;

    for (i = 0; i < n; i++) {
        struct elf_stmt *st = &eglobstmt[first + i];

        if (st->kind == ESTMT_ASSIGN)
            elf_define_abs(st->name, elf_eval_expr(st->expr, *dot));
        else if (st->kind == ESTMT_DOTASSIGN)
            *dot = elf_eval_expr(st->expr, *dot);
    }
}

static void
elf_layout(void)
{
    int i, j, k, m;
    unsigned seqdot;

    seqdot = basaddr;
    for (i = 0; i < neprestmt; i++) {
        if (eprestmt[i].kind == ESTMT_ASSIGN)
            elf_define_abs(eprestmt[i].name,
                elf_eval_expr(eprestmt[i].expr, seqdot));
        else if (eprestmt[i].kind == ESTMT_DOTASSIGN)
            seqdot = elf_eval_expr(eprestmt[i].expr, seqdot);
    }
    for (i = 0; i < neout; i++) {
        struct elf_outsec *out = &eout[i];
        unsigned dot, start;
        int have_content;

        m = -1;
        if (out->region) {
            m = elf_find_mem(out->region);
            if (m < 0)
                error(2, "unknown MEMORY region %s", out->region);
        }
        if (out->has_addr)
            dot = elf_eval_expr(out->addr_expr, seqdot);
        else if (out->region) {
            dot = emem[m].cursor;
        } else
            dot = seqdot;
        start = dot;
        have_content = 0;
        for (j = 0; j < out->nstmt; j++) {
            struct elf_stmt *st = &estmt[out->firststmt + j];

            if (st->kind == ESTMT_ASSIGN) {
                elf_define_abs(st->name, elf_eval_expr(st->expr, dot));
                continue;
            }
            if (st->kind == ESTMT_DOTASSIGN) {
                dot = elf_eval_expr(st->expr, dot);
                continue;
            }
            if (st->kind != ESTMT_INPUT)
                continue;
            for (k = 0; k < neinsec; k++) {
                if (einsec[k].placed || einsec[k].discarded)
                    continue;
                if (!elf_section_matches_stmt(k, st))
                    continue;
                dot = elf_align(dot, einsec[k].align);
                if (!have_content) {
                    start = dot;
                    have_content = 1;
                }
                einsec[k].placed = 1;
                einsec[k].out = i;
                einsec[k].outoff = dot - start;
                einsec[k].addr = dot;
                dot += einsec[k].size;
                out->flags |= einsec[k].flags;
                if (out->align < einsec[k].align)
                    out->align = einsec[k].align;
            }
            if (elf_stmt_has_common(st))
                elf_place_common(out, &dot);
        }
        out->addr = start;
        out->size = dot - start;
        out->type = out->noload ? SHT_NOBITS : SHT_PROGBITS;
        if (out->region)
            emem[m].cursor = dot;
        else
            seqdot = dot;
        elf_last_dot = dot;
        if (out->npost) {
            elf_apply_stmt_range(out->firstpost, out->npost, &dot);
            if (out->region)
                emem[m].cursor = dot;
            else
                seqdot = dot;
            elf_last_dot = dot;
        }
        if (out->region && emem[m].cursor > emem[m].origin + emem[m].length)
            error(2, "MEMORY region %s overflow", emem[m].name);
    }
    for (i = 0; i < neinsec; i++) {
        if (!einsec[i].placed && !einsec[i].discarded &&
            (einsec[i].flags & SHF_ALLOC) && einsec[i].size)
            error(2, "unplaced input section %s", einsec[i].name);
    }
    for (i = 0; i < nelsym; i++) {
        if (elsym[i].defined && elsym[i].insec >= 0) {
            int sec = elsym[i].insec;
            elsym[i].value = einsec[sec].addr + elsym[i].value;
        }
    }
    for (i = 0; i < neassert; i++) {
        if (!elf_eval_expr(eassert[i].expr, elf_last_dot))
            error(2, "%s", eassert[i].message);
    }
}

static unsigned
elf_symbol_value(int input, int symidx)
{
    struct elf_input *in;
    Elf32_Sym *s;
    const char *name;
    int idx, sec;

    in = &einput[input];
    if (symidx <= 0 || symidx >= in->nsym)
        return 0;
    s = &in->symtab[symidx];
    if (s->st_shndx == SHN_ABS)
        return s->st_value;
    if (s->st_shndx == SHN_UNDEF || ELF_ST_BIND(s->st_info) != STB_LOCAL) {
        name = in->strtab + s->st_name;
        idx = elf_find_lsym(name);
        if (idx < 0 || !elsym[idx].defined) {
            if (ELF_ST_BIND(s->st_info) == STB_WEAK)
                return 0;
            error(1, "undefined symbol %s", name);
            return 0;
        }
        return elsym[idx].value;
    }
    sec = in->secmap[s->st_shndx];
    if (sec < 0)
        return s->st_value;
    return einsec[sec].addr + s->st_value;
}

static int
elf_find_lo16(struct elf_input *in, Elf32_Shdr *relsec, int start,
    unsigned symidx, unsigned *lo)
{
    unsigned char *rp;
    int n, i;
    unsigned info, off, type, word;

    n = relsec->sh_size / sizeof(Elf32_Rel);
    rp = in->data + relsec->sh_offset;
    for (i = start + 1; i < n; i++) {
        off = elf_get32p(rp + i * sizeof(Elf32_Rel), in->big);
        info = elf_get32p(rp + i * sizeof(Elf32_Rel) + 4, in->big);
        type = ELF_R_TYPE(info);
        if (ELF_R_SYM(info) == symidx && type == R_MIPS_LO16) {
            if (off + sizeof(word) > in->shdr[relsec->sh_info].sh_size)
                error(2, "%s: bad LO16 relocation offset", in->name);
            word = elf_get32p(in->data + in->shdr[relsec->sh_info].sh_offset +
                off, in->big);
            *lo = word & 0xffff;
            return 1;
        }
    }
    return 0;
}

static void
elf_apply_relocs(void)
{
    int i, r, n;

    for (i = 0; i < neout; i++) {
        if (eout[i].type != SHT_NOBITS && eout[i].size) {
            eout[i].buf = calloc(1, eout[i].size);
            if (!eout[i].buf)
                error(2, "out of memory");
        }
    }
    for (i = 0; i < neinsec; i++) {
        if (!einsec[i].placed)
            continue;
        if (eout[einsec[i].out].type == SHT_NOBITS)
            continue;
        if (einsec[i].type != SHT_NOBITS && einsec[i].size)
            elf_copy_bytes(eout[einsec[i].out].buf + einsec[i].outoff,
                einsec[i].data, einsec[i].size);
    }
    for (i = 0; i < neinput; i++) {
        struct elf_input *in = &einput[i];
        for (r = 0; r < in->shnum; r++) {
            Elf32_Shdr *relsec = &in->shdr[r];
            int target, outidx;
            unsigned char *rp;

            if (relsec->sh_type != SHT_REL)
                continue;
            if (relsec->sh_info >= (unsigned)in->shnum)
                error(2, "%s: bad relocation target", in->name);
            target = in->secmap[relsec->sh_info];
            if (target < 0 || !einsec[target].placed)
                continue;
            outidx = einsec[target].out;
            if (eout[outidx].type == SHT_NOBITS)
                continue;
            rp = in->data + relsec->sh_offset;
            n = relsec->sh_size / sizeof(Elf32_Rel);
            for (int j = 0; j < n; j++) {
                unsigned roff, info, symidx, type, place, word, svalue, aval, loval;
                unsigned char *loc;

                roff = elf_get32p(rp + j * sizeof(Elf32_Rel), in->big);
                info = elf_get32p(rp + j * sizeof(Elf32_Rel) + 4, in->big);
                symidx = ELF_R_SYM(info);
                type = ELF_R_TYPE(info);
                place = einsec[target].addr + roff;
                loc = eout[outidx].buf + einsec[target].outoff + roff;
                svalue = elf_symbol_value(i, symidx);
                if (elf_target_machine == EM_386) {
                    int gotidx;
                    unsigned gotval;

                    switch (type) {
                    case R_386_NONE:
                        break;
                    case R_386_32:
                        word = elf_load32buf(loc);
                        elf_store32buf(loc, word + svalue);
                        break;
                    case R_386_PC32:
                    case R_386_PLT32:
                        word = elf_load32buf(loc);
                        elf_store32buf(loc, word + svalue - place);
                        break;
                    case R_386_GOTOFF:
                    case R_386_GOTPC:
                        gotidx = elf_find_lsym("_GLOBAL_OFFSET_TABLE_");
                        if (gotidx < 0 || !elsym[gotidx].defined)
                            error(2, "%s: _GLOBAL_OFFSET_TABLE_ is undefined",
                                in->name);
                        gotval = elsym[gotidx].value;
                        word = elf_load32buf(loc);
                        if (type == R_386_GOTOFF)
                            elf_store32buf(loc, word + svalue - gotval);
                        else
                            elf_store32buf(loc, word + gotval - place);
                        break;
                    case R_386_16:
                        word = elf_load16buf(loc);
                        elf_store16buf(loc, word + svalue);
                        break;
                    case R_386_PC16:
                        word = elf_load16buf(loc);
                        elf_store16buf(loc, word + svalue - place);
                        break;
                    case R_386_8:
                        loc[0] += svalue;
                        break;
                    case R_386_PC8:
                        loc[0] += svalue - place;
                        break;
                    case R_386_GOT32:
                        error(2, "%s: R_386_GOT32 requires a dynamic GOT",
                            in->name);
                        break;
                    default:
                        error(2, "unsupported i386 relocation %u", type);
                    }
                    continue;
                }
                word = elf_load32buf(loc);
                switch (type) {
                case R_MIPS_NONE:
                    break;
                case R_MIPS_32:
                    word += svalue;
                    elf_store32buf(loc, word);
                    break;
                case R_MIPS_26:
                    aval = (word & 0x03ffffff) << 2;
                    word &= ~0x03ffffff;
                    word |= ((svalue + aval) >> 2) & 0x03ffffff;
                    elf_store32buf(loc, word);
                    break;
                case R_MIPS_HI16:
                    loval = 0;
                    elf_find_lo16(in, relsec, j, symidx, &loval);
                    aval = ((word & 0xffff) << 16) +
                        (int)(short)(loval & 0xffff);
                    word &= ~0xffff;
                    word |= ((svalue + aval + 0x8000) >> 16) & 0xffff;
                    elf_store32buf(loc, word);
                    break;
                case R_MIPS_LO16:
                    aval = (short)(word & 0xffff);
                    word &= ~0xffff;
                    word |= (svalue + aval) & 0xffff;
                    elf_store32buf(loc, word);
                    break;
                case R_MIPS_PC16:
                    aval = (short)(word & 0xffff);
                    aval <<= 2;
                    word &= ~0xffff;
                    word |= ((svalue + aval - place - 4) >> 2) & 0xffff;
                    elf_store32buf(loc, word);
                    break;
                case R_MIPS_GPREL16:
                    aval = (short)(word & 0xffff);
                    {
                        int gpidx = elf_find_lsym("_gp");
                        unsigned gpval = gpidx >= 0 ? elsym[gpidx].value : 0;
                        word &= ~0xffff;
                        word |= (svalue + aval - gpval) & 0xffff;
                    }
                    elf_store32buf(loc, word);
                    break;
                case R_MIPS_GPREL32:
                    {
                        int gpidx = elf_find_lsym("_gp");
                        unsigned gpval = gpidx >= 0 ? elsym[gpidx].value : 0;
                        word = word + svalue - gpval;
                    }
                    elf_store32buf(loc, word);
                    break;
                default:
                    error(2, "unsupported MIPS relocation %u", type);
                }
            }
        }
    }
}

struct wstrtab {
    char *data;
    unsigned len;
    unsigned cap;
};

static void
wstr_init(struct wstrtab *t)
{
    t->cap = 256;
    t->data = malloc(t->cap);
    if (!t->data)
        error(2, "out of memory");
    t->data[0] = 0;
    t->len = 1;
}

static unsigned
wstr_add(struct wstrtab *t, const char *s)
{
    unsigned off, len;
    unsigned i;
    char *p;

    off = t->len;
    len = elf_strlen(s) + 1;
    if (t->len + len > t->cap) {
        while (t->len + len > t->cap)
            t->cap *= 2;
        p = realloc(t->data, t->cap);
        if (!p)
            error(2, "out of memory");
        t->data = p;
    }
    for (i = 0; i < len; i++)
        t->data[t->len + i] = s[i];
    t->len += len;
    return off;
}

static void
elf_write_ehdr_file(FILE *f, Elf32_Ehdr *h)
{
    fwrite(h->e_ident, 1, 16, f);
    aout_put16(h->e_type, f);
    aout_put16(h->e_machine, f);
    aout_put32(h->e_version, f);
    aout_put32(h->e_entry, f);
    aout_put32(h->e_phoff, f);
    aout_put32(h->e_shoff, f);
    aout_put32(h->e_flags, f);
    aout_put16(h->e_ehsize, f);
    aout_put16(h->e_phentsize, f);
    aout_put16(h->e_phnum, f);
    aout_put16(h->e_shentsize, f);
    aout_put16(h->e_shnum, f);
    aout_put16(h->e_shstrndx, f);
}

static void
elf_write_phdr_file(FILE *f, Elf32_Phdr *p)
{
    aout_put32(p->p_type, f);
    aout_put32(p->p_offset, f);
    aout_put32(p->p_vaddr, f);
    aout_put32(p->p_paddr, f);
    aout_put32(p->p_filesz, f);
    aout_put32(p->p_memsz, f);
    aout_put32(p->p_flags, f);
    aout_put32(p->p_align, f);
}

static void
elf_write_shdr_file(FILE *f, Elf32_Shdr *s)
{
    aout_put32(s->sh_name, f);
    aout_put32(s->sh_type, f);
    aout_put32(s->sh_flags, f);
    aout_put32(s->sh_addr, f);
    aout_put32(s->sh_offset, f);
    aout_put32(s->sh_size, f);
    aout_put32(s->sh_link, f);
    aout_put32(s->sh_info, f);
    aout_put32(s->sh_addralign, f);
    aout_put32(s->sh_entsize, f);
}

static void
elf_write_sym_file(FILE *f, Elf32_Sym *s)
{
    aout_put32(s->st_name, f);
    aout_put32(s->st_value, f);
    aout_put32(s->st_size, f);
    putc(s->st_info, f);
    putc(s->st_other, f);
    aout_put16(s->st_shndx, f);
}

static void elf_file_pad(FILE *f, unsigned *pos, unsigned to);

struct elf_reloc_outrel {
    unsigned offset;
    unsigned info;
};

struct elf_reloc_outsec {
    char *name;
    unsigned type;
    unsigned flags;
    unsigned align;
    unsigned size;
    unsigned fileoff;
    unsigned shndx;
    unsigned relshndx;
    unsigned char *buf;
    struct elf_reloc_outrel *rel;
    int nrel;
    int crel;
};

static void
elf_reserve_reloc_symbols(Elf32_Sym **symp, char ***namep, int *cap, int need)
{
    Elf32_Sym *newsym;
    char **newname;
    int oldcap, newcap;

    if (*cap >= need)
        return;
    oldcap = *cap;
    newcap = oldcap ? oldcap : 64;
    while (newcap < need) {
        if (newcap > ELF_MAX_SYMS / 2) {
            newcap = ELF_MAX_SYMS;
            break;
        }
        newcap *= 2;
    }
    if (newcap < need)
        error(2, "too many ELF output symbols");
    newsym = realloc(*symp, (size_t)newcap * sizeof(**symp));
    newname = realloc(*namep, (size_t)newcap * sizeof(**namep));
    if (!newsym || !newname)
        error(2, "out of memory");
    memset(newsym + oldcap, 0, (size_t)(newcap - oldcap) * sizeof(*newsym));
    memset(newname + oldcap, 0, (size_t)(newcap - oldcap) * sizeof(*newname));
    *symp = newsym;
    *namep = newname;
    *cap = newcap;
}

static int
elf_add_reloc_symbol(Elf32_Sym **symp, char ***namep, int *n, int *cap,
    Elf32_Sym *sym, const char *name)
{
    int idx;

    if (*n >= ELF_MAX_SYMS)
        error(2, "too many ELF output symbols");
    elf_reserve_reloc_symbols(symp, namep, cap, *n + 1);
    idx = (*n)++;
    (*symp)[idx] = *sym;
    (*namep)[idx] = name && name[0] ? elf_strdup(name) : 0;
    return idx;
}

static int
elf_reloc_find_outsec(struct elf_reloc_outsec *out, int nout,
    const char *name, unsigned type, unsigned flags)
{
    int i;

    for (i = 0; i < nout; i++) {
        if (strcmp(out[i].name, name) != 0)
            continue;
        if (out[i].type != type || out[i].flags != flags)
            error(2, "incompatible input section %s", name);
        return i;
    }
    return -1;
}

static void
elf_reloc_add_relocation(struct elf_reloc_outsec *out, unsigned offset,
    unsigned info)
{
    struct elf_reloc_outrel *newrel;
    int newcap;

    if (out->nrel >= out->crel) {
        newcap = out->crel ? out->crel * 2 : 32;
        newrel = realloc(out->rel, (size_t)newcap * sizeof(*out->rel));
        if (!newrel)
            error(2, "out of memory");
        out->rel = newrel;
        out->crel = newcap;
    }
    out->rel[out->nrel].offset = offset;
    out->rel[out->nrel].info = info;
    out->nrel++;
}

static unsigned
elf_reloc_output_flags(void)
{
    if (elf_target_machine == EM_386)
        return 0;
    return EF_MIPS_NOREORDER | EF_MIPS_ABI_O32 |
#ifdef TARGET_VR4300
        EF_MIPS_ARCH_3;
#else
        EF_MIPS_ARCH_32R2;
#endif
}

static void
elf_write_relocatable(void)
{
    struct elf_reloc_outsec *out = 0;
    Elf32_Sym *osym = 0;
    Elf32_Shdr *sh = 0;
    char **osymname = 0;
    struct wstrtab shstr, str;
    FILE *f;
    Elf32_Ehdr eh;
    Elf32_Sym sym;
    int nout = 0, cout = 0, nosym = 0, cosym = 0;
    int i, j, idx, first_global, symtab_idx, strtab_idx, shstr_idx;
    unsigned shnum, pos, off;

    for (i = 0; i < neinsec; i++) {
        struct elf_insec *s = &einsec[i];
        unsigned aligned;

        idx = elf_reloc_find_outsec(out, nout, s->name, s->type, s->flags);
        if (idx < 0) {
            ELF_RESERVE(out, cout, nout + 1, ELF_MAX_OUTSECS,
                "ELF output sections");
            idx = nout++;
            memset(&out[idx], 0, sizeof(out[idx]));
            out[idx].name = elf_strdup(s->name);
            out[idx].type = s->type;
            out[idx].flags = s->flags;
            out[idx].align = s->align ? s->align : 1;
            out[idx].shndx = idx + 1;
        }
        if (out[idx].align < s->align)
            out[idx].align = s->align;
        aligned = elf_align(out[idx].size, s->align ? s->align : 1);
        s->out = idx;
        s->outoff = aligned;
        s->addr = aligned;
        s->placed = 1;
        out[idx].size = aligned + s->size;
    }
    for (i = 0; i < nout; i++) {
        if (out[i].type == SHT_NOBITS || out[i].size == 0)
            continue;
        out[i].buf = calloc(1, out[i].size);
        if (!out[i].buf)
            error(2, "out of memory");
    }
    for (i = 0; i < neinsec; i++) {
        struct elf_insec *s = &einsec[i];

        if (s->type == SHT_NOBITS || s->size == 0)
            continue;
        elf_copy_bytes(out[s->out].buf + s->outoff, s->data, s->size);
    }

    memset(&sym, 0, sizeof(sym));
    elf_add_reloc_symbol(&osym, &osymname, &nosym, &cosym, &sym, 0);
    for (i = 0; i < neinput; i++) {
        struct elf_input *in = &einput[i];

        in->symmap = calloc(in->nsym ? in->nsym : 1, sizeof(int));
        if (!in->symmap)
            error(2, "out of memory");
        for (j = 1; j < in->nsym; j++) {
            Elf32_Sym *s = &in->symtab[j];
            int bind = ELF_ST_BIND(s->st_info);
            int sec;

            if (bind != STB_LOCAL)
                continue;
            memset(&sym, 0, sizeof(sym));
            sym = *s;
            if (s->st_shndx == SHN_COMMON) {
                /* Preserve local commons for a later final link. */
            } else if (s->st_shndx < (unsigned)in->shnum &&
                (sec = in->secmap[s->st_shndx]) >= 0) {
                sym.st_shndx = out[einsec[sec].out].shndx;
                sym.st_value = s->st_value + einsec[sec].outoff;
            } else if (s->st_shndx != SHN_ABS && s->st_shndx != SHN_UNDEF) {
                in->symmap[j] = 0;
                continue;
            }
            in->symmap[j] = elf_add_reloc_symbol(&osym, &osymname,
                &nosym, &cosym, &sym,
                s->st_name ? in->strtab + s->st_name : 0);
        }
    }
    first_global = nosym;
    for (i = 0; i < nelsym; i++) {
        memset(&sym, 0, sizeof(sym));
        sym.st_name = 0;
        sym.st_size = elsym[i].size;
        sym.st_info = ELF_ST_INFO(elsym[i].weak ? STB_WEAK : STB_GLOBAL,
            STT_NOTYPE);
        if (!elsym[i].defined) {
            sym.st_shndx = SHN_UNDEF;
        } else if (elsym[i].common) {
            sym.st_shndx = SHN_COMMON;
            sym.st_value = elsym[i].align;
            sym.st_size = elsym[i].size;
        } else if (elsym[i].insec >= 0) {
            int sec = elsym[i].insec;

            sym.st_shndx = out[einsec[sec].out].shndx;
            sym.st_value = elsym[i].value + einsec[sec].outoff;
        } else {
            sym.st_shndx = SHN_ABS;
            sym.st_value = elsym[i].value;
        }
        idx = elf_add_reloc_symbol(&osym, &osymname, &nosym, &cosym,
            &sym, elsym[i].name);
        for (j = 0; j < neinput; j++) {
            struct elf_input *in = &einput[j];
            int k;

            for (k = 1; k < in->nsym; k++) {
                Elf32_Sym *s = &in->symtab[k];

                if (ELF_ST_BIND(s->st_info) == STB_LOCAL || s->st_name == 0)
                    continue;
                if (elf_streq(in->strtab + s->st_name, elsym[i].name))
                    in->symmap[k] = idx;
            }
        }
    }

    for (i = 0; i < neinput; i++) {
        struct elf_input *in = &einput[i];

        for (j = 0; j < in->shnum; j++) {
            Elf32_Shdr *relsec = &in->shdr[j];
            unsigned char *rp;
            int nrel, target;

            if (relsec->sh_type != SHT_REL)
                continue;
            if (relsec->sh_info >= (unsigned)in->shnum)
                error(2, "%s: bad relocation target", in->name);
            target = in->secmap[relsec->sh_info];
            if (target < 0)
                continue;
            rp = in->data + relsec->sh_offset;
            nrel = relsec->sh_size / sizeof(Elf32_Rel);
            for (idx = 0; idx < nrel; idx++) {
                unsigned roff, info, symidx, type, outsym;

                roff = elf_get32p(rp + idx * sizeof(Elf32_Rel), in->big);
                info = elf_get32p(rp + idx * sizeof(Elf32_Rel) + 4, in->big);
                symidx = ELF_R_SYM(info);
                type = ELF_R_TYPE(info);
                outsym = symidx < (unsigned)in->nsym ? in->symmap[symidx] : 0;
                if (symidx != 0 && outsym == 0)
                    error(2, "%s: bad relocation symbol", in->name);
                elf_reloc_add_relocation(&out[einsec[target].out],
                    einsec[target].outoff + roff, ELF_R_INFO(outsym, type));
            }
        }
    }
    shnum = 1 + nout;
    for (i = 0; i < nout; i++)
        if (out[i].nrel)
            out[i].relshndx = shnum++;
    symtab_idx = shnum++;
    strtab_idx = shnum++;
    shstr_idx = shnum++;
    sh = calloc(shnum, sizeof(*sh));
    if (!sh)
        error(2, "out of memory");
    wstr_init(&shstr);
    wstr_init(&str);

    off = sizeof(Elf32_Ehdr);
    for (i = 0; i < nout; i++) {
        unsigned align = out[i].align ? out[i].align : 1;

        sh[out[i].shndx].sh_name = wstr_add(&shstr, out[i].name);
        sh[out[i].shndx].sh_type = out[i].type;
        sh[out[i].shndx].sh_flags = out[i].flags;
        sh[out[i].shndx].sh_addralign = align;
        sh[out[i].shndx].sh_size = out[i].size;
        off = elf_align(off, align);
        sh[out[i].shndx].sh_offset = off;
        out[i].fileoff = off;
        if (out[i].type != SHT_NOBITS)
            off += out[i].size;
    }
    for (i = 0; i < nout; i++) {
        char relname[300];
        unsigned rsize;

        if (!out[i].nrel)
            continue;
        snprintf(relname, sizeof(relname), ".rel%s", out[i].name);
        rsize = out[i].nrel * sizeof(Elf32_Rel);
        off = elf_align(off, W);
        sh[out[i].relshndx].sh_name = wstr_add(&shstr, relname);
        sh[out[i].relshndx].sh_type = SHT_REL;
        sh[out[i].relshndx].sh_offset = off;
        sh[out[i].relshndx].sh_size = rsize;
        sh[out[i].relshndx].sh_link = symtab_idx;
        sh[out[i].relshndx].sh_info = out[i].shndx;
        sh[out[i].relshndx].sh_addralign = W;
        sh[out[i].relshndx].sh_entsize = sizeof(Elf32_Rel);
        off += rsize;
    }
    for (i = 0; i < nosym; i++)
        if (osymname[i])
            osym[i].st_name = wstr_add(&str, osymname[i]);
    off = elf_align(off, W);
    sh[symtab_idx].sh_name = wstr_add(&shstr, ".symtab");
    sh[symtab_idx].sh_type = SHT_SYMTAB;
    sh[symtab_idx].sh_offset = off;
    sh[symtab_idx].sh_size = nosym * sizeof(Elf32_Sym);
    sh[symtab_idx].sh_link = strtab_idx;
    sh[symtab_idx].sh_info = first_global;
    sh[symtab_idx].sh_addralign = W;
    sh[symtab_idx].sh_entsize = sizeof(Elf32_Sym);
    off += sh[symtab_idx].sh_size;

    sh[strtab_idx].sh_name = wstr_add(&shstr, ".strtab");
    sh[strtab_idx].sh_type = SHT_STRTAB;
    sh[strtab_idx].sh_offset = off;
    sh[strtab_idx].sh_size = str.len;
    sh[strtab_idx].sh_addralign = 1;
    off += str.len;

    sh[shstr_idx].sh_name = wstr_add(&shstr, ".shstrtab");
    sh[shstr_idx].sh_type = SHT_STRTAB;
    sh[shstr_idx].sh_offset = off;
    sh[shstr_idx].sh_size = shstr.len;
    sh[shstr_idx].sh_addralign = 1;
    off += shstr.len;
    off = elf_align(off, W);

    memset(&eh, 0, sizeof(eh));
    eh.e_ident[0] = ELFMAG0;
    eh.e_ident[1] = ELFMAG1;
    eh.e_ident[2] = ELFMAG2;
    eh.e_ident[3] = ELFMAG3;
    eh.e_ident[4] = ELFCLASS32;
    eh.e_ident[EI_DATA] = elf_target_big ? ELFDATA2MSB : ELFDATA2LSB;
    eh.e_ident[6] = EV_CURRENT;
    eh.e_type = ET_REL;
    eh.e_machine = elf_target_machine;
    eh.e_version = EV_CURRENT;
    eh.e_shoff = off;
    eh.e_flags = elf_reloc_output_flags();
    eh.e_ehsize = sizeof(Elf32_Ehdr);
    eh.e_shentsize = sizeof(Elf32_Shdr);
    eh.e_shnum = shnum;
    eh.e_shstrndx = shstr_idx;

    f = fopen(ofilename, "w+");
    if (!f)
        error(2, "cannot create output file");
    elf_write_ehdr_file(f, &eh);
    pos = sizeof(Elf32_Ehdr);
    for (i = 0; i < nout; i++) {
        if (out[i].type == SHT_NOBITS || out[i].size == 0)
            continue;
        elf_file_pad(f, &pos, out[i].fileoff);
        fwrite(out[i].buf, 1, out[i].size, f);
        pos += out[i].size;
    }
    for (i = 0; i < nout; i++) {
        if (!out[i].nrel)
            continue;
        elf_file_pad(f, &pos, sh[out[i].relshndx].sh_offset);
        for (j = 0; j < out[i].nrel; j++) {
            aout_put32(out[i].rel[j].offset, f);
            aout_put32(out[i].rel[j].info, f);
            pos += sizeof(Elf32_Rel);
        }
    }
    elf_file_pad(f, &pos, sh[symtab_idx].sh_offset);
    for (i = 0; i < nosym; i++) {
        elf_write_sym_file(f, &osym[i]);
        pos += sizeof(Elf32_Sym);
    }
    elf_file_pad(f, &pos, sh[strtab_idx].sh_offset);
    fwrite(str.data, 1, str.len, f);
    pos += str.len;
    elf_file_pad(f, &pos, sh[shstr_idx].sh_offset);
    fwrite(shstr.data, 1, shstr.len, f);
    pos += shstr.len;
    elf_file_pad(f, &pos, eh.e_shoff);
    for (i = 0; i < (int)shnum; i++)
        elf_write_shdr_file(f, &sh[i]);
    fclose(f);
}

static void
elf_file_pad(FILE *f, unsigned *pos, unsigned to)
{
    while (*pos < to) {
        putc(0, f);
        (*pos)++;
    }
}

static unsigned
elf_phdr_flags(unsigned shflags)
{
    unsigned f;

    f = PF_R;
    if (shflags & SHF_WRITE)
        f |= PF_W;
    if (shflags & SHF_EXECINSTR)
        f |= PF_X;
    return f;
}

static int
elf_find_script_phdr(const char *name)
{
    int i;

    for (i = 0; i < nephdr; i++)
        if (strcmp(ephdr[i].name, name) == 0)
            return i;
    return -1;
}

static void
elf_write_output(void)
{
    FILE *f;
    Elf32_Ehdr eh;
    Elf32_Phdr *ph;
    Elf32_Shdr *sh;
    Elf32_Sym sym;
    struct wstrtab shstr, str;
    unsigned *ph_fileend, *ph_memend;
    unsigned phnum, shnum, pos, off, symtab_idx, strtab_idx, shstr_idx;
    unsigned nsyms, first_global, entry;
    int i, idx;

    ph = calloc(neout ? neout : 1, sizeof(*ph));
    sh = calloc((neout ? neout : 1) + 4, sizeof(*sh));
    ph_fileend = calloc(neout ? neout : 1, sizeof(*ph_fileend));
    ph_memend = calloc(neout ? neout : 1, sizeof(*ph_memend));
    if (!ph || !sh || !ph_fileend || !ph_memend)
        error(2, "out of memory");
    wstr_init(&shstr);
    wstr_init(&str);
    for (i = 0; i < nephdr; i++)
        ephdr[i].phndx = -1;
    phnum = 0;
    for (i = 0; i < neout; i++) {
        if ((eout[i].flags & SHF_ALLOC) && eout[i].size) {
            if (eout[i].phdr) {
                idx = elf_find_script_phdr(eout[i].phdr);
                if (idx < 0)
                    error(2, "unknown program header %s", eout[i].phdr);
                if (ephdr[idx].phndx < 0) {
                    ephdr[idx].phndx = phnum;
                    memset(&ph[phnum], 0, sizeof(ph[phnum]));
                    ph[phnum].p_type = PT_LOAD;
                    ph[phnum].p_flags = ephdr[idx].flags;
                    ph[phnum].p_align = ELF_LOAD_ALIGN;
                    phnum++;
                }
                eout[i].phndx = ephdr[idx].phndx;
                if (eout[i].align > ph[eout[i].phndx].p_align)
                    ph[eout[i].phndx].p_align = eout[i].align;
            } else {
                eout[i].phndx = phnum;
                memset(&ph[phnum], 0, sizeof(ph[phnum]));
                ph[phnum].p_type = PT_LOAD;
                ph[phnum].p_flags = elf_phdr_flags(eout[i].flags);
                ph[phnum].p_align = eout[i].align > ELF_LOAD_ALIGN ?
                    eout[i].align : ELF_LOAD_ALIGN;
                phnum++;
            }
        } else
            eout[i].phndx = -1;
    }

    off = sizeof(Elf32_Ehdr) + phnum * sizeof(Elf32_Phdr);
    for (i = 0; i < neout; i++) {
        unsigned align;

        if (eout[i].size == 0)
            continue;
        align = eout[i].align ? eout[i].align : W;
        if (eout[i].phndx >= 0) {
            align = ph[eout[i].phndx].p_align;
            off = elf_align_mod(off, align, eout[i].addr);
        } else
            off = elf_align(off, align);
        eout[i].fileoff = off;
        if (eout[i].type == SHT_NOBITS)
            continue;
        off += eout[i].size;
    }
    for (i = 0; i < (int)phnum; i++) {
        ph[i].p_offset = ~0U;
        ph[i].p_vaddr = ~0U;
        ph[i].p_paddr = ~0U;
        ph_fileend[i] = 0;
        ph_memend[i] = 0;
    }
    for (i = 0; i < neout; i++) {
        unsigned end;

        if (eout[i].phndx < 0)
            continue;
        idx = eout[i].phndx;
        if (eout[i].addr < ph[idx].p_vaddr) {
            ph[idx].p_vaddr = eout[i].addr;
            ph[idx].p_paddr = eout[i].addr;
        }
        end = eout[i].addr + eout[i].size;
        if (end > ph_memend[idx])
            ph_memend[idx] = end;
        if (eout[i].type != SHT_NOBITS) {
            if (eout[i].fileoff < ph[idx].p_offset)
                ph[idx].p_offset = eout[i].fileoff;
            end = eout[i].fileoff + eout[i].size;
            if (end > ph_fileend[idx])
                ph_fileend[idx] = end;
        }
    }
    for (i = 0; i < (int)phnum; i++) {
        if (ph[i].p_offset == ~0U)
            ph[i].p_offset = 0;
        ph[i].p_filesz = ph_fileend[i] > ph[i].p_offset ?
            ph_fileend[i] - ph[i].p_offset : 0;
        ph[i].p_memsz = ph_memend[i] > ph[i].p_vaddr ?
            ph_memend[i] - ph[i].p_vaddr : 0;
    }

    shnum = 1;
    for (i = 0; i < neout; i++) {
        if (eout[i].size == 0)
            continue;
        eout[i].shndx = shnum;
        sh[shnum].sh_name = wstr_add(&shstr, eout[i].name);
        sh[shnum].sh_type = eout[i].type;
        sh[shnum].sh_flags = eout[i].flags;
        sh[shnum].sh_addr = eout[i].addr;
        sh[shnum].sh_offset = eout[i].fileoff;
        sh[shnum].sh_size = eout[i].size;
        sh[shnum].sh_addralign = eout[i].align ? eout[i].align : W;
        shnum++;
    }
    first_global = 1 + shnum - 1;
    nsyms = first_global;
    for (i = 0; i < nelsym; i++)
        if (elsym[i].defined)
            nsyms++;
    symtab_idx = shnum++;
    strtab_idx = shnum++;
    shstr_idx = shnum++;

    off = elf_align(off, W);
    sh[symtab_idx].sh_name = wstr_add(&shstr, ".symtab");
    sh[symtab_idx].sh_type = SHT_SYMTAB;
    sh[symtab_idx].sh_offset = off;
    sh[symtab_idx].sh_size = nsyms * sizeof(Elf32_Sym);
    sh[symtab_idx].sh_link = strtab_idx;
    sh[symtab_idx].sh_info = first_global;
    sh[symtab_idx].sh_addralign = W;
    sh[symtab_idx].sh_entsize = sizeof(Elf32_Sym);
    off += sh[symtab_idx].sh_size;

    for (i = 0; i < nelsym; i++)
        if (elsym[i].defined)
            wstr_add(&str, elsym[i].name);
    sh[strtab_idx].sh_name = wstr_add(&shstr, ".strtab");
    sh[strtab_idx].sh_type = SHT_STRTAB;
    sh[strtab_idx].sh_offset = off;
    sh[strtab_idx].sh_size = str.len;
    sh[strtab_idx].sh_addralign = 1;
    off += str.len;

    sh[shstr_idx].sh_name = wstr_add(&shstr, ".shstrtab");
    sh[shstr_idx].sh_type = SHT_STRTAB;
    sh[shstr_idx].sh_offset = off;
    sh[shstr_idx].sh_size = shstr.len;
    sh[shstr_idx].sh_addralign = 1;
    off += shstr.len;
    off = elf_align(off, W);

    entry = 0;
    if (elf_entry_symbol) {
        idx = elf_find_lsym(elf_entry_symbol);
        if (idx >= 0 && elsym[idx].defined)
            entry = elsym[idx].value;
    }

    memset(&eh, 0, sizeof(eh));
    eh.e_ident[0] = ELFMAG0;
    eh.e_ident[1] = ELFMAG1;
    eh.e_ident[2] = ELFMAG2;
    eh.e_ident[3] = ELFMAG3;
    eh.e_ident[4] = ELFCLASS32;
    eh.e_ident[EI_DATA] = elf_target_big ? ELFDATA2MSB : ELFDATA2LSB;
    eh.e_ident[6] = EV_CURRENT;
    eh.e_type = ET_EXEC;
    eh.e_machine = elf_target_machine;
    eh.e_version = EV_CURRENT;
    eh.e_entry = entry;
    eh.e_phoff = sizeof(Elf32_Ehdr);
    eh.e_shoff = off;
    eh.e_flags = elf_reloc_output_flags();
    eh.e_ehsize = sizeof(Elf32_Ehdr);
    eh.e_phentsize = sizeof(Elf32_Phdr);
    eh.e_phnum = phnum;
    eh.e_shentsize = sizeof(Elf32_Shdr);
    eh.e_shnum = shnum;
    eh.e_shstrndx = shstr_idx;

    f = fopen(ofilename, "w+");
    if (!f)
        error(2, "cannot create output file");
    elf_write_ehdr_file(f, &eh);
    pos = sizeof(Elf32_Ehdr);
    for (i = 0; i < (int)phnum; i++) {
        elf_write_phdr_file(f, &ph[i]);
        pos += sizeof(Elf32_Phdr);
    }
    for (i = 0; i < neout; i++) {
        if (eout[i].type == SHT_NOBITS || eout[i].size == 0)
            continue;
        elf_file_pad(f, &pos, eout[i].fileoff);
        fwrite(eout[i].buf, 1, eout[i].size, f);
        pos += eout[i].size;
    }
    elf_file_pad(f, &pos, sh[symtab_idx].sh_offset);
    memset(&sym, 0, sizeof(sym));
    elf_write_sym_file(f, &sym);
    for (i = 0; i < neout; i++) {
        if (!eout[i].shndx)
            continue;
        memset(&sym, 0, sizeof(sym));
        sym.st_info = ELF_ST_INFO(STB_LOCAL, STT_SECTION);
        sym.st_shndx = eout[i].shndx;
        elf_write_sym_file(f, &sym);
    }
    {
        unsigned stroff = 1;

        for (i = 0; i < nelsym; i++) {
            if (!elsym[i].defined)
                continue;
            memset(&sym, 0, sizeof(sym));
            sym.st_name = stroff;
            sym.st_value = elsym[i].value;
            sym.st_size = elsym[i].size;
            sym.st_info = ELF_ST_INFO(STB_GLOBAL, STT_NOTYPE);
            sym.st_shndx = SHN_ABS;
            for (int o = 0; o < neout; o++) {
                if (elsym[i].value >= eout[o].addr &&
                    elsym[i].value < eout[o].addr + eout[o].size) {
                    sym.st_shndx = eout[o].shndx;
                    break;
                }
            }
            elf_write_sym_file(f, &sym);
            stroff += elf_strlen(elsym[i].name) + 1;
        }
    }
    pos += sh[symtab_idx].sh_size;
    elf_file_pad(f, &pos, sh[strtab_idx].sh_offset);
    fwrite(str.data, 1, str.len, f);
    pos += str.len;
    elf_file_pad(f, &pos, sh[shstr_idx].sh_offset);
    fwrite(shstr.data, 1, shstr.len, f);
    pos += shstr.len;
    elf_file_pad(f, &pos, eh.e_shoff);
    for (i = 0; i < (int)shnum; i++)
        elf_write_shdr_file(f, &sh[i]);
    fclose(f);
    chmod_executable_output();
    free(ph_memend);
    free(ph_fileend);
    free(sh);
    free(ph);
}

static int
elf_arg_takes_value(char opt)
{
    return opt == 'o' || opt == 'e' || opt == 'T' || opt == 'L' || opt == 'u';
}

static char *
elf_join_path(const char *left, const char *right)
{
    char *path;
    size_t leftlen, rightlen, rightoff;
    int need_slash;

    if (!left)
        left = "";
    if (!right)
        right = "";
    leftlen = strlen(left);
    rightlen = strlen(right);
    rightoff = 0;
    need_slash = 0;
    if (leftlen && rightlen) {
        if (left[leftlen - 1] == '/' && right[0] == '/')
            rightoff = 1;
        else if (left[leftlen - 1] != '/' && right[0] != '/')
            need_slash = 1;
    }
    path = malloc(leftlen + need_slash + rightlen - rightoff + 1);
    if (!path)
        error(2, "out of memory");
    elf_copy_bytes(path, left, leftlen);
    if (need_slash)
        path[leftlen++] = '/';
    elf_copy_bytes(path + leftlen, right + rightoff, rightlen - rightoff);
    path[leftlen + rightlen - rightoff] = 0;
    return path;
}

static char *
elf_try_script_path(const char *base, const char *dir, const char *name)
{
    char *tmp, *path;

    tmp = elf_join_path(base, dir);
    path = elf_join_path(tmp, name);
    free(tmp);
    if (access(path, R_OK) == 0)
        return path;
    free(path);
    return 0;
}

static char *
elf_tool_bindir(void)
{
    char *slash, *dir;
    size_t len;

    if (!ld_program)
        return 0;
    slash = strrchr(ld_program, '/');
    if (!slash)
        return 0;
    len = slash - ld_program;
    if (len == 0)
        len = 1;
    dir = malloc(len + 1);
    if (!dir)
        error(2, "out of memory");
    elf_copy_bytes(dir, ld_program, len);
    dir[len] = 0;
    return dir;
}

static char *
elf_tool_target(void)
{
    char *base, *target;
    size_t len;

    if (!ld_program)
        return 0;
    base = strrchr(ld_program, '/');
    base = base ? base + 1 : ld_program;
    len = strlen(base);
    if (len > 3 && strcmp(base + len - 3, "-ld") == 0) {
        target = malloc(len - 2);
        if (!target)
            error(2, "out of memory");
        elf_copy_bytes(target, base, len - 3);
        target[len - 3] = 0;
        return target;
    }
    return savestr(aout_is_big_endian() ? "mips-rebsd" : "mipsel-rebsd");
}

static const char *
elf_endian_target(void)
{
    return aout_is_big_endian() ? "mips-rebsd" : "mipsel-rebsd";
}

static char *
elf_default_script_from_tool(const char *name)
{
    char *bindir, *target, *rel, *path;
    size_t len;

    bindir = elf_tool_bindir();
    if (!bindir)
        return 0;
    if (elf_target_machine == EM_386) {
        path = elf_try_script_path(bindir, "../lib/ldscripts", name);
        free(bindir);
        return path;
    }
    target = savestr(elf_endian_target());
    len = strlen("../") + strlen(target) + strlen("/lib/ldscripts") + 1;
    rel = malloc(len);
    if (!rel)
        error(2, "out of memory");
    strcpy(rel, "../");
    strcat(rel, target);
    strcat(rel, "/lib/ldscripts");
    path = elf_try_script_path(bindir, rel, name);
    free(rel);
    free(target);
    if (!path) {
        target = elf_tool_target();
        if (strcmp(target, elf_endian_target()) != 0) {
            len = strlen("../") + strlen(target) + strlen("/lib/ldscripts") + 1;
            rel = malloc(len);
            if (!rel)
                error(2, "out of memory");
            strcpy(rel, "../");
            strcat(rel, target);
            strcat(rel, "/lib/ldscripts");
            path = elf_try_script_path(bindir, rel, name);
            free(rel);
        }
        free(target);
    }
    if (!path)
        path = elf_try_script_path(bindir, "../lib/ldscripts", name);
    free(bindir);
    return path;
}

static char *
elf_default_script(void)
{
    const char *name, *root;
    char *path;

    if (elf_target_machine == EM_386)
        name = "elf32-i386.ld";
    else
        name = elf_target_big ? "elf32-bigmips.ld" : "elf32-littlemips.ld";
    root = sysroot[0] ? sysroot : "/";
    path = elf_try_script_path(root, "usr/lib/ldscripts", name);
    if (path)
        return path;
    if (strcmp(sysroot, "/") != 0 && strcmp(sysroot, "") != 0) {
        path = elf_try_script_path("/", "usr/lib/ldscripts", name);
        if (path)
            return path;
    }
    path = elf_default_script_from_tool(name);
    if (path)
        return path;
    error(2, "ELF link requires -T script or /usr/lib/ldscripts/%s", name);
    return 0;
}

static void
elf_parse_args(int argc, char **argv)
{
    int i;

    ofilfnd = 0;
    for (i = 1; i < argc; i++) {
        char *a = argv[i];

        if (strcmp(a, "--elf") == 0) {
            elf_mode = 1;
            continue;
        }
        if (strcmp(a, "--aout") == 0)
            continue;
        if (strcmp(a, "-m") == 0) {
            if (++i >= argc)
                error(2, "-m: argument missing");
            elf_select_emulation(argv[i]);
            continue;
        }
        if (strncmp(a, "-m", 2) == 0 && a[2]) {
            elf_select_emulation(a + 2);
            continue;
        }
        if (strcmp(a, "-z") == 0) {
            if (++i >= argc)
                error(2, "-z: argument missing");
            continue;
        }
        if (strncmp(a, "--sysroot=", 10) == 0)
            continue;
        if (strcmp(a, "--sysroot") == 0) {
            if (++i >= argc)
                error(2, "--sysroot: argument missing");
            continue;
        }
        if (strcmp(a, "-nostdlib") == 0 || strcmp(a, "--fatal-warnings") == 0)
            continue;
        if (strcmp(a, "-L") == 0) {
            if (++i >= argc)
                error(2, "-L: argument missing");
            continue;
        }
        if (strncmp(a, "-L", 2) == 0)
            continue;
        if (strcmp(a, "-l") == 0) {
            if (++i >= argc)
                error(2, "-l: argument missing");
            elf_add_archive(elf_find_library(argv[i]));
            continue;
        }
        if (strncmp(a, "-l", 2) == 0) {
            elf_add_archive(elf_find_library(a + 2));
            continue;
        }
        if (strcmp(a, "-T") == 0) {
            if (++i >= argc)
                error(2, "-T: argument missing");
            elf_script_file = argv[i];
            continue;
        }
        if (strncmp(a, "-T", 2) == 0) {
            elf_script_file = a + 2;
            continue;
        }
        if (strcmp(a, "-r") == 0) {
            rflag = 1;
            continue;
        }
        if (a[0] == '-' && strchr(a + 1, 'r') && !elf_arg_takes_value(a[1])) {
            rflag = 1;
            continue;
        }
        if (strcmp(a, "-o") == 0) {
            if (++i >= argc)
                error(2, "-o: argument missing");
            ofilename = argv[i];
            ofilfnd = 1;
            continue;
        }
        if (strcmp(a, "-e") == 0) {
            if (++i >= argc)
                error(2, "-e: argument missing");
            elf_entry_symbol = argv[i];
            continue;
        }
        if (strcmp(a, "-u") == 0) {
            if (++i >= argc)
                error(2, "-u: argument missing");
            elf_add_lsym(argv[i]);
            continue;
        }
        if (strncmp(a, "-u", 2) == 0 && a[2]) {
            elf_add_lsym(a + 2);
            continue;
        }
        if (strcmp(a, "-EL") == 0) {
            elf_select_endian(0, "-EL");
            continue;
        }
        if (strcmp(a, "-EB") == 0) {
            elf_select_endian(1, "-EB");
            continue;
        }
        if (a[0] == '-' && a[1] && !elf_arg_takes_value(a[1]))
            continue;
        if (a[0] == '-' && elf_arg_takes_value(a[1])) {
            if (a[2] == 0)
                i++;
            continue;
        }
        if (elf_is_archive_file(a))
            elf_add_archive(a);
        else
            elf_load_object(a);
    }
    if (!rflag && !elf_script_file)
        elf_script_file = elf_default_script();
}

static int
elf_main(int argc, char **argv)
{
    int i;

    elf_parse_args(argc, argv);
    if (!elf_target_machine)
        error(2, "ELF link has no input machine type; use -m");
    elf_collect_symbols();
    elf_load_archives();
    if (rflag) {
        if (errlev)
            delexit(0);
        elf_write_relocatable();
        return 0;
    }
    script_parse_file(elf_script_file);
    elf_discard_sections();
    elf_layout();
    elf_apply_relocs();
    for (i = 0; i < nelsym; i++) {
        if (!elsym[i].defined && !elsym[i].weak)
            error(1, "undefined symbol %s", elsym[i].name);
    }
    if (errlev)
        delexit(0);
    elf_write_output();
    return 0;
}

enum {
    LD_PROBE_UNKNOWN,
    LD_PROBE_AOUT,
    LD_PROBE_ELF,
};

static int
ld_probe_image(unsigned char *data, unsigned size)
{
    int best, big;
    struct exec ex;

    if (size >= 4 && data[0] == ELFMAG0 && data[1] == ELFMAG1 &&
        data[2] == ELFMAG2 && data[3] == ELFMAG3)
        return LD_PROBE_ELF;
    if (elf_is_archive_image(data, size)) {
        unsigned off = SARMAG;

        best = LD_PROBE_UNKNOWN;
        while (off + sizeof(struct ar_hdr) <= size) {
            struct ar_hdr *hdr = (struct ar_hdr *)(data + off);
            unsigned arsize, dataoff, objoff, objsize, next;
            int kind;

            if (strncmp(hdr->ar_fmag, ARFMAG, sizeof(ARFMAG) - 1) != 0)
                break;
            arsize = (unsigned)elf_ar_atol(hdr->ar_size, sizeof(hdr->ar_size));
            dataoff = off + sizeof(struct ar_hdr);
            if (!elf_range_ok(size, dataoff, arsize))
                break;
            objoff = dataoff;
            objsize = arsize;
            if (strncmp(hdr->ar_name, AR_EFMT1, sizeof(AR_EFMT1) - 1) == 0) {
                unsigned namelen = (unsigned)atoi(hdr->ar_name +
                    sizeof(AR_EFMT1) - 1);
                if (namelen > objsize)
                    break;
                objoff += namelen;
                objsize -= namelen;
            }
            kind = ld_probe_image(data + objoff, objsize);
            if (kind == LD_PROBE_ELF)
                return kind;
            if (kind == LD_PROBE_AOUT)
                best = kind;
            next = dataoff + arsize + (arsize & 1);
            if (next <= off || next > size + 1)
                break;
            off = next;
        }
        return best;
    }
    if (size >= sizeof(struct exec)) {
        big = aout_is_big_endian();
        ex.a_midmag = elf_get32p(data, big);
        if (!N_BADMAG(ex))
            return LD_PROBE_AOUT;
    }
    return LD_PROBE_UNKNOWN;
}

static int
ld_probe_file(const char *path)
{
    FILE *f;
    unsigned char *data;
    long len;
    int kind;

    f = fopen(path, "r");
    if (!f)
        return LD_PROBE_UNKNOWN;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) {
        fclose(f);
        return LD_PROBE_UNKNOWN;
    }
    data = malloc(len ? (unsigned)len : 1);
    if (!data)
        error(2, "out of memory");
    if (len && fread(data, 1, len, f) != (size_t)len) {
        free(data);
        fclose(f);
        return LD_PROBE_UNKNOWN;
    }
    fclose(f);
    kind = ld_probe_image(data, (unsigned)len);
    free(data);
    return kind;
}

static char *
ld_find_library_quiet(const char *name)
{
    char *path;
    int i;

    for (i = 0; i < nlibdirs; i++) {
        path = makelibpath(libdirs[i], name);
        if (access(path, R_OK) == 0)
            return path;
        free(path);
    }
    for (i = 0; stdlibdirs[i]; i++) {
        path = makesyslibpath(stdlibdirs[i], name);
        if (access(path, R_OK) == 0)
            return path;
        free(path);
    }
    return 0;
}

static int
ld_args_want_elf(int argc, char **argv)
{
    int i, kind, saw_elf, saw_aout, force_elf, force_aout;

    saw_elf = saw_aout = force_elf = force_aout = 0;
    collectlibdirs(argc, argv);
    for (i = 1; i < argc; i++) {
        char *a = argv[i];

        if (strcmp(a, "--elf") == 0) {
            force_elf = 1;
            continue;
        }
        if (strcmp(a, "--aout") == 0) {
            force_aout = 1;
            continue;
        }
        if (strcmp(a, "-EL") == 0) {
            elf_select_endian(0, "-EL");
            continue;
        }
        if (strcmp(a, "-EB") == 0) {
            elf_select_endian(1, "-EB");
            continue;
        }
        if (strcmp(a, "-m") == 0) {
            force_elf = 1;
            if (++i < argc)
                elf_select_emulation(argv[i]);
            continue;
        }
        if (strncmp(a, "-m", 2) == 0 && a[2]) {
            force_elf = 1;
            elf_select_emulation(a + 2);
            continue;
        }
        if (strcmp(a, "-z") == 0) {
            i++;
            continue;
        }
        if (strcmp(a, "--sysroot") == 0 || strcmp(a, "-o") == 0 ||
            strcmp(a, "-e") == 0 || strcmp(a, "-T") == 0 ||
            strcmp(a, "-L") == 0 || strcmp(a, "-u") == 0) {
            i++;
            continue;
        }
        if (strncmp(a, "--sysroot=", 10) == 0 || strncmp(a, "-T", 2) == 0 ||
            strncmp(a, "-L", 2) == 0)
            continue;
        if (strcmp(a, "-l") == 0) {
            char *path;

            if (++i >= argc)
                break;
            path = ld_find_library_quiet(argv[i]);
            if (!path)
                continue;
            kind = ld_probe_file(path);
            free(path);
        } else if (strncmp(a, "-l", 2) == 0) {
            char *path = ld_find_library_quiet(a + 2);

            if (!path)
                continue;
            kind = ld_probe_file(path);
            free(path);
        } else if (a[0] == '-') {
            continue;
        } else {
            kind = ld_probe_file(a);
        }
        if (kind == LD_PROBE_ELF)
            saw_elf = 1;
        else if (kind == LD_PROBE_AOUT)
            saw_aout = 1;
    }
    if (force_elf)
        return 1;
    if (force_aout)
        return 0;
    if (saw_elf)
        return 1;
    if (saw_aout)
        return 0;
#ifdef REBSD_TOOLCHAIN_ELF_DEFAULT
    return 1;
#else
    return 0;
#endif
}

int main(int argc, char **argv)
{
    ld_program = argv[0];

#ifdef TARGET_BIG_ENDIAN
    aout_set_big_endian(1);
#else
    aout_set_big_endian(0);
#endif

    if (argc == 1) {
        printf("Usage:\n");
        printf("  ld [--elf|--aout] [-sSxXrdtv] [-EL|-EB] [--sysroot dir|--sysroot=dir] [-L dir] [-o file] [-lname] [-u name] [-e name] [-Taddr|-T script] file...\n");
        printf("Options:\n");
        printf("  -o filename     Set output file name, default a.out\n");
        printf("  -L dirname      Add a library search directory\n");
        printf("  --sysroot dir   Set root for standard library directories, default /\n");
        printf("  -llibname       Search for library libname\n");
        printf("  -u symbol       Start with undefined reference to symbol\n");
        printf("  -e symbol       Set start address\n");
        printf("  -Taddress       Set address of .text segment, default %#x\n", basaddr);
        printf("  -T script       Read linker script; a.out uses ENTRY() and initial . address\n");
        printf("  -s              Discard all symbols\n");
        printf("  -S              Discard all symbols except locals and globals\n");
        printf("  -x              Discard local symbols\n");
        printf("  -X              Discard locals starting with 'L' or '.'\n");
        printf("  -r              Generate relocatable output\n");
        printf("  -d              Force common symbols to be defined\n");
        printf("  -t              Increase trace verbosity (up to 3)\n");
        printf("  -v              Enable verbose diagnostics\n");
        printf("  --elf           Link ELF32 MIPS or i386 objects\n");
        printf("  --aout          Link legacy ReBSD a.out objects\n");
        printf("  -m emulation    Select elf_i386 or a MIPS ELF32 emulation\n");
        exit(4);
    }
    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        signal(SIGINT, delexit);
    if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
        signal(SIGTERM, delexit);

    if (ld_args_want_elf(argc, argv))
        return elf_main(argc, argv);

    /*
     * First pass: compute lengths of segments, symbol name table
     * and entry address.
     */
    collectlibdirs(argc, argv);
    pass1(argc, argv);
    filname = 0;

    /*
     * Compute name table.
     */
    middle();

    /*
     * Create temporary files.
     */
    setupout();

    /*
     * Second pass: relocation.
     */
    pass2(argc, argv);

    /*
     * Flush buffers, write a header.
     */
    finishout();

    if (!ofilfnd) {
        unlink("a.out");
        if (link("l.out", "a.out") < 0)
            perror("a.out");
        ofilename = "a.out";
    }
    delarg = errlev;
    delexit(0);
    return (0);
}
