/*
 * Compact AT&T syntax assembler for 32-bit i386 ELF objects.
 *
 * The encoder is intentionally written without host-endian structure writes:
 * this program is also used as a cross assembler on big-endian ReBSD hosts.
 * Instruction encodings and operand rules follow the i386 GAS implementation
 * shipped by NetBSD, but this is an independent compact implementation.  It
 * covers legacy IA-32, x87, MMX/3DNow!, SSE through SSE4.2, and the associated
 * 32-bit system/vendor extensions.  VEX/EVEX families are tracked separately.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <ctype.h>
#include <elf32.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_SECTIONS 64
#define MAX_SYMBOLS 8192
#define MAX_FIXUPS 32768
#define MAX_ALIGNS 8192
#define MAX_TERMS 6
#define MAX_LINE 4096
#define MAX_INCLUDE 16

#define SEC_ABS (-2)
#define FIX_PC8 0xfe
#define FIX_RELAX_JMP 0xfd
#define FIX_RELAX_JCC 0xfc
typedef unsigned char Byte;
typedef unsigned long long IsaMask;

typedef struct Section Section;
typedef struct Symbol Symbol;

typedef struct {
    int sign;
    Symbol *sym;
    int sec;
    unsigned value;
} Term;

typedef struct {
    long long addend;
    Term term[MAX_TERMS];
    int nterm;
    int reloc;
    int parse_sec;
    unsigned parse_dot;
    int has_dot;
} Expr;

struct Section {
    char *name;
    unsigned type;
    unsigned flags;
    unsigned align;
    Byte *data;
    unsigned size;
    unsigned capacity;
    unsigned out_index;
};

struct Symbol {
    char *name;
    int sec;
    unsigned value;
    unsigned size;
    unsigned align;
    unsigned out_index;
    unsigned char bind;
    unsigned char type;
    unsigned char other;
    unsigned char defined;
    unsigned char referenced;
    unsigned char common;
    unsigned char binding_set;
};

typedef struct {
    Section *sec;
    unsigned offset;
    Expr expr;
    unsigned char size;
    unsigned char reloc;
} Fixup;

typedef struct {
    Section *sec;
    unsigned offset;
    unsigned size;
    unsigned align;
    unsigned char fill;
} Alignment;

typedef struct {
    int kind;
    int indirect;
    int reg;
    int reg_size;
    int reg_class;
    int x87;
    int seg_prefix;
    int base;
    int index;
    int scale;
    int addr_size;
    Expr expr;
} Operand;

enum { O_NONE, O_REG, O_IMM, O_MEM };
enum { RC_GPR, RC_X87, RC_MMX, RC_XMM, RC_SEG, RC_CTRL, RC_DEBUG, RC_TEST };

#define ISA_I386 (1ull << 0)
#define ISA_I486 (1ull << 1)
#define ISA_I586 (1ull << 2)
#define ISA_I686 (1ull << 3)
#define ISA_X87 (1ull << 4)
#define ISA_MMX (1ull << 5)
#define ISA_3DNOW (1ull << 6)
#define ISA_SSE (1ull << 7)
#define ISA_SSE2 (1ull << 8)
#define ISA_SSE3 (1ull << 9)
#define ISA_SSSE3 (1ull << 10)
#define ISA_SSE41 (1ull << 11)
#define ISA_SSE42 (1ull << 12)
#define ISA_3DNOWA (1ull << 13)
#define ISA_SYSCALL (1ull << 14)
#define ISA_SVME (1ull << 15)
#define ISA_SSE4A (1ull << 16)
#define ISA_LZCNT (1ull << 17)
#define ISA_POPCNT (1ull << 18)
#define ISA_CYRIX (1ull << 19)
#define ISA_PADLOCK (1ull << 20)
#define ISA_RDTSCP (1ull << 21)
#define ISA_MMXEXT (1ull << 22)
#define ISA_FXSR (1ull << 23)
#define ISA_MONITOR (1ull << 24)
#define ISA_MOVBE (1ull << 25)
#define ISA_VMX (1ull << 26)
#define ISA_SMX (1ull << 27)
#define ISA_XSAVE (1ull << 28)
#define ISA_EPT (1ull << 29)
#define ISA_VMFUNC (1ull << 30)
#define ISA_PRFCHW (1ull << 31)
#define ISA_CLFLUSH (1ull << 32)
#define ISA_XSAVEOPT (1ull << 33)
#define ISA_PADLOCK_RNG2 (1ull << 34)
#define ISA_PADLOCK_PHE2 (1ull << 35)
#define ISA_PADLOCK_XMODX (1ull << 36)
#define ISA_GMISM2 (1ull << 37)
#define ISA_GMICCS (1ull << 38)
#define ISA_MWAITX (1ull << 39)
#define ISA_CLZERO (1ull << 40)
#define ISA_RDPRU (1ull << 41)
#define ISA_ALL ((1ull << 42) - 1)

typedef struct {
    Byte *data;
    unsigned size;
    unsigned capacity;
} Buffer;

static Section sections[MAX_SECTIONS];
static Symbol symbols[MAX_SYMBOLS];
static Fixup fixups[MAX_FIXUPS];
static Alignment alignments[MAX_ALIGNS];
static int nsection, nsymbol, nfixup, nalignment;
static Section *cursec, *prevsec;
static Section *secstack[16];
static int nsecstack;
static unsigned local_count[10];
static const char *source_name = "<stdin>";
static unsigned source_line;
static const char *outfile = "a.out";
static const char *infile;
static const char *include_dir[MAX_INCLUDE];
static int ninclude;
static int uflag, xflag, Xflag;
static int errors;
static int force_addr16;
static IsaMask isa_features = ISA_ALL;
static IsaMask isa_stack[16];
static int isa_stack_depth;
static const char *isa_cpu = "default";
static const char *tune_cpu = "generic32";

static void assemble_instruction(char *line);
static void set_march(const char *arg);
static void set_extension(const char *name);

static void fail(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s:%u: ", source_name, source_line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    errors++;
}

static void fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "as-i386: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void *xrealloc(void *p, unsigned n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q)
        fatal("out of memory");
    return q;
}

static char *xstrdup(const char *s)
{
    unsigned n = strlen(s) + 1;
    char *p = xrealloc(0, n);
    memcpy(p, s, n);
    return p;
}

static char *trim(char *s)
{
    char *e;
    while (isspace((unsigned char)*s))
        s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

static unsigned align_up(unsigned n, unsigned a)
{
    if (a <= 1)
        return n;
    return (n + a - 1) & ~(a - 1);
}

static int power_of_two(unsigned n)
{
    return n && !(n & (n - 1));
}

static void buf_need(Buffer *b, unsigned extra)
{
    unsigned cap;
    if (b->size + extra <= b->capacity)
        return;
    cap = b->capacity ? b->capacity : 128;
    while (cap < b->size + extra)
        cap *= 2;
    b->data = xrealloc(b->data, cap);
    b->capacity = cap;
}

static void buf8(Buffer *b, unsigned v)
{
    buf_need(b, 1);
    b->data[b->size++] = v;
}

static void buf16(Buffer *b, unsigned v)
{
    buf8(b, v);
    buf8(b, v >> 8);
}

static void buf32(Buffer *b, unsigned v)
{
    buf8(b, v);
    buf8(b, v >> 8);
    buf8(b, v >> 16);
    buf8(b, v >> 24);
}

static void buf_bytes(Buffer *b, const void *p, unsigned n)
{
    buf_need(b, n);
    memcpy(b->data + b->size, p, n);
    b->size += n;
}

static unsigned buf_string(Buffer *b, const char *s)
{
    unsigned off = b->size;
    buf_bytes(b, s, strlen(s) + 1);
    return off;
}

static Section *find_section(const char *name)
{
    int i;
    for (i = 0; i < nsection; i++)
        if (!strcmp(sections[i].name, name))
            return &sections[i];
    return 0;
}

static Section *get_section(const char *name, unsigned type, unsigned flags, unsigned align)
{
    Section *s = find_section(name);
    if (s) {
        if (s->type != type || s->flags != flags)
            fail("inconsistent attributes for section %s", name);
        if (align > s->align)
            s->align = align;
        return s;
    }
    if (nsection >= MAX_SECTIONS)
        fatal("too many sections");
    s = &sections[nsection++];
    memset(s, 0, sizeof(*s));
    s->name = xstrdup(name);
    s->type = type;
    s->flags = flags;
    s->align = align ? align : 1;
    return s;
}

static void sec_need(Section *s, unsigned extra)
{
    unsigned cap;
    if (s->type == SHT_NOBITS)
        return;
    if (s->size + extra <= s->capacity)
        return;
    cap = s->capacity ? s->capacity : 256;
    while (cap < s->size + extra)
        cap *= 2;
    s->data = xrealloc(s->data, cap);
    s->capacity = cap;
}

static void emit8(unsigned v)
{
    sec_need(cursec, 1);
    if (cursec->type != SHT_NOBITS)
        cursec->data[cursec->size] = v;
    cursec->size++;
}

static void emit16(unsigned v)
{
    emit8(v);
    emit8(v >> 8);
}

static void emit32(unsigned v)
{
    emit8(v);
    emit8(v >> 8);
    emit8(v >> 16);
    emit8(v >> 24);
}

static void emit64(unsigned long long v)
{
    emit32((unsigned)v);
    emit32((unsigned)(v >> 32));
}

static void patch_value(Section *s, unsigned off, unsigned size, unsigned long long value)
{
    unsigned i;
    if (s->type == SHT_NOBITS || off + size > s->size) {
        fail("internal relocation outside section");
        return;
    }
    for (i = 0; i < size; i++)
        s->data[off + i] = value >> (i * 8);
}

static Symbol *find_symbol(const char *name)
{
    int i;
    for (i = 0; i < nsymbol; i++)
        if (!strcmp(symbols[i].name, name))
            return &symbols[i];
    return 0;
}

static int local_name(const char *name)
{
    return name[0] == '.' || name[0] == 'L' || name[0] == '$';
}

static Symbol *get_symbol(const char *name)
{
    Symbol *s = find_symbol(name);
    if (s)
        return s;
    if (nsymbol >= MAX_SYMBOLS)
        fatal("too many symbols");
    s = &symbols[nsymbol++];
    memset(s, 0, sizeof(*s));
    s->name = xstrdup(name);
    s->sec = -1;
    s->bind = STB_LOCAL;
    s->type = STT_NOTYPE;
    return s;
}

static void define_symbol(const char *name, Section *sec, unsigned value)
{
    Symbol *s = get_symbol(name);
    if (s->defined && !s->common) {
        fail("symbol %s is already defined", name);
        return;
    }
    s->defined = 1;
    s->common = 0;
    s->sec = (int)(sec - sections);
    s->value = value;
}

static void numeric_name(char *buf, unsigned n, unsigned instance)
{
    sprintf(buf, ".L%u.%u", n, instance);
}

static int expr_term_name(const char **cpp, char *name, unsigned namesz)
{
    const char *p = *cpp;
    unsigned n = 0;
    if (isdigit((unsigned char)*p)) {
        const char *q = p;
        while (isdigit((unsigned char)*q))
            q++;
        if ((*q == 'f' || *q == 'b') && !(isalnum((unsigned char)q[1]) || q[1] == '_')) {
            unsigned digit = 0;
            while (p < q)
                digit = digit * 10 + (*p++ - '0');
            if (digit > 9) {
                fail("numeric label must be in range 0..9");
                return 0;
            }
            numeric_name(name, digit, local_count[digit] + (*q == 'f' ? 1 : 0));
            *cpp = q + 1;
            return 1;
        }
    }
    while (*p && !isspace((unsigned char)*p) && *p != '+' && *p != '-' && *p != '[' && *p != ']' &&
           *p != ',' && n + 1 < namesz)
        name[n++] = *p++;
    name[n] = 0;
    *cpp = p;
    return n != 0;
}

static int add_term(Expr *e, int sign, Symbol *sym, int sec, unsigned value)
{
    if (e->nterm >= MAX_TERMS) {
        fail("expression is too complex");
        return 0;
    }
    e->term[e->nterm].sign = sign;
    e->term[e->nterm].sym = sym;
    e->term[e->nterm].sec = sec;
    e->term[e->nterm].value = value;
    e->nterm++;
    return 1;
}

static int parse_expr(const char *text, Expr *e, int dotsec, unsigned dot)
{
    const char *p = text;
    int sign = 1, need_term = 1;
    memset(e, 0, sizeof(*e));
    e->reloc = R_386_32;
    e->parse_sec = dotsec;
    e->parse_dot = dot;
    while (*p) {
        char name[512], *end;
        long long value;
        while (isspace((unsigned char)*p) || *p == '[' || *p == ']')
            p++;
        if (!*p)
            break;
        if (*p == '+' || *p == '-') {
            sign = (*p++ == '-') ? -1 : 1;
            need_term = 1;
            continue;
        }
        if (!need_term) {
            fail("bad expression near '%s'", p);
            return 0;
        }
        if (*p == '.') {
            if (isalnum((unsigned char)p[1]) || p[1] == '_' || p[1] == '$') {
                if (!expr_term_name(&p, name, sizeof(name)))
                    return 0;
                add_term(e, sign, get_symbol(name), -1, 0);
            } else {
                p++;
                e->has_dot = 1;
                add_term(e, sign, 0, dotsec, dot);
            }
        } else if (isdigit((unsigned char)*p) && (strchr(p, 'f') == p + strspn(p, "0123456789") ||
                                                  strchr(p, 'b') == p + strspn(p, "0123456789"))) {
            if (!expr_term_name(&p, name, sizeof(name)))
                return 0;
            add_term(e, sign, get_symbol(name), -1, 0);
        } else if (isdigit((unsigned char)*p)) {
            value = strtoll(p, &end, 0);
            if (end == p) {
                fail("bad number in expression");
                return 0;
            }
            e->addend += sign * value;
            p = end;
        } else if (isalpha((unsigned char)*p) || *p == '_' || *p == '$') {
            char *at;
            if (!expr_term_name(&p, name, sizeof(name)))
                return 0;
            if (!strcmp(name, "_GLOBAL_OFFSET_TABLE_"))
                e->reloc = R_386_GOTPC;
            at = strchr(name, '@');
            if (at) {
                if (!strcmp(at, "@GOTOFF"))
                    e->reloc = R_386_GOTOFF;
                else if (!strcmp(at, "@GOT") || !strcmp(at, "@GOT32"))
                    e->reloc = R_386_GOT32;
                else if (!strcmp(at, "@PLT"))
                    e->reloc = R_386_PLT32;
                else if (!strcmp(at, "@GOTPC"))
                    e->reloc = R_386_GOTPC;
                else {
                    fail("unsupported relocation suffix %s", at);
                    return 0;
                }
                *at = 0;
            }
            add_term(e, sign, get_symbol(name), -1, 0);
        } else {
            fail("bad expression near '%s'", p);
            return 0;
        }
        sign = 1;
        need_term = 0;
    }
    return !need_term || e->nterm == 0;
}

/* Fold absolute symbols and same-section differences. */
static void reduce_expr(Expr *e)
{
    int i, j;
    for (i = 0; i < e->nterm; i++) {
        Term *t = &e->term[i];
        if (t->sym && t->sym->defined && t->sym->sec == SEC_ABS) {
            e->addend += (long long)t->sign * t->sym->value;
            t->sign = 0;
        } else if (t->sym && t->sym->defined && !t->sym->common) {
            t->sec = t->sym->sec;
            t->value = t->sym->value;
        }
    }
    for (i = 0; i < e->nterm; i++) {
        if (!e->term[i].sign || e->term[i].sec < 0)
            continue;
        for (j = i + 1; j < e->nterm; j++) {
            if (e->term[j].sign == -e->term[i].sign && e->term[j].sec == e->term[i].sec) {
                e->addend +=
                    (long long)e->term[i].sign * ((long long)e->term[i].value - e->term[j].value);
                e->term[i].sign = e->term[j].sign = 0;
                break;
            }
        }
    }
    j = 0;
    for (i = 0; i < e->nterm; i++)
        if (e->term[i].sign)
            e->term[j++] = e->term[i];
    e->nterm = j;
}

static int expr_absolute(Expr *e, long long *value)
{
    reduce_expr(e);
    if (e->nterm)
        return 0;
    *value = e->addend;
    return 1;
}

static void add_fixup(Section *sec, unsigned off, unsigned size, Expr *e, unsigned reloc)
{
    if (nfixup >= MAX_FIXUPS)
        fatal("too many relocations");
    fixups[nfixup].sec = sec;
    fixups[nfixup].offset = off;
    fixups[nfixup].size = size;
    fixups[nfixup].expr = *e;
    fixups[nfixup].reloc = reloc;
    nfixup++;
}

static void emit_expr(Expr *e, unsigned size, unsigned reloc)
{
    long long value;
    unsigned rtype;
    unsigned off = cursec->size;
    Expr copy = *e;
    if (expr_absolute(&copy, &value)) {
        if (size == 1)
            emit8((unsigned)value);
        else if (size == 2)
            emit16((unsigned)value);
        else if (size == 4)
            emit32((unsigned)value);
        else
            emit64((unsigned long long)value);
        return;
    }
    if (size != 1 && size != 2 && size != 4) {
        fail("symbolic expression requires an 8-bit, 16-bit, or 32-bit field");
        while (size--)
            emit8(0);
        return;
    }
    rtype = reloc ? reloc : copy.reloc;
    if (rtype == R_386_32)
        rtype = size == 1 ? R_386_8 : size == 2 ? R_386_16 : R_386_32;
    if (size == 1)
        emit8(0);
    else if (size == 2)
        emit16(0);
    else
        emit32(0);
    if (rtype == R_386_GOTPC && copy.has_dot &&
        copy.parse_sec == (int)(cursec - sections))
        copy.addend += (long long)off - copy.parse_dot;
    add_fixup(cursec, off, size, &copy, rtype);
}

static int split_args(char *s, char **arg, int max)
{
    int n = 0, depth = 0, quote = 0;
    char *start = s, *p;
    for (p = s;; p++) {
        if (*p == '"' && (p == s || p[-1] != '\\'))
            quote = !quote;
        if (!quote) {
            if (*p == '(' || *p == '[')
                depth++;
            else if (*p == ')' || *p == ']')
                depth--;
        }
        if ((!*p || (*p == ',' && depth == 0 && !quote))) {
            int at_end = !*p;
            if (n >= max) {
                fail("too many operands");
                return n;
            }
            if (!at_end)
                *p = 0;
            arg[n++] = trim(start);
            if (at_end)
                break;
            start = p + 1;
        }
    }
    return n == 1 && !*arg[0] ? 0 : n;
}

static int general_reg(const char *s, int *size)
{
    static const char *r32[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    static const char *r16[] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
    static const char *r8[] = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" };
    int i;
    if (*s == '%')
        s++;
    for (i = 0; i < 8; i++) {
        if (!strcmp(s, r32[i])) {
            *size = 4;
            return i;
        }
        if (!strcmp(s, r16[i])) {
            *size = 2;
            return i;
        }
        if (!strcmp(s, r8[i])) {
            *size = 1;
            return i;
        }
    }
    return -1;
}

static int special_reg(const char *s, int *reg_class, int *size)
{
    static const char *seg[] = { "es", "cs", "ss", "ds", "fs", "gs" };
    int i;
    if (*s == '%')
        s++;
    if (!strncmp(s, "mm", 2) && s[2] >= '0' && s[2] <= '7' && !s[3]) {
        *reg_class = RC_MMX;
        *size = 8;
        return s[2] - '0';
    }
    if (!strncmp(s, "xmm", 3) && s[3] >= '0' && s[3] <= '7' && !s[4]) {
        *reg_class = RC_XMM;
        *size = 16;
        return s[3] - '0';
    }
    for (i = 0; i < 6; i++)
        if (!strcmp(s, seg[i])) {
            *reg_class = RC_SEG;
            *size = 2;
            return i;
        }
    if ((!strncmp(s, "cr", 2) || !strncmp(s, "dr", 2)) && s[2] >= '0' && s[2] <= '7' &&
        !s[3]) {
        *reg_class = s[0] == 'c' ? RC_CTRL : RC_DEBUG;
        *size = 4;
        return s[2] - '0';
    }
    if (!strncmp(s, "tr", 2) && s[2] >= '3' && s[2] <= '7' && !s[3]) {
        *reg_class = RC_TEST;
        *size = 4;
        return s[2] - '0';
    }
    return -1;
}

static int is_gpr(const Operand *o)
{
    return o->kind == O_REG && o->reg_class == RC_GPR;
}

static int is_gpr_or_mem(const Operand *o)
{
    return is_gpr(o) || o->kind == O_MEM;
}

static const char *feature_name(IsaMask feature)
{
    switch (feature) {
    case ISA_I386:
        return "i386";
    case ISA_I486:
        return "i486";
    case ISA_I586:
        return "i586";
    case ISA_I686:
        return "i686";
    case ISA_X87:
        return "387";
    case ISA_MMX:
        return "mmx";
    case ISA_3DNOW:
        return "3dnow";
    case ISA_SSE:
        return "sse";
    case ISA_SSE2:
        return "sse2";
    case ISA_SSE3:
        return "sse3";
    case ISA_SSSE3:
        return "ssse3";
    case ISA_SSE41:
        return "sse4.1";
    case ISA_SSE42:
        return "sse4.2";
    case ISA_3DNOWA:
        return "3dnowa";
    case ISA_SYSCALL:
        return "syscall";
    case ISA_SVME:
        return "svme";
    case ISA_SSE4A:
        return "sse4a";
    case ISA_LZCNT:
        return "lzcnt";
    case ISA_POPCNT:
        return "popcnt";
    case ISA_CYRIX:
        return "cyrix";
    case ISA_PADLOCK:
        return "padlock";
    case ISA_RDTSCP:
        return "rdtscp";
    case ISA_MMXEXT:
        return "sse or 3dnowa MMX extensions";
    case ISA_FXSR:
        return "fxsr";
    case ISA_MONITOR:
        return "monitor";
    case ISA_MOVBE:
        return "movbe";
    case ISA_VMX:
        return "vmx";
    case ISA_SMX:
        return "smx";
    case ISA_XSAVE:
        return "xsave";
    case ISA_EPT:
        return "ept";
    case ISA_VMFUNC:
        return "vmfunc";
    case ISA_PRFCHW:
        return "prfchw";
    case ISA_CLFLUSH:
        return "clflush";
    case ISA_XSAVEOPT:
        return "xsaveopt";
    case ISA_PADLOCK_RNG2:
        return "padlockrng2";
    case ISA_PADLOCK_PHE2:
        return "padlockphe2";
    case ISA_PADLOCK_XMODX:
        return "padlockxmodx";
    case ISA_GMISM2:
        return "gmism2";
    case ISA_GMICCS:
        return "gmiccs";
    case ISA_MWAITX:
        return "mwaitx";
    case ISA_CLZERO:
        return "clzero";
    case ISA_RDPRU:
        return "rdpru";
    default:
        return "selected ISA";
    }
}

static int need_feature(IsaMask feature, const char *mn)
{
    if ((isa_features & feature) == feature)
        return 1;
    fail("%s requires %s (selected -march=%s)", mn, feature_name(feature), isa_cpu);
    return 0;
}

static int segment_prefix(const char *s)
{
    if (!strcmp(s, "%es"))
        return 0x26;
    if (!strcmp(s, "%cs"))
        return 0x2e;
    if (!strcmp(s, "%ss"))
        return 0x36;
    if (!strcmp(s, "%ds"))
        return 0x3e;
    if (!strcmp(s, "%fs"))
        return 0x64;
    if (!strcmp(s, "%gs"))
        return 0x65;
    return 0;
}

static int parse_x87(const char *s)
{
    char *end;
    long n;
    if (*s == '%')
        s++;
    if (!strcmp(s, "st"))
        return 0;
    if (strncmp(s, "st(", 3) || s[strlen(s) - 1] != ')')
        return -1;
    n = strtol(s + 3, &end, 10);
    if (*end != ')' || end[1] || n < 0 || n > 7)
        return -1;
    return (int)n;
}

static int parse_operand(char *text, Operand *o, int dotsec, unsigned dot)
{
    char *p = trim(text), *lp, *rp, *part[3];
    int n, size;
    memset(o, 0, sizeof(*o));
    o->base = o->index = -1;
    o->scale = 1;
    o->addr_size = 4;
    if (*p == '*') {
        o->indirect = 1;
        p = trim(p + 1);
    }
    if (*p == '%') {
        int xr = parse_x87(p);
        if (xr >= 0) {
            o->kind = O_REG;
            o->x87 = 1;
            o->reg_class = RC_X87;
            o->reg = xr;
            return 1;
        }
        o->reg = general_reg(p, &size);
        if (o->reg >= 0) {
            o->kind = O_REG;
            o->reg_class = RC_GPR;
            o->reg_size = size;
            return 1;
        }
        o->reg = special_reg(p, &o->reg_class, &size);
        if (o->reg >= 0) {
            o->kind = O_REG;
            o->reg_size = size;
            return 1;
        }
    }
    if (*p == '$') {
        o->kind = O_IMM;
        return parse_expr(trim(p + 1), &o->expr, dotsec, dot);
    }
    if (*p == '%') {
        char *colon = strchr(p, ':');
        if (colon) {
            *colon = 0;
            o->seg_prefix = segment_prefix(p);
            if (!o->seg_prefix) {
                fail("bad segment override %s", p);
                return 0;
            }
            p = trim(colon + 1);
        }
    }
    o->kind = O_MEM;
    lp = strchr(p, '(');
    rp = strrchr(p, ')');
    if (!lp) {
        if (!*p)
            return parse_expr("0", &o->expr, dotsec, dot);
        return parse_expr(p, &o->expr, dotsec, dot);
    }
    if (!rp || rp[1]) {
        fail("bad i386 address '%s'", p);
        return 0;
    }
    *lp++ = 0;
    *rp = 0;
    if (*trim(p)) {
        if (!parse_expr(trim(p), &o->expr, dotsec, dot))
            return 0;
    } else if (!parse_expr("0", &o->expr, dotsec, dot))
        return 0;
    n = split_args(lp, part, 3);
    if (n > 0 && *part[0]) {
        o->base = general_reg(part[0], &size);
        if (o->base < 0 || (size != 2 && size != 4)) {
            fail("base register must be 16-bit or 32-bit");
            return 0;
        }
        o->addr_size = size;
    }
    if (n > 1 && *part[1]) {
        o->index = general_reg(part[1], &size);
        if (o->index < 0 || (size != 2 && size != 4) ||
            (o->base >= 0 && size != o->addr_size) || (size == 4 && o->index == 4)) {
            fail("bad index register");
            return 0;
        }
        o->addr_size = size;
    }
    if (n > 2 && *part[2]) {
        o->scale = atoi(part[2]);
        if (o->scale != 1 && o->scale != 2 && o->scale != 4 && o->scale != 8) {
            fail("bad address scale");
            return 0;
        }
    }
    if (o->addr_size == 2 && o->scale != 1) {
        fail("16-bit addresses do not support scaled indexes");
        return 0;
    }
    return 1;
}

static int size_prefix(int size)
{
    if (size == 2) {
        emit8(0x66);
        return 1;
    }
    return 0;
}

static int scale_bits(int scale)
{
    if (scale == 2)
        return 1;
    if (scale == 4)
        return 2;
    if (scale == 8)
        return 3;
    return 0;
}

static int address16_rmbits(const Operand *rm)
{
    int a = rm->base, b = rm->index;

    if (a < 0 && b < 0)
        return 6;
    if (a < 0)
        a = b, b = -1;
    if (b < 0) {
        if (a == 3)
            return 7;
        if (a == 5)
            return 6;
        if (a == 6)
            return 4;
        if (a == 7)
            return 5;
        return -1;
    }
    if ((a == 3 && b == 6) || (a == 6 && b == 3))
        return 0;
    if ((a == 3 && b == 7) || (a == 7 && b == 3))
        return 1;
    if ((a == 5 && b == 6) || (a == 6 && b == 5))
        return 2;
    if ((a == 5 && b == 7) || (a == 7 && b == 5))
        return 3;
    return -1;
}

static void emit_modrm(int regfield, Operand *rm)
{
    Expr e = rm->expr;
    long long disp = 0;
    int absolute = expr_absolute(&e, &disp);
    int mod, rmbits, sib = 0, disp_size = 0;
    if (rm->kind == O_REG) {
        emit8(0xc0 | ((regfield & 7) << 3) | rm->reg);
        return;
    }
    if (rm->seg_prefix)
        fail("internal: segment prefix emitted too late");
    if (rm->addr_size == 2) {
        rmbits = address16_rmbits(rm);
        if (rmbits < 0) {
            fail("invalid 16-bit base/index combination");
            return;
        }
        if (rm->base < 0 && rm->index < 0) {
            mod = 0;
            disp_size = 2;
        } else if (absolute && disp == 0 && rmbits != 6) {
            mod = 0;
            disp_size = 0;
        } else if (absolute && disp >= -128 && disp <= 127) {
            mod = 1;
            disp_size = 1;
        } else {
            mod = 2;
            disp_size = 2;
        }
        emit8((mod << 6) | ((regfield & 7) << 3) | rmbits);
        if (disp_size == 1)
            emit8((unsigned)disp);
        else if (disp_size == 2)
            emit_expr(&rm->expr, 2, R_386_16);
        return;
    }
    if (rm->base < 0) {
        mod = 0;
        disp_size = 4;
        if (rm->index >= 0) {
            rmbits = 4;
            sib = (scale_bits(rm->scale) << 6) | (rm->index << 3) | 5;
        } else
            rmbits = 5;
    } else {
        if (absolute && disp == 0 && rm->base != 5) {
            mod = 0;
            disp_size = 0;
        } else if (absolute && disp >= -128 && disp <= 127) {
            mod = 1;
            disp_size = 1;
        } else {
            mod = 2;
            disp_size = 4;
        }
        if (rm->base == 4 || rm->index >= 0) {
            rmbits = 4;
            sib = (scale_bits(rm->scale) << 6) | ((rm->index < 0 ? 4 : rm->index) << 3) | rm->base;
        } else
            rmbits = rm->base;
    }
    emit8((mod << 6) | ((regfield & 7) << 3) | rmbits);
    if (rmbits == 4)
        emit8(sib);
    if (disp_size == 1)
        emit8((unsigned)disp);
    else if (disp_size == 4)
        emit_expr(&rm->expr, 4, rm->expr.reloc);
}

static int operand_size(Operand *a, Operand *b, int explicit_size)
{
    if (explicit_size)
        return explicit_size;
    if (b && is_gpr(b))
        return b->reg_size;
    if (a && is_gpr(a))
        return a->reg_size;
    return 4;
}

static int signed_byte_expr(Expr *e, long long *value)
{
    Expr copy = *e;
    return expr_absolute(&copy, value) && *value >= -128 && *value <= 127;
}

static int mnemonic(const char *mn, const char *base, int *size)
{
    unsigned n = strlen(base);
    if (!strcmp(mn, base)) {
        *size = 0;
        return 1;
    }
    if (!strncmp(mn, base, n) && mn[n] && !mn[n + 1]) {
        if (mn[n] == 'b')
            *size = 1;
        else if (mn[n] == 'w')
            *size = 2;
        else if (mn[n] == 'l')
            *size = 4;
        else
            return 0;
        return 1;
    }
    return 0;
}

static int require_n(int got, int want, const char *mn)
{
    if (got == want)
        return 1;
    fail("%s expects %d operand%s", mn, want, want == 1 ? "" : "s");
    return 0;
}

static void emit_binary(const char *mn, Operand *op, int n, unsigned base, int group,
                        int explicit_size)
{
    int size;
    if (!require_n(n, 2, mn))
        return;
    if ((op[0].kind == O_REG && !is_gpr(&op[0])) ||
        (op[1].kind == O_REG && !is_gpr(&op[1])) || !is_gpr_or_mem(&op[1])) {
        fail("bad general-register operands for %s", mn);
        return;
    }
    size = operand_size(&op[0], &op[1], explicit_size);
    if (size != 1 && size != 2 && size != 4) {
        fail("bad operand size for %s", mn);
        return;
    }
    if (op[0].seg_prefix)
        emit8(op[0].seg_prefix);
    if (op[1].seg_prefix)
        emit8(op[1].seg_prefix);
    size_prefix(size);
    if (op[0].kind == O_IMM) {
        long long small;
        int short_imm = size != 1 && signed_byte_expr(&op[0].expr, &small);
        emit8(size == 1 ? 0x80 : short_imm ? 0x83 : 0x81);
        emit_modrm(group, &op[1]);
        emit_expr(&op[0].expr, short_imm ? 1 : size, op[0].expr.reloc);
    } else if (op[0].kind == O_REG && op[1].kind != O_IMM) {
        emit8(base + (size != 1));
        emit_modrm(op[0].reg, &op[1]);
    } else if (op[1].kind == O_REG && op[0].kind == O_MEM) {
        emit8(base + 2 + (size != 1));
        emit_modrm(op[1].reg, &op[0]);
    } else
        fail("bad operands for %s", mn);
}

static int condition_code(const char *s)
{
    static const struct {
        const char *name;
        int code;
    } cc[] = {
        { "o", 0 },   { "no", 1 },  { "b", 2 },   { "c", 2 },   { "nae", 2 }, { "ae", 3 },
        { "nb", 3 },  { "nc", 3 },  { "e", 4 },   { "z", 4 },   { "ne", 5 },  { "nz", 5 },
        { "be", 6 },  { "na", 6 },  { "a", 7 },   { "nbe", 7 }, { "s", 8 },   { "ns", 9 },
        { "p", 10 },  { "pe", 10 }, { "np", 11 }, { "po", 11 }, { "l", 12 },  { "nge", 12 },
        { "ge", 13 }, { "nl", 13 }, { "le", 14 }, { "ng", 14 }, { "g", 15 },  { "nle", 15 }
    };
    unsigned i;
    for (i = 0; i < sizeof(cc) / sizeof(cc[0]); i++)
        if (!strcmp(s, cc[i].name))
            return cc[i].code;
    return -1;
}

static void emit_relative(Expr *e, unsigned opcode1, int opcode2, int reloc)
{
    Expr x = *e;
    if (opcode2 >= 0) {
        emit8(opcode1);
        emit8(opcode2);
    } else
        emit8(opcode1);
    x.addend -= 4;
    emit_expr(&x, 4, reloc ? reloc : R_386_PC32);
}

static void emit_shift(const char *mn, Operand *op, int n, int group, int esize)
{
    int size;
    Operand *dst;
    if (n != 1 && n != 2) {
        fail("%s expects one or two operands", mn);
        return;
    }
    dst = &op[n - 1];
    if (!is_gpr_or_mem(dst)) {
        fail("bad shift destination");
        return;
    }
    size = operand_size(0, dst, esize);
    if (dst->seg_prefix)
        emit8(dst->seg_prefix);
    size_prefix(size);
    if (n == 1) {
        emit8(size == 1 ? 0xd0 : 0xd1);
        emit_modrm(group, dst);
    } else if (is_gpr(&op[0]) && op[0].reg == 1 && op[0].reg_size == 1) {
        emit8(size == 1 ? 0xd2 : 0xd3);
        emit_modrm(group, dst);
    } else if (op[0].kind == O_IMM) {
        Expr e = op[0].expr;
        long long count;
        if (expr_absolute(&e, &count) && count == 1) {
            emit8(size == 1 ? 0xd0 : 0xd1);
            emit_modrm(group, dst);
        } else {
            emit8(size == 1 ? 0xc0 : 0xc1);
            emit_modrm(group, dst);
            emit_expr(&op[0].expr, 1, 0);
        }
    } else
        fail("bad shift count");
}

typedef struct {
    const char *name;
    unsigned char opcode;
    unsigned char group;
} BinaryInsn;

static int emit_binary_family(const char *mn, Operand *op, int n)
{
    static const BinaryInsn tab[] = {
        { "add", 0x00, 0 }, { "or", 0x08, 1 },  { "adc", 0x10, 2 },
        { "sbb", 0x18, 3 }, { "and", 0x20, 4 }, { "sub", 0x28, 5 },
        { "xor", 0x30, 6 }, { "cmp", 0x38, 7 },
    };
    unsigned i;
    int size;

    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if (!mnemonic(mn, tab[i].name, &size))
            continue;
        emit_binary(mn, op, n, tab[i].opcode, tab[i].group, size);
        return 1;
    }
    return 0;
}

typedef struct {
    const char *name;
    unsigned char group;
} GroupInsn;

static int emit_unary_family(const char *mn, Operand *op, int n)
{
    static const GroupInsn tab[] = {
        { "inc", 0 }, { "dec", 1 }, { "not", 2 },  { "neg", 3 },
        { "mul", 4 }, { "imul", 5 }, { "div", 6 }, { "idiv", 7 },
    };
    unsigned i;
    int size, group;

    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if (!mnemonic(mn, tab[i].name, &size))
            continue;
        if (tab[i].group == 5 && n != 1)
            return 0;
        group = tab[i].group;
        if (!require_n(n, 1, mn))
            return 1;
        if (!is_gpr_or_mem(&op[0])) {
            fail("bad operand for %s", mn);
            return 1;
        }
        size = operand_size(&op[0], 0, size);
        if (op[0].seg_prefix)
            emit8(op[0].seg_prefix);
        size_prefix(size);
        if (group <= 1 && is_gpr(&op[0]) && size != 1) {
            emit8(0x40 + group * 8 + op[0].reg);
            return 1;
        }
        emit8(group <= 1 ? (size == 1 ? 0xfe : 0xff) :
                            (size == 1 ? 0xf6 : 0xf7));
        emit_modrm(group, &op[0]);
        return 1;
    }
    return 0;
}

static int emit_shift_family(const char *mn, Operand *op, int n)
{
    static const GroupInsn tab[] = {
        { "rol", 0 }, { "ror", 1 }, { "rcl", 2 }, { "rcr", 3 },
        { "sal", 4 }, { "shl", 4 }, { "shr", 5 }, { "sar", 7 },
    };
    unsigned i;
    int size;

    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if (!mnemonic(mn, tab[i].name, &size))
            continue;
        emit_shift(mn, op, n, tab[i].group, size);
        return 1;
    }
    return 0;
}

static int x87_fixed(const char *mn, int n)
{
    static const struct {
        const char *name;
        unsigned char len;
        unsigned char code[3];
    } tab[] = {
        { "f2xm1", 2, { 0xd9, 0xf0 } },   { "fabs", 2, { 0xd9, 0xe1 } },
        { "fchs", 2, { 0xd9, 0xe0 } },    { "fcos", 2, { 0xd9, 0xff } },
        { "fdecstp", 2, { 0xd9, 0xf6 } }, { "fincstp", 2, { 0xd9, 0xf7 } },
        { "fld1", 2, { 0xd9, 0xe8 } },    { "fldl2t", 2, { 0xd9, 0xe9 } },
        { "fldl2e", 2, { 0xd9, 0xea } },  { "fldpi", 2, { 0xd9, 0xeb } },
        { "fldlg2", 2, { 0xd9, 0xec } },  { "fldln2", 2, { 0xd9, 0xed } },
        { "fldz", 2, { 0xd9, 0xee } },    { "fnclex", 2, { 0xdb, 0xe2 } },
        { "fclex", 3, { 0x9b, 0xdb, 0xe2 } },
        { "fninit", 2, { 0xdb, 0xe3 } },  { "finit", 3, { 0x9b, 0xdb, 0xe3 } },
        { "fnop", 2, { 0xd9, 0xd0 } },    { "fpatan", 2, { 0xd9, 0xf3 } },
        { "fprem", 2, { 0xd9, 0xf8 } },   { "fprem1", 2, { 0xd9, 0xf5 } },
        { "fptan", 2, { 0xd9, 0xf2 } },   { "frndint", 2, { 0xd9, 0xfc } },
        { "fscale", 2, { 0xd9, 0xfd } },  { "fsin", 2, { 0xd9, 0xfe } },
        { "fsincos", 2, { 0xd9, 0xfb } }, { "fsqrt", 2, { 0xd9, 0xfa } },
        { "ftst", 2, { 0xd9, 0xe4 } },    { "fucompp", 2, { 0xda, 0xe9 } },
        { "fxam", 2, { 0xd9, 0xe5 } },    { "fxtract", 2, { 0xd9, 0xf4 } },
        { "fyl2x", 2, { 0xd9, 0xf1 } },   { "fyl2xp1", 2, { 0xd9, 0xf9 } },
        { "fcompp", 2, { 0xde, 0xd9 } },  { "fneni", 2, { 0xdb, 0xe0 } },
        { "feni", 3, { 0x9b, 0xdb, 0xe0 } },
        { "fndisi", 2, { 0xdb, 0xe1 } },  { "fdisi", 3, { 0x9b, 0xdb, 0xe1 } },
        { "fnsetpm", 2, { 0xdb, 0xe4 } }, { "fsetpm", 3, { 0x9b, 0xdb, 0xe4 } },
        { "frstpm", 2, { 0xdb, 0xe5 } },
    };
    unsigned i, j;
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if (strcmp(mn, tab[i].name))
            continue;
        if (!require_n(n, 0, mn))
            return 1;
        for (j = 0; j < tab[i].len; j++)
            emit8(tab[i].code[j]);
        return 1;
    }
    return 0;
}

static int x87_register(const char *mn, Operand *op, int n)
{
    static const struct {
        const char *name;
        unsigned short code;
        unsigned char default_st1;
    } simple[] = {
        { "fld", 0xd9c0, 0 },   { "fxch", 0xd9c8, 1 }, { "fst", 0xddd0, 0 },
        { "fstp", 0xddd8, 0 },  { "fcom", 0xd8d0, 1 }, { "fcomp", 0xd8d8, 1 },
        { "fucom", 0xdde0, 1 }, { "fucomp", 0xdde8, 1 },
        { "ffree", 0xddc0, 0 }, { "ffreep", 0xdfc0, 0 },
    };
    static const struct {
        const char *suffix;
        unsigned short code;
    } cmov[] = {
        { "b", 0xdac0 },   { "nae", 0xdac0 }, { "e", 0xdac8 },  { "be", 0xdad0 },
        { "na", 0xdad0 },  { "u", 0xdad8 },   { "ae", 0xdbc0 }, { "nb", 0xdbc0 },
        { "ne", 0xdbc8 },  { "a", 0xdbd0 },   { "nbe", 0xdbd0 }, { "nu", 0xdbd8 },
    };
    static const struct {
        const char *name;
        unsigned char opcode;
        unsigned char code;
    } compare[] = {
        { "fcomi", 0xdb, 0xf0 },   { "fucomi", 0xdb, 0xe8 },
        { "fcomip", 0xdf, 0xf0 },  { "fcompi", 0xdf, 0xf0 },
        { "fucomip", 0xdf, 0xe8 }, { "fucompi", 0xdf, 0xe8 },
    };
    static const struct {
        const char *name;
        unsigned char slash;
        unsigned char pop;
    } arith[] = {
        { "fadd", 0, 0 },  { "fmul", 1, 0 },  { "fsub", 4, 0 },
        { "fsubr", 5, 0 }, { "fdiv", 6, 0 },  { "fdivr", 7, 0 },
        { "faddp", 0, 1 }, { "fmulp", 1, 1 }, { "fsubp", 4, 1 },
        { "fsubrp", 5, 1 }, { "fdivp", 6, 1 }, { "fdivrp", 7, 1 },
    };
    unsigned j;
    int i, reverse;

    if (!strcmp(mn, "fnstsw") || !strcmp(mn, "fstsw")) {
        if (n == 0 || (n == 1 && is_gpr(&op[0]) && op[0].reg == 0 && op[0].reg_size == 2)) {
            if (mn[1] != 'n')
                emit8(0x9b);
            emit8(0xdf);
            emit8(0xe0);
            return 1;
        }
    }
    for (j = 0; j < sizeof(simple) / sizeof(simple[0]); j++) {
        if (strcmp(mn, simple[j].name))
            continue;
        if (n == 0 && simple[j].default_st1)
            i = 1;
        else if (n == 1 && op[0].reg_class == RC_X87)
            i = op[0].reg;
        else {
            fail("bad operands for %s", mn);
            return 1;
        }
        emit8(simple[j].code >> 8);
        emit8(simple[j].code + i);
        return 1;
    }
    if (!strncmp(mn, "fcmov", 5)) {
        const char *ccname = mn + 5;
        if (!require_n(n, 2, mn) || op[0].reg_class != RC_X87 ||
            op[1].reg_class != RC_X87 || op[1].reg != 0) {
            fail("bad operands for %s", mn);
            return 1;
        }
        if (!need_feature(ISA_I686, mn))
            return 1;
        for (j = 0; j < sizeof(cmov) / sizeof(cmov[0]); j++)
            if (!strcmp(ccname, cmov[j].suffix))
                break;
        if (j == sizeof(cmov) / sizeof(cmov[0])) {
            fail("unknown x87 instruction %s", mn);
            return 1;
        }
        emit8(cmov[j].code >> 8);
        emit8(cmov[j].code + op[0].reg);
        return 1;
    }
    for (j = 0; j < sizeof(compare) / sizeof(compare[0]); j++)
        if (!strcmp(mn, compare[j].name))
            break;
    if (j != sizeof(compare) / sizeof(compare[0])) {
        if ((n != 1 && n != 2) || op[0].reg_class != RC_X87 ||
            (n == 2 && (op[1].reg_class != RC_X87 || op[1].reg != 0))) {
            fail("bad operands for %s", mn);
            return 1;
        }
        if (!need_feature(ISA_I686, mn))
            return 1;
        emit8(compare[j].opcode);
        emit8(compare[j].code + op[0].reg);
        return 1;
    }
    for (j = 0; j < sizeof(arith) / sizeof(arith[0]); j++)
        if (!strcmp(mn, arith[j].name))
            break;
    if (j != sizeof(arith) / sizeof(arith[0])) {
        if (n == 0) {
            if (!arith[j].pop)
                return 0;
            i = 1;
            reverse = 0;
        } else if (n == 1 && op[0].reg_class == RC_X87) {
            i = op[0].reg;
            reverse = arith[j].pop;
        } else if (n == 2 && op[0].reg_class == RC_X87 && op[1].reg_class == RC_X87 &&
                   (op[0].reg == 0 || op[1].reg == 0)) {
            i = op[0].reg ? op[0].reg : op[1].reg;
            reverse = op[0].reg == 0;
        } else
            return 0;
        if (arith[j].pop) {
            emit8(0xde);
            emit8(0xc0 + arith[j].slash * 8 + i);
        } else if (reverse) {
            emit8(0xdc);
            emit8(0xc0 + arith[j].slash * 8 + i);
        } else {
            emit8(0xd8);
            emit8(0xc0 + arith[j].slash * 8 + i);
        }
        return 1;
    }
    return 0;
}

enum {
    X87M_WAIT = 1,
    X87M_SHORT_ENV = 2
};

typedef struct {
    const char *name;
    IsaMask feature;
    unsigned char opcode;
    unsigned char slash;
    unsigned char flags;
} X87MemInsn;

#define X87M(n, f, o, s, fl) { n, f, o, s, fl }
#define X87(n, o, s) X87M(n, ISA_X87, o, s, 0)
#define X87W(n, o, s) X87M(n, ISA_X87, o, s, X87M_WAIT)
#define X87S(n, o, s) X87M(n, ISA_X87, o, s, X87M_SHORT_ENV)
#define X87SW(n, o, s) X87M(n, ISA_X87, o, s, X87M_SHORT_ENV | X87M_WAIT)

static const X87MemInsn x87_mem_insns[] = {
    X87("fld", 0xd9, 0),       X87("flds", 0xd9, 0),      X87("fldl", 0xdd, 0),
    X87("fldt", 0xdb, 5),      X87("fst", 0xd9, 2),       X87("fsts", 0xd9, 2),
    X87("fstl", 0xdd, 2),      X87("fstp", 0xd9, 3),      X87("fstps", 0xd9, 3),
    X87("fstpl", 0xdd, 3),     X87("fstpt", 0xdb, 7),

    X87("fild", 0xdf, 0),      X87("filds", 0xdf, 0),
    X87("fildl", 0xdb, 0),     X87("fildll", 0xdf, 5),    X87("fildq", 0xdf, 5),
    X87("fist", 0xdf, 2),      X87("fists", 0xdf, 2),
    X87("fistl", 0xdb, 2),     X87("fistp", 0xdf, 3),     X87("fistps", 0xdf, 3),
    X87("fistpl", 0xdb, 3),    X87("fistpll", 0xdf, 7),
    X87("fistpq", 0xdf, 7),
    X87M("fisttp", ISA_SSE3, 0xdf, 1, 0),
    X87M("fisttps", ISA_SSE3, 0xdf, 1, 0),
    X87M("fisttpl", ISA_SSE3, 0xdb, 1, 0),
    X87M("fisttpll", ISA_SSE3, 0xdd, 1, 0),
    X87("fbld", 0xdf, 4),      X87("fbstp", 0xdf, 6),

    X87("fadds", 0xd8, 0),     X87("fadd", 0xd8, 0),      X87("faddl", 0xdc, 0),
    X87("fmuls", 0xd8, 1),     X87("fmul", 0xd8, 1),      X87("fmull", 0xdc, 1),
    X87("fcoms", 0xd8, 2),     X87("fcom", 0xd8, 2),      X87("fcoml", 0xdc, 2),
    X87("fcomps", 0xd8, 3),    X87("fcomp", 0xd8, 3),     X87("fcompl", 0xdc, 3),
    X87("fsubs", 0xd8, 4),     X87("fsub", 0xd8, 4),      X87("fsubl", 0xdc, 4),
    X87("fsubrs", 0xd8, 5),    X87("fsubr", 0xd8, 5),     X87("fsubrl", 0xdc, 5),
    X87("fdivs", 0xd8, 6),     X87("fdiv", 0xd8, 6),      X87("fdivl", 0xdc, 6),
    X87("fdivrs", 0xd8, 7),    X87("fdivr", 0xd8, 7),     X87("fdivrl", 0xdc, 7),

    X87("fiadd", 0xde, 0),     X87("fiadds", 0xde, 0),
    X87("fiaddl", 0xda, 0),    X87("fimul", 0xde, 1),     X87("fimuls", 0xde, 1),
    X87("fimull", 0xda, 1),    X87("ficom", 0xde, 2),     X87("ficoms", 0xde, 2),
    X87("ficoml", 0xda, 2),    X87("ficomp", 0xde, 3),    X87("ficomps", 0xde, 3),
    X87("ficompl", 0xda, 3),   X87("fisub", 0xde, 4),     X87("fisubs", 0xde, 4),
    X87("fisubl", 0xda, 4),    X87("fisubr", 0xde, 5),    X87("fisubrs", 0xde, 5),
    X87("fisubrl", 0xda, 5),   X87("fidiv", 0xde, 6),     X87("fidivs", 0xde, 6),
    X87("fidivl", 0xda, 6),    X87("fidivr", 0xde, 7),    X87("fidivrs", 0xde, 7),
    X87("fidivrl", 0xda, 7),

    X87("fldcw", 0xd9, 5),     X87("fnstcw", 0xd9, 7),    X87W("fstcw", 0xd9, 7),
    X87("fnstsw", 0xdd, 7),    X87W("fstsw", 0xdd, 7),
    X87("fnstenv", 0xd9, 6),   X87S("fnstenvs", 0xd9, 6), X87("fnstenvl", 0xd9, 6),
    X87W("fstenv", 0xd9, 6),   X87SW("fstenvs", 0xd9, 6), X87W("fstenvl", 0xd9, 6),
    X87("fldenv", 0xd9, 4),    X87S("fldenvs", 0xd9, 4),  X87("fldenvl", 0xd9, 4),
    X87("fnsave", 0xdd, 6),    X87S("fnsaves", 0xdd, 6),  X87("fnsavel", 0xdd, 6),
    X87W("fsave", 0xdd, 6),    X87SW("fsaves", 0xdd, 6),  X87W("fsavel", 0xdd, 6),
    X87("frstor", 0xdd, 4),    X87S("frstors", 0xdd, 4),  X87("frstorl", 0xdd, 4)
};

#undef X87SW
#undef X87S
#undef X87W
#undef X87
#undef X87M

static int x87_memory(const char *mn, Operand *op, int n)
{
    const X87MemInsn *insn;
    unsigned i;

    if (n != 1 || op[0].kind != O_MEM)
        return 0;
    for (i = 0; i < sizeof(x87_mem_insns) / sizeof(x87_mem_insns[0]); i++) {
        insn = &x87_mem_insns[i];
        if (strcmp(mn, insn->name))
            continue;
        if (!need_feature(insn->feature, mn))
            return 1;
        if (insn->flags & X87M_WAIT)
            emit8(0x9b);
        if (op[0].seg_prefix)
            emit8(op[0].seg_prefix);
        if (insn->flags & X87M_SHORT_ENV)
            emit8(0x66);
        emit8(insn->opcode);
        emit_modrm(insn->slash, &op[0]);
        return 1;
    }
    return 0;
}

static void emit_x87(const char *mn, Operand *op, int n)
{
    if (x87_fixed(mn, n) || x87_register(mn, op, n) || x87_memory(mn, op, n))
        return;
    fail("unknown x87 instruction or bad operands for %s", mn);
}

static int is_x87_name(const char *mn)
{
    return mn[0] == 'f' && strncmp(mn, "file", 4);
}

typedef struct {
    const char *name;
    IsaMask feature;
    unsigned char len;
    unsigned char code[4];
} FixedInsn;

static int emit_fixed(const char *mn, int n)
{
    static const FixedInsn tab[] = {
        { "aaa", ISA_I386, 1, { 0x37 } },       { "aas", ISA_I386, 1, { 0x3f } },
        { "cbw", ISA_I386, 2, { 0x66, 0x98 } }, { "cbtw", ISA_I386, 2, { 0x66, 0x98 } },
        { "cwde", ISA_I386, 1, { 0x98 } },      { "cwtl", ISA_I386, 1, { 0x98 } },
        { "cwd", ISA_I386, 2, { 0x66, 0x99 } }, { "cwtd", ISA_I386, 2, { 0x66, 0x99 } },
        { "cdq", ISA_I386, 1, { 0x99 } },       { "cltd", ISA_I386, 1, { 0x99 } },
        { "clc", ISA_I386, 1, { 0xf8 } },       { "cld", ISA_I386, 1, { 0xfc } },
        { "cli", ISA_I386, 1, { 0xfa } },       { "clts", ISA_I386, 2, { 0x0f, 0x06 } },
        { "cmc", ISA_I386, 1, { 0xf5 } },       { "cpuid", ISA_I586, 2, { 0x0f, 0xa2 } },
        { "daa", ISA_I386, 1, { 0x27 } },       { "das", ISA_I386, 1, { 0x2f } },
        { "fwait", ISA_X87, 1, { 0x9b } },      { "wait", ISA_X87, 1, { 0x9b } },
        { "hlt", ISA_I386, 1, { 0xf4 } },       { "int3", ISA_I386, 1, { 0xcc } },
        { "into", ISA_I386, 1, { 0xce } },      { "invd", ISA_I486, 2, { 0x0f, 0x08 } },
        { "iret", ISA_I386, 1, { 0xcf } },      { "iretl", ISA_I386, 1, { 0xcf } },
        { "lahf", ISA_I386, 1, { 0x9f } },      { "leave", ISA_I386, 1, { 0xc9 } },
        { "nop", ISA_I386, 1, { 0x90 } },       { "pause", ISA_SSE2, 2, { 0xf3, 0x90 } },
        { "popa", ISA_I386, 1, { 0x61 } },      { "popal", ISA_I386, 1, { 0x61 } },
        { "popaw", ISA_I386, 2, { 0x66, 0x61 } },
        { "popf", ISA_I386, 1, { 0x9d } },      { "popfl", ISA_I386, 1, { 0x9d } },
        { "popfw", ISA_I386, 2, { 0x66, 0x9d } },
        { "pusha", ISA_I386, 1, { 0x60 } },     { "pushal", ISA_I386, 1, { 0x60 } },
        { "pushaw", ISA_I386, 2, { 0x66, 0x60 } },
        { "pushf", ISA_I386, 1, { 0x9c } },     { "pushfl", ISA_I386, 1, { 0x9c } },
        { "pushfw", ISA_I386, 2, { 0x66, 0x9c } },
        { "rdmsr", ISA_I586, 2, { 0x0f, 0x32 } },
        { "rdpmc", ISA_I686, 2, { 0x0f, 0x33 } },
        { "rdtsc", ISA_I586, 2, { 0x0f, 0x31 } },
        { "rsm", ISA_I586, 2, { 0x0f, 0xaa } }, { "sahf", ISA_I386, 1, { 0x9e } },
        { "stc", ISA_I386, 1, { 0xf9 } },       { "std", ISA_I386, 1, { 0xfd } },
        { "sti", ISA_I386, 1, { 0xfb } },       { "sysenter", ISA_I686, 2, { 0x0f, 0x34 } },
        { "sysexit", ISA_I686, 2, { 0x0f, 0x35 } },
        { "ud2", ISA_I686, 2, { 0x0f, 0x0b } }, { "wbinvd", ISA_I486, 2, { 0x0f, 0x09 } },
        { "wrmsr", ISA_I586, 2, { 0x0f, 0x30 } },
        { "xlat", ISA_I386, 1, { 0xd7 } },      { "xlatb", ISA_I386, 1, { 0xd7 } },
        { "emms", ISA_MMX, 2, { 0x0f, 0x77 } },
        { "femms", ISA_3DNOW, 2, { 0x0f, 0x0e } },
        { "sfence", ISA_MMXEXT, 3, { 0x0f, 0xae, 0xf8 } },
        { "lfence", ISA_SSE2, 3, { 0x0f, 0xae, 0xe8 } },
        { "mfence", ISA_SSE2, 3, { 0x0f, 0xae, 0xf0 } },
        { "monitor", ISA_MONITOR, 3, { 0x0f, 0x01, 0xc8 } },
        { "mwait", ISA_MONITOR, 3, { 0x0f, 0x01, 0xc9 } },
        { "syscall", ISA_SYSCALL, 2, { 0x0f, 0x05 } },
        { "sysret", ISA_SYSCALL, 2, { 0x0f, 0x07 } },
        { "rdtscp", ISA_RDTSCP, 3, { 0x0f, 0x01, 0xf9 } },
        { "vmrun", ISA_SVME, 3, { 0x0f, 0x01, 0xd8 } },
        { "vmmcall", ISA_SVME, 3, { 0x0f, 0x01, 0xd9 } },
        { "vmload", ISA_SVME, 3, { 0x0f, 0x01, 0xda } },
        { "vmsave", ISA_SVME, 3, { 0x0f, 0x01, 0xdb } },
        { "stgi", ISA_SVME, 3, { 0x0f, 0x01, 0xdc } },
        { "clgi", ISA_SVME, 3, { 0x0f, 0x01, 0xdd } },
        { "skinit", ISA_SVME, 3, { 0x0f, 0x01, 0xde } },
        { "invlpga", ISA_SVME, 3, { 0x0f, 0x01, 0xdf } },
        { "smint", ISA_CYRIX, 2, { 0x0f, 0x7e } },
        { "smintold", ISA_CYRIX, 2, { 0x0f, 0x7e } },
        { "xstore", ISA_PADLOCK, 3, { 0x0f, 0xa7, 0xc0 } },
        { "xstorerng", ISA_PADLOCK, 3, { 0x0f, 0xa7, 0xc0 } },
        { "xstore-rng", ISA_PADLOCK, 3, { 0x0f, 0xa7, 0xc0 } },
        { "xcryptecb", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa7, 0xc8 } },
        { "xcrypt-ecb", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa7, 0xc8 } },
        { "xcryptcbc", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa7, 0xd0 } },
        { "xcrypt-cbc", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa7, 0xd0 } },
        { "xcryptctr", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa7, 0xd8 } },
        { "xcrypt-ctr", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa7, 0xd8 } },
        { "xcryptcfb", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa7, 0xe0 } },
        { "xcrypt-cfb", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa7, 0xe0 } },
        { "xcryptofb", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa7, 0xe8 } },
        { "xcrypt-ofb", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa7, 0xe8 } },
        { "montmul", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa6, 0xc0 } },
        { "xsha1", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa6, 0xc8 } },
        { "xsha256", ISA_PADLOCK, 4, { 0xf3, 0x0f, 0xa6, 0xd0 } },
        { "xrng2", ISA_PADLOCK_RNG2, 4, { 0xf3, 0x0f, 0xa7, 0xf8 } },
        { "xsha384", ISA_PADLOCK_PHE2, 4, { 0xf3, 0x0f, 0xa6, 0xd8 } },
        { "xsha512", ISA_PADLOCK_PHE2, 4, { 0xf3, 0x0f, 0xa6, 0xe0 } },
        { "montmul2", ISA_PADLOCK_XMODX, 4, { 0xf3, 0x0f, 0xa6, 0xf0 } },
        { "xmodexp", ISA_PADLOCK_XMODX, 4, { 0xf3, 0x0f, 0xa6, 0xf8 } },
        { "sm2", ISA_GMISM2, 4, { 0xf2, 0x0f, 0xa6, 0xc0 } },
        { "sm3", ISA_GMICCS, 4, { 0xf3, 0x0f, 0xa6, 0xe8 } },
        { "sm4", ISA_GMICCS, 4, { 0xf3, 0x0f, 0xa7, 0xf0 } },
        { "monitorx", ISA_MWAITX, 3, { 0x0f, 0x01, 0xfa } },
        { "mwaitx", ISA_MWAITX, 3, { 0x0f, 0x01, 0xfb } },
        { "clzero", ISA_CLZERO, 3, { 0x0f, 0x01, 0xfc } },
        { "rdpru", ISA_RDPRU, 3, { 0x0f, 0x01, 0xfd } },
        { "vmcall", ISA_VMX, 3, { 0x0f, 0x01, 0xc1 } },
        { "vmlaunch", ISA_VMX, 3, { 0x0f, 0x01, 0xc2 } },
        { "vmresume", ISA_VMX, 3, { 0x0f, 0x01, 0xc3 } },
        { "vmxoff", ISA_VMX, 3, { 0x0f, 0x01, 0xc4 } },
        { "vmfunc", ISA_VMFUNC, 3, { 0x0f, 0x01, 0xd4 } },
        { "getsec", ISA_SMX, 2, { 0x0f, 0x37 } },
        { "xgetbv", ISA_XSAVE, 3, { 0x0f, 0x01, 0xd0 } },
        { "xsetbv", ISA_XSAVE, 3, { 0x0f, 0x01, 0xd1 } },
        { "movsb", ISA_I386, 1, { 0xa4 } },   { "movsw", ISA_I386, 2, { 0x66, 0xa5 } },
        { "movsl", ISA_I386, 1, { 0xa5 } },   { "cmpsb", ISA_I386, 1, { 0xa6 } },
        { "cmpsw", ISA_I386, 2, { 0x66, 0xa7 } },
        { "cmpsl", ISA_I386, 1, { 0xa7 } },   { "stosb", ISA_I386, 1, { 0xaa } },
        { "stosw", ISA_I386, 2, { 0x66, 0xab } },
        { "stosl", ISA_I386, 1, { 0xab } },   { "lodsb", ISA_I386, 1, { 0xac } },
        { "lodsw", ISA_I386, 2, { 0x66, 0xad } },
        { "lodsl", ISA_I386, 1, { 0xad } },   { "scasb", ISA_I386, 1, { 0xae } },
        { "scasw", ISA_I386, 2, { 0x66, 0xaf } },
        { "scasl", ISA_I386, 1, { 0xaf } },   { "insb", ISA_I386, 1, { 0x6c } },
        { "insw", ISA_I386, 2, { 0x66, 0x6d } },
        { "insl", ISA_I386, 1, { 0x6d } },    { "outsb", ISA_I386, 1, { 0x6e } },
        { "outsw", ISA_I386, 2, { 0x66, 0x6f } },
        { "outsl", ISA_I386, 1, { 0x6f } },   { "salc", ISA_I386, 1, { 0xd6 } },
        { "int1", ISA_I386, 1, { 0xf1 } },    { "icebp", ISA_I386, 1, { 0xf1 } },
        { "iretw", ISA_I386, 2, { 0x66, 0xcf } },
        { "ud2a", ISA_I386, 2, { 0x0f, 0x0b } },
    };
    unsigned i, j;
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if (strcmp(mn, tab[i].name))
            continue;
        if (!strcmp(mn, "nop") && n != 0)
            return 0;
        if (!require_n(n, 0, mn) || !need_feature(tab[i].feature, mn))
            return 1;
        for (j = 0; j < tab[i].len; j++)
            emit8(tab[i].code[j]);
        return 1;
    }
    return 0;
}

typedef struct {
    const char *name;
    IsaMask feature;
    unsigned char code[3];
    unsigned char noperand;
    unsigned char reg[3];
    unsigned char address_size_first;
} ImplicitInsn;

static int emit_implicit(const char *mn, Operand *op, int n)
{
    static const ImplicitInsn tab[] = {
        { "monitor", ISA_MONITOR, { 0x0f, 0x01, 0xc8 }, 3, { 0, 1, 2 }, 1 },
        { "mwait", ISA_MONITOR, { 0x0f, 0x01, 0xc9 }, 2, { 0, 1, 0 }, 0 },
        { "vmrun", ISA_SVME, { 0x0f, 0x01, 0xd8 }, 1, { 0, 0, 0 }, 1 },
        { "vmload", ISA_SVME, { 0x0f, 0x01, 0xda }, 1, { 0, 0, 0 }, 1 },
        { "vmsave", ISA_SVME, { 0x0f, 0x01, 0xdb }, 1, { 0, 0, 0 }, 1 },
        { "skinit", ISA_SVME, { 0x0f, 0x01, 0xde }, 1, { 0, 0, 0 }, 0 },
        { "invlpga", ISA_SVME, { 0x0f, 0x01, 0xdf }, 2, { 0, 1, 0 }, 1 },
        { "monitorx", ISA_MWAITX, { 0x0f, 0x01, 0xfa }, 3, { 0, 1, 2 }, 1 },
        { "mwaitx", ISA_MWAITX, { 0x0f, 0x01, 0xfb }, 3, { 0, 1, 3 }, 0 },
        { "clzero", ISA_CLZERO, { 0x0f, 0x01, 0xfc }, 1, { 0, 0, 0 }, 1 },
    };
    const ImplicitInsn *insn;
    unsigned i, j;

    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        insn = &tab[i];
        if (strcmp(mn, insn->name))
            continue;
        if (n != 0 && n != insn->noperand) {
            fail("bad operands for %s", mn);
            return 1;
        }
        if (n) {
            for (j = 0; j < insn->noperand; j++)
                if (!is_gpr(&op[j]) || op[j].reg != insn->reg[j] ||
                    (op[j].reg_size != 4 &&
                     !(j == 0 && insn->address_size_first && op[j].reg_size == 2))) {
                    fail("bad implicit-register operands for %s", mn);
                    return 1;
                }
        }
        if (!need_feature(insn->feature, mn))
            return 1;
        if (n && insn->address_size_first && op[0].reg_size == 2)
            emit8(0x67);
        emit8(insn->code[0]);
        emit8(insn->code[1]);
        emit8(insn->code[2]);
        return 1;
    }
    return 0;
}

typedef struct {
    const char *name;
    unsigned char sign_extend;
    unsigned char source_size;
    unsigned char destination_size;
} ExtendInsn;

static int emit_extend(const char *mn, Operand *op, int n)
{
    static const ExtendInsn tab[] = {
        { "movsx", 1, 0, 0 },  { "movsb", 1, 1, 0 },  { "movsw", 1, 2, 0 },
        { "movsbw", 1, 1, 2 }, { "movsbl", 1, 1, 4 }, { "movswl", 1, 2, 4 },
        { "movzx", 0, 0, 0 },  { "movzb", 0, 1, 0 },  { "movzw", 0, 2, 0 },
        { "movzbw", 0, 1, 2 }, { "movzbl", 0, 1, 4 }, { "movzwl", 0, 2, 4 },
    };
    const ExtendInsn *insn;
    unsigned i;
    int source_size, destination_size;

    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        insn = &tab[i];
        if (strcmp(mn, insn->name))
            continue;
        if (n == 0 && (!strcmp(mn, "movsb") || !strcmp(mn, "movsw")))
            return 0;
        if (n != 2 || !is_gpr(&op[1]) ||
            (op[0].kind != O_MEM && !is_gpr(&op[0]))) {
            fail("bad extension operands for %s", mn);
            return 1;
        }
        source_size = insn->source_size;
        if (!source_size) {
            if (!is_gpr(&op[0])) {
                fail("%s requires a sized register source", mn);
                return 1;
            }
            source_size = op[0].reg_size;
        }
        destination_size = insn->destination_size ? insn->destination_size : op[1].reg_size;
        if ((source_size != 1 && source_size != 2) ||
            (destination_size != 2 && destination_size != 4) ||
            source_size >= destination_size ||
            (is_gpr(&op[0]) && op[0].reg_size != source_size) ||
            op[1].reg_size != destination_size) {
            fail("bad extension sizes for %s", mn);
            return 1;
        }
        if (!need_feature(ISA_I386, mn))
            return 1;
        if (op[0].seg_prefix)
            emit8(op[0].seg_prefix);
        size_prefix(destination_size);
        emit8(0x0f);
        emit8((insn->sign_extend ? 0xbe : 0xb6) + (source_size == 2));
        emit_modrm(op[1].reg, &op[0]);
        return 1;
    }
    return 0;
}

static int emit_prefix_instruction(const char *mn, char *rest)
{
    static const struct {
        const char *name;
        unsigned char prefix;
        unsigned char address16;
    } tab[] = {
        { "lock", 0xf0, 0 },  { "rep", 0xf3, 0 },    { "repe", 0xf3, 0 },
        { "repz", 0xf3, 0 },  { "repne", 0xf2, 0 },  { "repnz", 0xf2, 0 },
        { "data16", 0x66, 0 }, { "addr16", 0, 1 },
    };
    unsigned i;

    if (!strcmp(mn, "addr32") || !strcmp(mn, "data32")) {
        fail("redundant %s prefix in 32-bit mode", mn);
        return 1;
    }
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
        if (!strcmp(mn, tab[i].name))
            break;
    if (i == sizeof(tab) / sizeof(tab[0]))
        return 0;
    if (!*rest) {
        fail("%s requires an instruction", mn);
        return 1;
    }
    if (tab[i].address16)
        force_addr16++;
    else
        emit8(tab[i].prefix);
    assemble_instruction(rest);
    if (tab[i].address16)
        force_addr16--;
    return 1;
}

static int emit_special_mov(const char *mn, Operand *op, int n)
{
    Operand *src, *dst;
    int opcode = -1, regfield = -1;
    if (strcmp(mn, "mov") && strcmp(mn, "movl") && strcmp(mn, "movw"))
        return 0;
    if (n != 2)
        return 0;
    src = &op[0];
    dst = &op[1];
    if (src->kind == O_REG && src->reg_class == RC_SEG) {
        opcode = 0x8c;
        regfield = src->reg;
        if (dst->reg_class != RC_GPR && dst->kind == O_REG)
            opcode = -1;
    } else if (dst->kind == O_REG && dst->reg_class == RC_SEG) {
        opcode = 0x8e;
        regfield = dst->reg;
        if (src->reg_class != RC_GPR && src->kind == O_REG)
            opcode = -1;
    } else if (src->kind == O_REG &&
               (src->reg_class == RC_CTRL || src->reg_class == RC_DEBUG ||
                src->reg_class == RC_TEST) &&
               is_gpr(dst)) {
        opcode = src->reg_class == RC_CTRL ? 0x20 : src->reg_class == RC_DEBUG ? 0x21 : 0x24;
        regfield = src->reg;
        emit8(0x0f);
    } else if (dst->kind == O_REG &&
               (dst->reg_class == RC_CTRL || dst->reg_class == RC_DEBUG ||
                dst->reg_class == RC_TEST) &&
               is_gpr(src)) {
        opcode = dst->reg_class == RC_CTRL ? 0x22 : dst->reg_class == RC_DEBUG ? 0x23 : 0x26;
        regfield = dst->reg;
        emit8(0x0f);
    } else
        return 0;
    if (opcode < 0) {
        fail("bad special-register move");
        return 1;
    }
    if ((src->reg_class == RC_CTRL || src->reg_class == RC_DEBUG || src->reg_class == RC_TEST ||
         dst->reg_class == RC_CTRL || dst->reg_class == RC_DEBUG || dst->reg_class == RC_TEST) &&
        !need_feature(ISA_I386, mn))
        return 1;
    emit8(opcode);
    emit_modrm(regfield, src->reg_class == RC_SEG || src->reg_class == RC_CTRL ||
                               src->reg_class == RC_DEBUG || src->reg_class == RC_TEST
                           ? dst
                           : src);
    return 1;
}

static int emit_segment_stack(const char *mn, Operand *op, int n)
{
    int pop, r;
    static const unsigned char push1[] = { 0x06, 0x0e, 0x16, 0x1e };
    static const unsigned char pop1[] = { 0x07, 0, 0x17, 0x1f };
    if (strcmp(mn, "push") && strcmp(mn, "pushw") && strcmp(mn, "pop") &&
        strcmp(mn, "popw"))
        return 0;
    if (n != 1 || op[0].kind != O_REG || op[0].reg_class != RC_SEG)
        return 0;
    pop = mn[0] == 'p' && mn[1] == 'o';
    r = op[0].reg;
    if (pop && r == 1) {
        fail("cannot pop %%cs");
        return 1;
    }
    if (r < 4)
        emit8(pop ? pop1[r] : push1[r]);
    else {
        emit8(0x0f);
        emit8((pop ? 0xa1 : 0xa0) + (r == 5 ? 8 : 0));
    }
    return 1;
}

static int emit_aad_aam(const char *mn, Operand *op, int n)
{
    static const struct {
        const char *name;
        unsigned char opcode;
    } tab[] = {
        { "aad", 0xd5 }, { "aam", 0xd4 },
    };
    unsigned i;

    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
        if (!strcmp(mn, tab[i].name))
            break;
    if (i == sizeof(tab) / sizeof(tab[0]))
        return 0;
    if (n > 1 || (n == 1 && op[0].kind != O_IMM)) {
        fail("bad %s operand", mn);
        return 1;
    }
    emit8(tab[i].opcode);
    if (n)
        emit_expr(&op[0].expr, 1, 0);
    else
        emit8(10);
    return 1;
}

static int emit_io(const char *mn, Operand *op, int n)
{
    int input, size, named_size, port_dx;

    if (mnemonic(mn, "in", &named_size))
        input = 1;
    else if (mnemonic(mn, "out", &named_size))
        input = 0;
    else
        return 0;
    if (!require_n(n, 2, mn))
        return 1;
    size = input ? op[1].reg_size : op[0].reg_size;
    if (!(input ? is_gpr(&op[1]) : is_gpr(&op[0])) ||
        (input ? op[1].reg : op[0].reg) != 0 || (size != 1 && size != 2 && size != 4)) {
        fail("%s requires %%al, %%ax, or %%eax", mn);
        return 1;
    }
    if (named_size && named_size != size) {
        fail("operand size does not match %s", mn);
        return 1;
    }
    port_dx = input ? is_gpr(&op[0]) && op[0].reg == 2 && op[0].reg_size == 2
                    : is_gpr(&op[1]) && op[1].reg == 2 && op[1].reg_size == 2;
    if (!port_dx && (input ? op[0].kind : op[1].kind) != O_IMM) {
        fail("%s port must be immediate or %%dx", mn);
        return 1;
    }
    size_prefix(size);
    emit8((input ? 0xe4 : 0xe6) + (size != 1) + (port_dx ? 8 : 0));
    if (!port_dx)
        emit_expr(input ? &op[0].expr : &op[1].expr, 1, 0);
    return 1;
}

static void emit_short_relative(Expr *e, unsigned opcode, unsigned reloc)
{
    unsigned off;
    emit8(opcode);
    off = cursec->size;
    emit8(0);
    add_fixup(cursec, off, 1, e, reloc);
}

static int local_branch_target(Expr *e)
{
    Expr copy = *e;
    reduce_expr(&copy);
    if (copy.nterm != 1 || copy.term[0].sign != 1 || !copy.term[0].sym)
        return 0;
    return local_name(copy.term[0].sym->name) ||
           (copy.term[0].sym->defined && !copy.term[0].sym->common &&
            copy.term[0].sym->sec == (int)(cursec - sections));
}

static int emit_loop(const char *mn, Operand *op, int n)
{
    static const struct {
        const char *name;
        unsigned char opcode;
        unsigned char address16;
    } tab[] = {
        { "loop", 0xe2, 0 },   { "loopl", 0xe2, 0 },
        { "loope", 0xe1, 0 },  { "loopz", 0xe1, 0 },
        { "loopne", 0xe0, 0 }, { "loopnz", 0xe0, 0 },
        { "jecxz", 0xe3, 0 },  { "jcxz", 0xe3, 1 },
    };
    unsigned i;

    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
        if (!strcmp(mn, tab[i].name))
            break;
    if (i == sizeof(tab) / sizeof(tab[0]))
        return 0;
    if (tab[i].address16)
        emit8(0x67);
    if (!require_n(n, 1, mn) || op[0].kind != O_MEM) {
        fail("bad short branch operand");
        return 1;
    }
    emit_short_relative(&op[0].expr, tab[i].opcode, FIX_PC8);
    return 1;
}

/*
 * Most later IA-32 extensions use the same prefix/map/opcode/ModR/M shape.
 * Keeping that shape in one table avoids importing binutils' generated opcode
 * machinery and makes every encoded byte independent of host byte order.
 */
#define OM_R8 (1u << 0)
#define OM_R16 (1u << 1)
#define OM_R32 (1u << 2)
#define OM_X87 (1u << 3)
#define OM_MMX (1u << 4)
#define OM_XMM (1u << 5)
#define OM_SEG (1u << 6)
#define OM_CTRL (1u << 7)
#define OM_DEBUG (1u << 8)
#define OM_TEST (1u << 9)
#define OM_MEM (1u << 10)
#define OM_IMM (1u << 11)
#define OM_GPR (OM_R8 | OM_R16 | OM_R32)
#define OM_RM8 (OM_R8 | OM_MEM)
#define OM_RM16 (OM_R16 | OM_MEM)
#define OM_RM32 (OM_R32 | OM_MEM)
#define OM_MMXRM (OM_MMX | OM_MEM)
#define OM_XMMRM (OM_XMM | OM_MEM)
#define MODRM_FIXED(n) (0x80 | (n))
#define NO_OPERAND 0xff

typedef struct {
    const char *name;
    IsaMask feature;
    unsigned short allow[3];
    unsigned char noperand;
    unsigned short prefix;
    unsigned char map;
    unsigned char opcode;
    unsigned char regop;
    unsigned char rmop;
    unsigned char immop;
    unsigned char immsize;
} TableInsn;

static unsigned operand_mask(const Operand *o)
{
    if (o->kind == O_IMM)
        return OM_IMM;
    if (o->kind == O_MEM)
        return OM_MEM;
    if (o->kind != O_REG)
        return 0;
    if (o->reg_class == RC_GPR)
        return o->reg_size == 1 ? OM_R8 : o->reg_size == 2 ? OM_R16 : OM_R32;
    if (o->reg_class == RC_X87)
        return OM_X87;
    if (o->reg_class == RC_MMX)
        return OM_MMX;
    if (o->reg_class == RC_XMM)
        return OM_XMM;
    if (o->reg_class == RC_SEG)
        return OM_SEG;
    if (o->reg_class == RC_CTRL)
        return OM_CTRL;
    if (o->reg_class == RC_DEBUG)
        return OM_DEBUG;
    return OM_TEST;
}

static void emit_opcode_map(unsigned prefix, unsigned map, unsigned opcode)
{
    if (prefix > 0xff)
        emit8(prefix >> 8);
    if (prefix)
        emit8(prefix);
    if (map) {
        emit8(0x0f);
        if (map == 2)
            emit8(0x38);
        else if (map == 3)
            emit8(0x3a);
    }
    emit8(opcode);
}

static int emit_table(const TableInsn *tab, unsigned count, const char *mn, Operand *op, int n)
{
    unsigned i, j;
    int saw_name = 0;
    for (i = 0; i < count; i++) {
        const TableInsn *t = &tab[i];
        Operand *rm;
        unsigned reg;
        if (strcmp(mn, t->name))
            continue;
        saw_name = 1;
        if (n != t->noperand)
            continue;
        for (j = 0; j < (unsigned)n; j++)
            if (!(operand_mask(&op[j]) & t->allow[j]))
                break;
        if (j != (unsigned)n)
            continue;
        if (!need_feature(t->feature, mn))
            return 1;
        rm = &op[t->rmop];
        if (rm->seg_prefix)
            emit8(rm->seg_prefix);
        emit_opcode_map(t->prefix, t->map, t->opcode);
        reg = t->regop & 0x80 ? t->regop & 7 : op[t->regop].reg;
        emit_modrm(reg, rm);
        if (t->immop != NO_OPERAND)
            emit_expr(&op[t->immop].expr, t->immsize, 0);
        return 1;
    }
    if (saw_name) {
        fail("bad operands for %s", mn);
        return 1;
    }
    return 0;
}

#define T2(n, f, a, b, p, m, o, r, x)                                                     \
    { n, f, { a, b, 0 }, 2, p, m, o, r, x, NO_OPERAND, 0 }
#define T1(n, f, a, p, m, o, r, x)                                                         \
    { n, f, { a, 0, 0 }, 1, p, m, o, r, x, NO_OPERAND, 0 }
#define T3I(n, f, a, b, c, p, m, o, r, x, ii)                                              \
    { n, f, { a, b, c }, 3, p, m, o, r, x, ii, 1 }
#define T2I(n, f, a, b, p, m, o, r, x, ii)                                                 \
    { n, f, { a, b, 0 }, 2, p, m, o, r, x, ii, 1 }

static int emit_legacy_table(const char *mn, Operand *op, int n)
{
    static const TableInsn tab[] = {
        T2("arpl", ISA_I386, OM_R16, OM_RM16, 0, 0, 0x63, 0, 1),
        T2("boundw", ISA_I386, OM_R16, OM_MEM, 0x66, 0, 0x62, 0, 1),
        T2("boundl", ISA_I386, OM_R32, OM_MEM, 0, 0, 0x62, 0, 1),
        T2("larw", ISA_I386, OM_RM16, OM_R16, 0x66, 1, 0x02, 1, 0),
        T2("larl", ISA_I386, OM_RM16, OM_R32, 0, 1, 0x02, 1, 0),
        T2("lslw", ISA_I386, OM_RM16, OM_R16, 0x66, 1, 0x03, 1, 0),
        T2("lsll", ISA_I386, OM_RM16, OM_R32, 0, 1, 0x03, 1, 0),
        T2("lssw", ISA_I386, OM_MEM, OM_R16, 0x66, 1, 0xb2, 1, 0),
        T2("lssl", ISA_I386, OM_MEM, OM_R32, 0, 1, 0xb2, 1, 0),
        T2("lfsw", ISA_I386, OM_MEM, OM_R16, 0x66, 1, 0xb4, 1, 0),
        T2("lfsl", ISA_I386, OM_MEM, OM_R32, 0, 1, 0xb4, 1, 0),
        T2("lgsw", ISA_I386, OM_MEM, OM_R16, 0x66, 1, 0xb5, 1, 0),
        T2("lgsl", ISA_I386, OM_MEM, OM_R32, 0, 1, 0xb5, 1, 0),
        T2("ldsw", ISA_I386, OM_MEM, OM_R16, 0x66, 0, 0xc5, 1, 0),
        T2("ldsl", ISA_I386, OM_MEM, OM_R32, 0, 0, 0xc5, 1, 0),
        T2("lesw", ISA_I386, OM_MEM, OM_R16, 0x66, 0, 0xc4, 1, 0),
        T2("lesl", ISA_I386, OM_MEM, OM_R32, 0, 0, 0xc4, 1, 0),
        T2("bsfw", ISA_I386, OM_RM16, OM_R16, 0x66, 1, 0xbc, 1, 0),
        T2("bsfl", ISA_I386, OM_RM32, OM_R32, 0, 1, 0xbc, 1, 0),
        T2("bsrw", ISA_I386, OM_RM16, OM_R16, 0x66, 1, 0xbd, 1, 0),
        T2("bsrl", ISA_I386, OM_RM32, OM_R32, 0, 1, 0xbd, 1, 0),
        T2("btw", ISA_I386, OM_R16, OM_RM16, 0x66, 1, 0xa3, 0, 1),
        T2("btl", ISA_I386, OM_R32, OM_RM32, 0, 1, 0xa3, 0, 1),
        T2("btcw", ISA_I386, OM_R16, OM_RM16, 0x66, 1, 0xbb, 0, 1),
        T2("btcl", ISA_I386, OM_R32, OM_RM32, 0, 1, 0xbb, 0, 1),
        T2("btrw", ISA_I386, OM_R16, OM_RM16, 0x66, 1, 0xb3, 0, 1),
        T2("btrl", ISA_I386, OM_R32, OM_RM32, 0, 1, 0xb3, 0, 1),
        T2("btsw", ISA_I386, OM_R16, OM_RM16, 0x66, 1, 0xab, 0, 1),
        T2("btsl", ISA_I386, OM_R32, OM_RM32, 0, 1, 0xab, 0, 1),
        T2I("btw", ISA_I386, OM_IMM, OM_RM16, 0x66, 1, 0xba, MODRM_FIXED(4), 1, 0),
        T2I("btl", ISA_I386, OM_IMM, OM_RM32, 0, 1, 0xba, MODRM_FIXED(4), 1, 0),
        T2I("btcw", ISA_I386, OM_IMM, OM_RM16, 0x66, 1, 0xba, MODRM_FIXED(7), 1, 0),
        T2I("btcl", ISA_I386, OM_IMM, OM_RM32, 0, 1, 0xba, MODRM_FIXED(7), 1, 0),
        T2I("btrw", ISA_I386, OM_IMM, OM_RM16, 0x66, 1, 0xba, MODRM_FIXED(6), 1, 0),
        T2I("btrl", ISA_I386, OM_IMM, OM_RM32, 0, 1, 0xba, MODRM_FIXED(6), 1, 0),
        T2I("btsw", ISA_I386, OM_IMM, OM_RM16, 0x66, 1, 0xba, MODRM_FIXED(5), 1, 0),
        T2I("btsl", ISA_I386, OM_IMM, OM_RM32, 0, 1, 0xba, MODRM_FIXED(5), 1, 0),
        T2("cmpxchgb", ISA_I486, OM_R8, OM_RM8, 0, 1, 0xb0, 0, 1),
        T2("cmpxchgw", ISA_I486, OM_R16, OM_RM16, 0x66, 1, 0xb1, 0, 1),
        T2("cmpxchgl", ISA_I486, OM_R32, OM_RM32, 0, 1, 0xb1, 0, 1),
        T2("xaddb", ISA_I486, OM_R8, OM_RM8, 0, 1, 0xc0, 0, 1),
        T2("xaddw", ISA_I486, OM_R16, OM_RM16, 0x66, 1, 0xc1, 0, 1),
        T2("xaddl", ISA_I486, OM_R32, OM_RM32, 0, 1, 0xc1, 0, 1),
        T1("cmpxchg8b", ISA_I586, OM_MEM, 0, 1, 0xc7, MODRM_FIXED(1), 0),
        T1("sldt", ISA_I386, OM_RM16, 0x66, 1, 0x00, MODRM_FIXED(0), 0),
        T1("str", ISA_I386, OM_RM16, 0x66, 1, 0x00, MODRM_FIXED(1), 0),
        T1("lldt", ISA_I386, OM_RM16, 0, 1, 0x00, MODRM_FIXED(2), 0),
        T1("ltr", ISA_I386, OM_RM16, 0, 1, 0x00, MODRM_FIXED(3), 0),
        T1("verr", ISA_I386, OM_RM16, 0, 1, 0x00, MODRM_FIXED(4), 0),
        T1("verw", ISA_I386, OM_RM16, 0, 1, 0x00, MODRM_FIXED(5), 0),
        T1("sgdt", ISA_I386, OM_MEM, 0, 1, 0x01, MODRM_FIXED(0), 0),
        T1("sidt", ISA_I386, OM_MEM, 0, 1, 0x01, MODRM_FIXED(1), 0),
        T1("lgdt", ISA_I386, OM_MEM, 0, 1, 0x01, MODRM_FIXED(2), 0),
        T1("lidt", ISA_I386, OM_MEM, 0, 1, 0x01, MODRM_FIXED(3), 0),
        T1("smsw", ISA_I386, OM_R16, 0x66, 1, 0x01, MODRM_FIXED(4), 0),
        T1("smsw", ISA_I386, OM_R32 | OM_MEM, 0, 1, 0x01, MODRM_FIXED(4), 0),
        T1("lmsw", ISA_I386, OM_RM16, 0, 1, 0x01, MODRM_FIXED(6), 0),
        T1("invlpg", ISA_I486, OM_MEM, 0, 1, 0x01, MODRM_FIXED(7), 0),
        T1("fxsave", ISA_FXSR, OM_MEM, 0, 1, 0xae, MODRM_FIXED(0), 0),
        T1("fxrstor", ISA_FXSR, OM_MEM, 0, 1, 0xae, MODRM_FIXED(1), 0),
        T1("ldmxcsr", ISA_SSE, OM_MEM, 0, 1, 0xae, MODRM_FIXED(2), 0),
        T1("stmxcsr", ISA_SSE, OM_MEM, 0, 1, 0xae, MODRM_FIXED(3), 0),
        T1("clflush", ISA_CLFLUSH, OM_MEM, 0, 1, 0xae, MODRM_FIXED(7), 0),
        T1("prefetchnta", ISA_MMXEXT, OM_MEM, 0, 1, 0x18, MODRM_FIXED(0), 0),
        T1("prefetcht0", ISA_MMXEXT, OM_MEM, 0, 1, 0x18, MODRM_FIXED(1), 0),
        T1("prefetcht1", ISA_MMXEXT, OM_MEM, 0, 1, 0x18, MODRM_FIXED(2), 0),
        T1("prefetcht2", ISA_MMXEXT, OM_MEM, 0, 1, 0x18, MODRM_FIXED(3), 0),
        T1("prefetch", ISA_3DNOW, OM_MEM, 0, 1, 0x0d, MODRM_FIXED(0), 0),
        T1("prefetchw", ISA_PRFCHW, OM_MEM, 0, 1, 0x0d, MODRM_FIXED(1), 0),
        T2("movnti", ISA_SSE2, OM_R32, OM_MEM, 0, 1, 0xc3, 0, 1),
        T2("popcntw", ISA_POPCNT, OM_RM16, OM_R16, 0x66f3, 1, 0xb8, 1, 0),
        T2("popcntl", ISA_POPCNT, OM_RM32, OM_R32, 0xf3, 1, 0xb8, 1, 0),
        T2("lzcntw", ISA_LZCNT, OM_RM16, OM_R16, 0x66f3, 1, 0xbd, 1, 0),
        T2("lzcntl", ISA_LZCNT, OM_RM32, OM_R32, 0xf3, 1, 0xbd, 1, 0),
        T1("nop", ISA_I686, OM_MEM, 0, 1, 0x1f, MODRM_FIXED(0), 0),
        T1("nopl", ISA_I686, OM_MEM, 0, 1, 0x1f, MODRM_FIXED(0), 0),
        T2("movbew", ISA_MOVBE, OM_MEM, OM_R16, 0x66, 2, 0xf0, 1, 0),
        T2("movbel", ISA_MOVBE, OM_MEM, OM_R32, 0, 2, 0xf0, 1, 0),
        T2("movbew", ISA_MOVBE, OM_R16, OM_MEM, 0x66, 2, 0xf1, 0, 1),
        T2("movbel", ISA_MOVBE, OM_R32, OM_MEM, 0, 2, 0xf1, 0, 1),
        T2("svdc", ISA_CYRIX, OM_SEG, OM_MEM, 0, 1, 0x78, 0, 1),
        T2("rsdc", ISA_CYRIX, OM_MEM, OM_SEG, 0, 1, 0x79, 1, 0),
        T1("svldt", ISA_CYRIX, OM_MEM, 0, 1, 0x7a, MODRM_FIXED(0), 0),
        T1("rsldt", ISA_CYRIX, OM_MEM, 0, 1, 0x7b, MODRM_FIXED(0), 0),
        T1("svts", ISA_CYRIX, OM_MEM, 0, 1, 0x7c, MODRM_FIXED(0), 0),
        T1("rsts", ISA_CYRIX, OM_MEM, 0, 1, 0x7d, MODRM_FIXED(0), 0),
        T2("ud1w", ISA_I386, OM_RM16, OM_R16, 0x66, 1, 0xb9, 1, 0),
        T2("ud1l", ISA_I386, OM_RM32, OM_R32, 0, 1, 0xb9, 1, 0),
        T2("ud1", ISA_I386, OM_RM16, OM_R16, 0x66, 1, 0xb9, 1, 0),
        T2("ud1", ISA_I386, OM_RM32, OM_R32, 0, 1, 0xb9, 1, 0),
        T2("ud2bw", ISA_I386, OM_RM16, OM_R16, 0x66, 1, 0xb9, 1, 0),
        T2("ud2bl", ISA_I386, OM_RM32, OM_R32, 0, 1, 0xb9, 1, 0),
        T2("ud2b", ISA_I386, OM_RM16, OM_R16, 0x66, 1, 0xb9, 1, 0),
        T2("ud2b", ISA_I386, OM_RM32, OM_R32, 0, 1, 0xb9, 1, 0),
        T2("ud0w", ISA_I386, OM_RM16, OM_R16, 0x66, 1, 0xff, 1, 0),
        T2("ud0l", ISA_I386, OM_RM32, OM_R32, 0, 1, 0xff, 1, 0),
        T2("ud0", ISA_I386, OM_RM16, OM_R16, 0x66, 1, 0xff, 1, 0),
        T2("ud0", ISA_I386, OM_RM32, OM_R32, 0, 1, 0xff, 1, 0),
        T1("vmclear", ISA_VMX, OM_MEM, 0x66, 1, 0xc7, MODRM_FIXED(6), 0),
        T1("vmptrld", ISA_VMX, OM_MEM, 0, 1, 0xc7, MODRM_FIXED(6), 0),
        T1("vmptrst", ISA_VMX, OM_MEM, 0, 1, 0xc7, MODRM_FIXED(7), 0),
        T1("vmxon", ISA_VMX, OM_MEM, 0xf3, 1, 0xc7, MODRM_FIXED(6), 0),
        T2("vmread", ISA_VMX, OM_R32, OM_RM32, 0, 1, 0x78, 0, 1),
        T2("vmwrite", ISA_VMX, OM_RM32, OM_R32, 0, 1, 0x79, 1, 0),
        T2("invept", ISA_EPT, OM_MEM, OM_R32, 0x66, 2, 0x80, 1, 0),
        T2("invvpid", ISA_EPT, OM_MEM, OM_R32, 0x66, 2, 0x81, 1, 0),
        T1("xsave", ISA_XSAVE, OM_MEM, 0, 1, 0xae, MODRM_FIXED(4), 0),
        T1("xrstor", ISA_XSAVE, OM_MEM, 0, 1, 0xae, MODRM_FIXED(5), 0),
        T1("xsaveopt", ISA_XSAVEOPT, OM_MEM, 0, 1, 0xae, MODRM_FIXED(6), 0),
    };
    return emit_table(tab, sizeof(tab) / sizeof(tab[0]), mn, op, n);
}

#define RR(n, f, p, m, o, rm, r) T2(n, f, rm, r, p, m, o, 1, 0)
#define ST(n, f, p, m, o, r, rm) T2(n, f, r, rm, p, m, o, 0, 1)
#define RI(n, f, p, m, o, rm, r) T3I(n, f, OM_IMM, rm, r, p, m, o, 2, 1, 0)
#define SI(n, f, p, m, o, slash, rm)                                                       \
    T2I(n, f, OM_IMM, rm, p, m, o, MODRM_FIXED(slash), 1, 0)

static int emit_simd_table(const char *mn, Operand *op, int n)
{
    static const TableInsn tab[] = {
        /* Original MMX integer operations. */
        RR("packsswb", ISA_MMX, 0, 1, 0x63, OM_MMXRM, OM_MMX),
        RR("packssdw", ISA_MMX, 0, 1, 0x6b, OM_MMXRM, OM_MMX),
        RR("packuswb", ISA_MMX, 0, 1, 0x67, OM_MMXRM, OM_MMX),
        RR("paddb", ISA_MMX, 0, 1, 0xfc, OM_MMXRM, OM_MMX),
        RR("paddw", ISA_MMX, 0, 1, 0xfd, OM_MMXRM, OM_MMX),
        RR("paddd", ISA_MMX, 0, 1, 0xfe, OM_MMXRM, OM_MMX),
        RR("paddsb", ISA_MMX, 0, 1, 0xec, OM_MMXRM, OM_MMX),
        RR("paddsw", ISA_MMX, 0, 1, 0xed, OM_MMXRM, OM_MMX),
        RR("paddusb", ISA_MMX, 0, 1, 0xdc, OM_MMXRM, OM_MMX),
        RR("paddusw", ISA_MMX, 0, 1, 0xdd, OM_MMXRM, OM_MMX),
        RR("pand", ISA_MMX, 0, 1, 0xdb, OM_MMXRM, OM_MMX),
        RR("pandn", ISA_MMX, 0, 1, 0xdf, OM_MMXRM, OM_MMX),
        RR("pcmpeqb", ISA_MMX, 0, 1, 0x74, OM_MMXRM, OM_MMX),
        RR("pcmpeqw", ISA_MMX, 0, 1, 0x75, OM_MMXRM, OM_MMX),
        RR("pcmpeqd", ISA_MMX, 0, 1, 0x76, OM_MMXRM, OM_MMX),
        RR("pcmpgtb", ISA_MMX, 0, 1, 0x64, OM_MMXRM, OM_MMX),
        RR("pcmpgtw", ISA_MMX, 0, 1, 0x65, OM_MMXRM, OM_MMX),
        RR("pcmpgtd", ISA_MMX, 0, 1, 0x66, OM_MMXRM, OM_MMX),
        RR("pmaddwd", ISA_MMX, 0, 1, 0xf5, OM_MMXRM, OM_MMX),
        RR("pmulhw", ISA_MMX, 0, 1, 0xe5, OM_MMXRM, OM_MMX),
        RR("pmullw", ISA_MMX, 0, 1, 0xd5, OM_MMXRM, OM_MMX),
        RR("por", ISA_MMX, 0, 1, 0xeb, OM_MMXRM, OM_MMX),
        RR("psubb", ISA_MMX, 0, 1, 0xf8, OM_MMXRM, OM_MMX),
        RR("psubw", ISA_MMX, 0, 1, 0xf9, OM_MMXRM, OM_MMX),
        RR("psubd", ISA_MMX, 0, 1, 0xfa, OM_MMXRM, OM_MMX),
        RR("psubsb", ISA_MMX, 0, 1, 0xe8, OM_MMXRM, OM_MMX),
        RR("psubsw", ISA_MMX, 0, 1, 0xe9, OM_MMXRM, OM_MMX),
        RR("psubusb", ISA_MMX, 0, 1, 0xd8, OM_MMXRM, OM_MMX),
        RR("psubusw", ISA_MMX, 0, 1, 0xd9, OM_MMXRM, OM_MMX),
        RR("punpckhbw", ISA_MMX, 0, 1, 0x68, OM_MMXRM, OM_MMX),
        RR("punpckhwd", ISA_MMX, 0, 1, 0x69, OM_MMXRM, OM_MMX),
        RR("punpckhdq", ISA_MMX, 0, 1, 0x6a, OM_MMXRM, OM_MMX),
        RR("punpcklbw", ISA_MMX, 0, 1, 0x60, OM_MMXRM, OM_MMX),
        RR("punpcklwd", ISA_MMX, 0, 1, 0x61, OM_MMXRM, OM_MMX),
        RR("punpckldq", ISA_MMX, 0, 1, 0x62, OM_MMXRM, OM_MMX),
        RR("pxor", ISA_MMX, 0, 1, 0xef, OM_MMXRM, OM_MMX),
        RR("psllw", ISA_MMX, 0, 1, 0xf1, OM_MMXRM, OM_MMX),
        RR("pslld", ISA_MMX, 0, 1, 0xf2, OM_MMXRM, OM_MMX),
        RR("psllq", ISA_MMX, 0, 1, 0xf3, OM_MMXRM, OM_MMX),
        RR("psraw", ISA_MMX, 0, 1, 0xe1, OM_MMXRM, OM_MMX),
        RR("psrad", ISA_MMX, 0, 1, 0xe2, OM_MMXRM, OM_MMX),
        RR("psrlw", ISA_MMX, 0, 1, 0xd1, OM_MMXRM, OM_MMX),
        RR("psrld", ISA_MMX, 0, 1, 0xd2, OM_MMXRM, OM_MMX),
        RR("psrlq", ISA_MMX, 0, 1, 0xd3, OM_MMXRM, OM_MMX),
        SI("psllw", ISA_MMX, 0, 1, 0x71, 6, OM_MMX),
        SI("pslld", ISA_MMX, 0, 1, 0x72, 6, OM_MMX),
        SI("psllq", ISA_MMX, 0, 1, 0x73, 6, OM_MMX),
        SI("psraw", ISA_MMX, 0, 1, 0x71, 4, OM_MMX),
        SI("psrad", ISA_MMX, 0, 1, 0x72, 4, OM_MMX),
        SI("psrlw", ISA_MMX, 0, 1, 0x71, 2, OM_MMX),
        SI("psrld", ISA_MMX, 0, 1, 0x72, 2, OM_MMX),
        SI("psrlq", ISA_MMX, 0, 1, 0x73, 2, OM_MMX),
        RI("pshufw", ISA_MMX | ISA_MMXEXT, 0, 1, 0x70, OM_MMXRM, OM_MMX),
        RR("pavgb", ISA_MMX | ISA_MMXEXT, 0, 1, 0xe0, OM_MMXRM, OM_MMX),
        RR("pavgw", ISA_MMX | ISA_MMXEXT, 0, 1, 0xe3, OM_MMXRM, OM_MMX),
        RR("pmaxsw", ISA_MMX | ISA_MMXEXT, 0, 1, 0xee, OM_MMXRM, OM_MMX),
        RR("pmaxub", ISA_MMX | ISA_MMXEXT, 0, 1, 0xde, OM_MMXRM, OM_MMX),
        RR("pminsw", ISA_MMX | ISA_MMXEXT, 0, 1, 0xea, OM_MMXRM, OM_MMX),
        RR("pminub", ISA_MMX | ISA_MMXEXT, 0, 1, 0xda, OM_MMXRM, OM_MMX),
        RR("pmulhuw", ISA_MMX | ISA_MMXEXT, 0, 1, 0xe4, OM_MMXRM, OM_MMX),
        RR("psadbw", ISA_MMX | ISA_MMXEXT, 0, 1, 0xf6, OM_MMXRM, OM_MMX),
        RR("paddq", ISA_SSE2, 0, 1, 0xd4, OM_MMXRM, OM_MMX),
        RR("psubq", ISA_SSE2, 0, 1, 0xfb, OM_MMXRM, OM_MMX),
        RR("pmuludq", ISA_SSE2, 0, 1, 0xf4, OM_MMXRM, OM_MMX),
        RR("movd", ISA_MMX, 0, 1, 0x6e, OM_RM32, OM_MMX),
        ST("movd", ISA_MMX, 0, 1, 0x7e, OM_MMX, OM_RM32),
        RR("movq", ISA_MMX, 0, 1, 0x6f, OM_MMXRM, OM_MMX),
        ST("movq", ISA_MMX, 0, 1, 0x7f, OM_MMX, OM_MMXRM),
        T3I("pinsrw", ISA_MMX | ISA_MMXEXT, OM_IMM, OM_R16 | OM_R32 | OM_MEM, OM_MMX, 0, 1, 0xc4, 2, 1, 0),
        T3I("pextrw", ISA_MMX | ISA_MMXEXT, OM_IMM, OM_MMX, OM_R32, 0, 1, 0xc5, 2, 1, 0),
        T2("pmovmskb", ISA_MMX | ISA_MMXEXT, OM_MMX, OM_R32, 0, 1, 0xd7, 1, 0),
        ST("movntq", ISA_MMX | ISA_MMXEXT, 0, 1, 0xe7, OM_MMX, OM_MEM),
        T2("maskmovq", ISA_MMX | ISA_MMXEXT, OM_MMX, OM_MMX, 0, 1, 0xf7, 1, 0),

        /* Scalar and packed single precision SSE. */
        RR("movups", ISA_SSE, 0, 1, 0x10, OM_XMMRM, OM_XMM),
        ST("movups", ISA_SSE, 0, 1, 0x11, OM_XMM, OM_XMMRM),
        RR("movaps", ISA_SSE, 0, 1, 0x28, OM_XMMRM, OM_XMM),
        ST("movaps", ISA_SSE, 0, 1, 0x29, OM_XMM, OM_XMMRM),
        RR("movss", ISA_SSE, 0xf3, 1, 0x10, OM_XMMRM, OM_XMM),
        ST("movss", ISA_SSE, 0xf3, 1, 0x11, OM_XMM, OM_XMMRM),
        RR("movlps", ISA_SSE, 0, 1, 0x12, OM_MEM, OM_XMM),
        ST("movlps", ISA_SSE, 0, 1, 0x13, OM_XMM, OM_MEM),
        RR("movhps", ISA_SSE, 0, 1, 0x16, OM_MEM, OM_XMM),
        ST("movhps", ISA_SSE, 0, 1, 0x17, OM_XMM, OM_MEM),
        RR("movhlps", ISA_SSE, 0, 1, 0x12, OM_XMM, OM_XMM),
        RR("movlhps", ISA_SSE, 0, 1, 0x16, OM_XMM, OM_XMM),
        RR("unpcklps", ISA_SSE, 0, 1, 0x14, OM_XMMRM, OM_XMM),
        RR("unpckhps", ISA_SSE, 0, 1, 0x15, OM_XMMRM, OM_XMM),
        RR("sqrtps", ISA_SSE, 0, 1, 0x51, OM_XMMRM, OM_XMM),
        RR("sqrtss", ISA_SSE, 0xf3, 1, 0x51, OM_XMMRM, OM_XMM),
        RR("rsqrtps", ISA_SSE, 0, 1, 0x52, OM_XMMRM, OM_XMM),
        RR("rsqrtss", ISA_SSE, 0xf3, 1, 0x52, OM_XMMRM, OM_XMM),
        RR("rcpps", ISA_SSE, 0, 1, 0x53, OM_XMMRM, OM_XMM),
        RR("rcpss", ISA_SSE, 0xf3, 1, 0x53, OM_XMMRM, OM_XMM),
        RR("andps", ISA_SSE, 0, 1, 0x54, OM_XMMRM, OM_XMM),
        RR("andnps", ISA_SSE, 0, 1, 0x55, OM_XMMRM, OM_XMM),
        RR("orps", ISA_SSE, 0, 1, 0x56, OM_XMMRM, OM_XMM),
        RR("xorps", ISA_SSE, 0, 1, 0x57, OM_XMMRM, OM_XMM),
        RR("addps", ISA_SSE, 0, 1, 0x58, OM_XMMRM, OM_XMM),
        RR("addss", ISA_SSE, 0xf3, 1, 0x58, OM_XMMRM, OM_XMM),
        RR("mulps", ISA_SSE, 0, 1, 0x59, OM_XMMRM, OM_XMM),
        RR("mulss", ISA_SSE, 0xf3, 1, 0x59, OM_XMMRM, OM_XMM),
        RR("subps", ISA_SSE, 0, 1, 0x5c, OM_XMMRM, OM_XMM),
        RR("subss", ISA_SSE, 0xf3, 1, 0x5c, OM_XMMRM, OM_XMM),
        RR("minps", ISA_SSE, 0, 1, 0x5d, OM_XMMRM, OM_XMM),
        RR("minss", ISA_SSE, 0xf3, 1, 0x5d, OM_XMMRM, OM_XMM),
        RR("divps", ISA_SSE, 0, 1, 0x5e, OM_XMMRM, OM_XMM),
        RR("divss", ISA_SSE, 0xf3, 1, 0x5e, OM_XMMRM, OM_XMM),
        RR("maxps", ISA_SSE, 0, 1, 0x5f, OM_XMMRM, OM_XMM),
        RR("maxss", ISA_SSE, 0xf3, 1, 0x5f, OM_XMMRM, OM_XMM),
        RI("cmpps", ISA_SSE, 0, 1, 0xc2, OM_XMMRM, OM_XMM),
        RI("cmpss", ISA_SSE, 0xf3, 1, 0xc2, OM_XMMRM, OM_XMM),
        RI("shufps", ISA_SSE, 0, 1, 0xc6, OM_XMMRM, OM_XMM),
        RR("ucomiss", ISA_SSE, 0, 1, 0x2e, OM_XMMRM, OM_XMM),
        RR("comiss", ISA_SSE, 0, 1, 0x2f, OM_XMMRM, OM_XMM),
        T2("movmskps", ISA_SSE, OM_XMM, OM_R32, 0, 1, 0x50, 1, 0),
        RR("cvtpi2ps", ISA_SSE, 0, 1, 0x2a, OM_MMXRM, OM_XMM),
        RR("cvtsi2ss", ISA_SSE, 0xf3, 1, 0x2a, OM_RM32, OM_XMM),
        T2("cvttps2pi", ISA_SSE, OM_XMMRM, OM_MMX, 0, 1, 0x2c, 1, 0),
        T2("cvttss2si", ISA_SSE, OM_XMMRM, OM_R32, 0xf3, 1, 0x2c, 1, 0),
        T2("cvtps2pi", ISA_SSE, OM_XMMRM, OM_MMX, 0, 1, 0x2d, 1, 0),
        T2("cvtss2si", ISA_SSE, OM_XMMRM, OM_R32, 0xf3, 1, 0x2d, 1, 0),
        ST("movntps", ISA_SSE, 0, 1, 0x2b, OM_XMM, OM_MEM),

        /* SSE2 packed-double, scalar-double, and XMM integer operations. */
        RR("movupd", ISA_SSE2, 0x66, 1, 0x10, OM_XMMRM, OM_XMM),
        ST("movupd", ISA_SSE2, 0x66, 1, 0x11, OM_XMM, OM_XMMRM),
        RR("movapd", ISA_SSE2, 0x66, 1, 0x28, OM_XMMRM, OM_XMM),
        ST("movapd", ISA_SSE2, 0x66, 1, 0x29, OM_XMM, OM_XMMRM),
        RR("movsd", ISA_SSE2, 0xf2, 1, 0x10, OM_XMMRM, OM_XMM),
        ST("movsd", ISA_SSE2, 0xf2, 1, 0x11, OM_XMM, OM_XMMRM),
        RR("movlpd", ISA_SSE2, 0x66, 1, 0x12, OM_MEM, OM_XMM),
        ST("movlpd", ISA_SSE2, 0x66, 1, 0x13, OM_XMM, OM_MEM),
        RR("movhpd", ISA_SSE2, 0x66, 1, 0x16, OM_MEM, OM_XMM),
        ST("movhpd", ISA_SSE2, 0x66, 1, 0x17, OM_XMM, OM_MEM),
        RR("unpcklpd", ISA_SSE2, 0x66, 1, 0x14, OM_XMMRM, OM_XMM),
        RR("unpckhpd", ISA_SSE2, 0x66, 1, 0x15, OM_XMMRM, OM_XMM),
        RR("sqrtpd", ISA_SSE2, 0x66, 1, 0x51, OM_XMMRM, OM_XMM),
        RR("sqrtsd", ISA_SSE2, 0xf2, 1, 0x51, OM_XMMRM, OM_XMM),
        RR("andpd", ISA_SSE2, 0x66, 1, 0x54, OM_XMMRM, OM_XMM),
        RR("andnpd", ISA_SSE2, 0x66, 1, 0x55, OM_XMMRM, OM_XMM),
        RR("orpd", ISA_SSE2, 0x66, 1, 0x56, OM_XMMRM, OM_XMM),
        RR("xorpd", ISA_SSE2, 0x66, 1, 0x57, OM_XMMRM, OM_XMM),
        RR("addpd", ISA_SSE2, 0x66, 1, 0x58, OM_XMMRM, OM_XMM),
        RR("addsd", ISA_SSE2, 0xf2, 1, 0x58, OM_XMMRM, OM_XMM),
        RR("mulpd", ISA_SSE2, 0x66, 1, 0x59, OM_XMMRM, OM_XMM),
        RR("mulsd", ISA_SSE2, 0xf2, 1, 0x59, OM_XMMRM, OM_XMM),
        RR("subpd", ISA_SSE2, 0x66, 1, 0x5c, OM_XMMRM, OM_XMM),
        RR("subsd", ISA_SSE2, 0xf2, 1, 0x5c, OM_XMMRM, OM_XMM),
        RR("minpd", ISA_SSE2, 0x66, 1, 0x5d, OM_XMMRM, OM_XMM),
        RR("minsd", ISA_SSE2, 0xf2, 1, 0x5d, OM_XMMRM, OM_XMM),
        RR("divpd", ISA_SSE2, 0x66, 1, 0x5e, OM_XMMRM, OM_XMM),
        RR("divsd", ISA_SSE2, 0xf2, 1, 0x5e, OM_XMMRM, OM_XMM),
        RR("maxpd", ISA_SSE2, 0x66, 1, 0x5f, OM_XMMRM, OM_XMM),
        RR("maxsd", ISA_SSE2, 0xf2, 1, 0x5f, OM_XMMRM, OM_XMM),
        RI("cmppd", ISA_SSE2, 0x66, 1, 0xc2, OM_XMMRM, OM_XMM),
        RI("cmpsd", ISA_SSE2, 0xf2, 1, 0xc2, OM_XMMRM, OM_XMM),
        RI("shufpd", ISA_SSE2, 0x66, 1, 0xc6, OM_XMMRM, OM_XMM),
        RR("ucomisd", ISA_SSE2, 0x66, 1, 0x2e, OM_XMMRM, OM_XMM),
        RR("comisd", ISA_SSE2, 0x66, 1, 0x2f, OM_XMMRM, OM_XMM),
        T2("movmskpd", ISA_SSE2, OM_XMM, OM_R32, 0x66, 1, 0x50, 1, 0),
        RR("cvtpi2pd", ISA_SSE2, 0x66, 1, 0x2a, OM_MMXRM, OM_XMM),
        RR("cvtsi2sd", ISA_SSE2, 0xf2, 1, 0x2a, OM_RM32, OM_XMM),
        T2("cvttpd2pi", ISA_SSE2, OM_XMMRM, OM_MMX, 0x66, 1, 0x2c, 1, 0),
        T2("cvttsd2si", ISA_SSE2, OM_XMMRM, OM_R32, 0xf2, 1, 0x2c, 1, 0),
        T2("cvtpd2pi", ISA_SSE2, OM_XMMRM, OM_MMX, 0x66, 1, 0x2d, 1, 0),
        T2("cvtsd2si", ISA_SSE2, OM_XMMRM, OM_R32, 0xf2, 1, 0x2d, 1, 0),
        RR("cvtps2pd", ISA_SSE2, 0, 1, 0x5a, OM_XMMRM, OM_XMM),
        RR("cvtss2sd", ISA_SSE2, 0xf3, 1, 0x5a, OM_XMMRM, OM_XMM),
        RR("cvtpd2ps", ISA_SSE2, 0x66, 1, 0x5a, OM_XMMRM, OM_XMM),
        RR("cvtsd2ss", ISA_SSE2, 0xf2, 1, 0x5a, OM_XMMRM, OM_XMM),
        RR("cvtdq2ps", ISA_SSE2, 0, 1, 0x5b, OM_XMMRM, OM_XMM),
        RR("cvttps2dq", ISA_SSE2, 0xf3, 1, 0x5b, OM_XMMRM, OM_XMM),
        RR("cvtps2dq", ISA_SSE2, 0x66, 1, 0x5b, OM_XMMRM, OM_XMM),
        ST("movntpd", ISA_SSE2, 0x66, 1, 0x2b, OM_XMM, OM_MEM),
        RR("movd", ISA_SSE2, 0x66, 1, 0x6e, OM_RM32, OM_XMM),
        ST("movd", ISA_SSE2, 0x66, 1, 0x7e, OM_XMM, OM_RM32),
        RR("movq", ISA_SSE2, 0xf3, 1, 0x7e, OM_XMMRM, OM_XMM),
        ST("movq", ISA_SSE2, 0x66, 1, 0xd6, OM_XMM, OM_XMMRM),
        RR("movdqa", ISA_SSE2, 0x66, 1, 0x6f, OM_XMMRM, OM_XMM),
        ST("movdqa", ISA_SSE2, 0x66, 1, 0x7f, OM_XMM, OM_XMMRM),
        RR("movdqu", ISA_SSE2, 0xf3, 1, 0x6f, OM_XMMRM, OM_XMM),
        ST("movdqu", ISA_SSE2, 0xf3, 1, 0x7f, OM_XMM, OM_XMMRM),
        RI("pshufd", ISA_SSE2, 0x66, 1, 0x70, OM_XMMRM, OM_XMM),
        RI("pshufhw", ISA_SSE2, 0xf3, 1, 0x70, OM_XMMRM, OM_XMM),
        RI("pshuflw", ISA_SSE2, 0xf2, 1, 0x70, OM_XMMRM, OM_XMM),
        RR("punpcklqdq", ISA_SSE2, 0x66, 1, 0x6c, OM_XMMRM, OM_XMM),
        RR("punpckhqdq", ISA_SSE2, 0x66, 1, 0x6d, OM_XMMRM, OM_XMM),
#define XMMI(n, f, o) RR(n, f, 0x66, 1, o, OM_XMMRM, OM_XMM)
        XMMI("packsswb", ISA_SSE2, 0x63), XMMI("packssdw", ISA_SSE2, 0x6b),
        XMMI("packuswb", ISA_SSE2, 0x67), XMMI("paddb", ISA_SSE2, 0xfc),
        XMMI("paddw", ISA_SSE2, 0xfd), XMMI("paddd", ISA_SSE2, 0xfe),
        XMMI("paddq", ISA_SSE2, 0xd4), XMMI("paddsb", ISA_SSE2, 0xec),
        XMMI("paddsw", ISA_SSE2, 0xed), XMMI("paddusb", ISA_SSE2, 0xdc),
        XMMI("paddusw", ISA_SSE2, 0xdd), XMMI("pand", ISA_SSE2, 0xdb),
        XMMI("pandn", ISA_SSE2, 0xdf), XMMI("pcmpeqb", ISA_SSE2, 0x74),
        XMMI("pcmpeqw", ISA_SSE2, 0x75), XMMI("pcmpeqd", ISA_SSE2, 0x76),
        XMMI("pcmpgtb", ISA_SSE2, 0x64), XMMI("pcmpgtw", ISA_SSE2, 0x65),
        XMMI("pcmpgtd", ISA_SSE2, 0x66), XMMI("pmaddwd", ISA_SSE2, 0xf5),
        XMMI("pmulhuw", ISA_SSE2, 0xe4), XMMI("pmulhw", ISA_SSE2, 0xe5),
        XMMI("pmullw", ISA_SSE2, 0xd5), XMMI("pmuludq", ISA_SSE2, 0xf4),
        XMMI("por", ISA_SSE2, 0xeb), XMMI("psadbw", ISA_SSE2, 0xf6),
        XMMI("psubb", ISA_SSE2, 0xf8), XMMI("psubw", ISA_SSE2, 0xf9),
        XMMI("psubd", ISA_SSE2, 0xfa), XMMI("psubq", ISA_SSE2, 0xfb),
        XMMI("psubsb", ISA_SSE2, 0xe8), XMMI("psubsw", ISA_SSE2, 0xe9),
        XMMI("psubusb", ISA_SSE2, 0xd8), XMMI("psubusw", ISA_SSE2, 0xd9),
        XMMI("punpckhbw", ISA_SSE2, 0x68), XMMI("punpckhwd", ISA_SSE2, 0x69),
        XMMI("punpckhdq", ISA_SSE2, 0x6a), XMMI("punpcklbw", ISA_SSE2, 0x60),
        XMMI("punpcklwd", ISA_SSE2, 0x61), XMMI("punpckldq", ISA_SSE2, 0x62),
        XMMI("pxor", ISA_SSE2, 0xef), XMMI("psllw", ISA_SSE2, 0xf1),
        XMMI("pslld", ISA_SSE2, 0xf2), XMMI("psllq", ISA_SSE2, 0xf3),
        XMMI("psraw", ISA_SSE2, 0xe1), XMMI("psrad", ISA_SSE2, 0xe2),
        XMMI("psrlw", ISA_SSE2, 0xd1), XMMI("psrld", ISA_SSE2, 0xd2),
        XMMI("psrlq", ISA_SSE2, 0xd3),
#undef XMMI
        SI("psllw", ISA_SSE2, 0x66, 1, 0x71, 6, OM_XMM),
        SI("pslld", ISA_SSE2, 0x66, 1, 0x72, 6, OM_XMM),
        SI("psllq", ISA_SSE2, 0x66, 1, 0x73, 6, OM_XMM),
        SI("psraw", ISA_SSE2, 0x66, 1, 0x71, 4, OM_XMM),
        SI("psrad", ISA_SSE2, 0x66, 1, 0x72, 4, OM_XMM),
        SI("psrlw", ISA_SSE2, 0x66, 1, 0x71, 2, OM_XMM),
        SI("psrld", ISA_SSE2, 0x66, 1, 0x72, 2, OM_XMM),
        SI("psrlq", ISA_SSE2, 0x66, 1, 0x73, 2, OM_XMM),
        SI("pslldq", ISA_SSE2, 0x66, 1, 0x73, 7, OM_XMM),
        SI("psrldq", ISA_SSE2, 0x66, 1, 0x73, 3, OM_XMM),
        T3I("pinsrw", ISA_SSE2, OM_IMM, OM_R16 | OM_R32 | OM_MEM, OM_XMM, 0x66, 1, 0xc4, 2, 1, 0),
        T3I("pextrw", ISA_SSE2, OM_IMM, OM_XMM, OM_R32, 0x66, 1, 0xc5, 2, 1, 0),
        T2("pmovmskb", ISA_SSE2, OM_XMM, OM_R32, 0x66, 1, 0xd7, 1, 0),
        ST("movntdq", ISA_SSE2, 0x66, 1, 0xe7, OM_XMM, OM_MEM),
        T2("maskmovdqu", ISA_SSE2, OM_XMM, OM_XMM, 0x66, 1, 0xf7, 1, 0),
        RR("cvtpd2dq", ISA_SSE2, 0xf2, 1, 0xe6, OM_XMMRM, OM_XMM),
        RR("cvttpd2dq", ISA_SSE2, 0x66, 1, 0xe6, OM_XMMRM, OM_XMM),
        RR("cvtdq2pd", ISA_SSE2, 0xf3, 1, 0xe6, OM_XMMRM, OM_XMM),
        T2("movq2dq", ISA_SSE2, OM_MMX, OM_XMM, 0xf3, 1, 0xd6, 1, 0),
        T2("movdq2q", ISA_SSE2, OM_XMM, OM_MMX, 0xf2, 1, 0xd6, 1, 0),

        /* SSE3 horizontal arithmetic and duplicate-load operations. */
        RR("addsubpd", ISA_SSE3, 0x66, 1, 0xd0, OM_XMMRM, OM_XMM),
        RR("addsubps", ISA_SSE3, 0xf2, 1, 0xd0, OM_XMMRM, OM_XMM),
        RR("haddpd", ISA_SSE3, 0x66, 1, 0x7c, OM_XMMRM, OM_XMM),
        RR("haddps", ISA_SSE3, 0xf2, 1, 0x7c, OM_XMMRM, OM_XMM),
        RR("hsubpd", ISA_SSE3, 0x66, 1, 0x7d, OM_XMMRM, OM_XMM),
        RR("hsubps", ISA_SSE3, 0xf2, 1, 0x7d, OM_XMMRM, OM_XMM),
        RR("lddqu", ISA_SSE3, 0xf2, 1, 0xf0, OM_MEM, OM_XMM),
        RR("movddup", ISA_SSE3, 0xf2, 1, 0x12, OM_XMMRM, OM_XMM),
        RR("movshdup", ISA_SSE3, 0xf3, 1, 0x16, OM_XMMRM, OM_XMM),
        RR("movsldup", ISA_SSE3, 0xf3, 1, 0x12, OM_XMMRM, OM_XMM),

        /* SSSE3: both MMX and XMM encodings use the 0f38/0f3a maps. */
#define SS3(n, o)                                                                          \
        RR(n, ISA_SSSE3, 0, 2, o, OM_MMXRM, OM_MMX),                                      \
            RR(n, ISA_SSSE3, 0x66, 2, o, OM_XMMRM, OM_XMM)
        SS3("pshufb", 0x00), SS3("phaddw", 0x01), SS3("phaddd", 0x02),
        SS3("phaddsw", 0x03), SS3("pmaddubsw", 0x04), SS3("phsubw", 0x05),
        SS3("phsubd", 0x06), SS3("phsubsw", 0x07), SS3("psignb", 0x08),
        SS3("psignw", 0x09), SS3("psignd", 0x0a), SS3("pmulhrsw", 0x0b),
        SS3("pabsb", 0x1c), SS3("pabsw", 0x1d), SS3("pabsd", 0x1e),
#undef SS3
        RI("palignr", ISA_SSSE3, 0, 3, 0x0f, OM_MMXRM, OM_MMX),
        RI("palignr", ISA_SSSE3, 0x66, 3, 0x0f, OM_XMMRM, OM_XMM),

        /* SSE4.1 packed integer, blend, rounding, insert, and extract. */
        RR("ptest", ISA_SSE41, 0x66, 2, 0x17, OM_XMMRM, OM_XMM),
        RR("pblendvb", ISA_SSE41, 0x66, 2, 0x10, OM_XMMRM, OM_XMM),
        RR("blendvps", ISA_SSE41, 0x66, 2, 0x14, OM_XMMRM, OM_XMM),
        RR("blendvpd", ISA_SSE41, 0x66, 2, 0x15, OM_XMMRM, OM_XMM),
        RR("pmovsxbw", ISA_SSE41, 0x66, 2, 0x20, OM_XMMRM, OM_XMM),
        RR("pmovsxbd", ISA_SSE41, 0x66, 2, 0x21, OM_XMMRM, OM_XMM),
        RR("pmovsxbq", ISA_SSE41, 0x66, 2, 0x22, OM_XMMRM, OM_XMM),
        RR("pmovsxwd", ISA_SSE41, 0x66, 2, 0x23, OM_XMMRM, OM_XMM),
        RR("pmovsxwq", ISA_SSE41, 0x66, 2, 0x24, OM_XMMRM, OM_XMM),
        RR("pmovsxdq", ISA_SSE41, 0x66, 2, 0x25, OM_XMMRM, OM_XMM),
        RR("pmuldq", ISA_SSE41, 0x66, 2, 0x28, OM_XMMRM, OM_XMM),
        RR("pcmpeqq", ISA_SSE41, 0x66, 2, 0x29, OM_XMMRM, OM_XMM),
        RR("movntdqa", ISA_SSE41, 0x66, 2, 0x2a, OM_MEM, OM_XMM),
        RR("packusdw", ISA_SSE41, 0x66, 2, 0x2b, OM_XMMRM, OM_XMM),
        RR("pmovzxbw", ISA_SSE41, 0x66, 2, 0x30, OM_XMMRM, OM_XMM),
        RR("pmovzxbd", ISA_SSE41, 0x66, 2, 0x31, OM_XMMRM, OM_XMM),
        RR("pmovzxbq", ISA_SSE41, 0x66, 2, 0x32, OM_XMMRM, OM_XMM),
        RR("pmovzxwd", ISA_SSE41, 0x66, 2, 0x33, OM_XMMRM, OM_XMM),
        RR("pmovzxwq", ISA_SSE41, 0x66, 2, 0x34, OM_XMMRM, OM_XMM),
        RR("pmovzxdq", ISA_SSE41, 0x66, 2, 0x35, OM_XMMRM, OM_XMM),
        RR("pminsb", ISA_SSE41, 0x66, 2, 0x38, OM_XMMRM, OM_XMM),
        RR("pminsd", ISA_SSE41, 0x66, 2, 0x39, OM_XMMRM, OM_XMM),
        RR("pminuw", ISA_SSE41, 0x66, 2, 0x3a, OM_XMMRM, OM_XMM),
        RR("pminud", ISA_SSE41, 0x66, 2, 0x3b, OM_XMMRM, OM_XMM),
        RR("pmaxsb", ISA_SSE41, 0x66, 2, 0x3c, OM_XMMRM, OM_XMM),
        RR("pmaxsd", ISA_SSE41, 0x66, 2, 0x3d, OM_XMMRM, OM_XMM),
        RR("pmaxuw", ISA_SSE41, 0x66, 2, 0x3e, OM_XMMRM, OM_XMM),
        RR("pmaxud", ISA_SSE41, 0x66, 2, 0x3f, OM_XMMRM, OM_XMM),
        RR("pmulld", ISA_SSE41, 0x66, 2, 0x40, OM_XMMRM, OM_XMM),
        RR("phminposuw", ISA_SSE41, 0x66, 2, 0x41, OM_XMMRM, OM_XMM),
        RI("roundps", ISA_SSE41, 0x66, 3, 0x08, OM_XMMRM, OM_XMM),
        RI("roundpd", ISA_SSE41, 0x66, 3, 0x09, OM_XMMRM, OM_XMM),
        RI("roundss", ISA_SSE41, 0x66, 3, 0x0a, OM_XMMRM, OM_XMM),
        RI("roundsd", ISA_SSE41, 0x66, 3, 0x0b, OM_XMMRM, OM_XMM),
        RI("blendps", ISA_SSE41, 0x66, 3, 0x0c, OM_XMMRM, OM_XMM),
        RI("blendpd", ISA_SSE41, 0x66, 3, 0x0d, OM_XMMRM, OM_XMM),
        RI("pblendw", ISA_SSE41, 0x66, 3, 0x0e, OM_XMMRM, OM_XMM),
        RI("dpps", ISA_SSE41, 0x66, 3, 0x40, OM_XMMRM, OM_XMM),
        RI("dppd", ISA_SSE41, 0x66, 3, 0x41, OM_XMMRM, OM_XMM),
        RI("mpsadbw", ISA_SSE41, 0x66, 3, 0x42, OM_XMMRM, OM_XMM),
        T3I("pextrb", ISA_SSE41, OM_IMM, OM_XMM, OM_R32 | OM_MEM, 0x66, 3, 0x14, 1, 2, 0),
        T3I("pextrw", ISA_SSE41, OM_IMM, OM_XMM, OM_R32 | OM_MEM, 0x66, 3, 0x15, 1, 2, 0),
        T3I("pextrd", ISA_SSE41, OM_IMM, OM_XMM, OM_RM32, 0x66, 3, 0x16, 1, 2, 0),
        T3I("pinsrb", ISA_SSE41, OM_IMM, OM_R32 | OM_MEM, OM_XMM, 0x66, 3, 0x20, 2, 1, 0),
        T3I("insertps", ISA_SSE41, OM_IMM, OM_XMMRM, OM_XMM, 0x66, 3, 0x21, 2, 1, 0),
        T3I("pinsrd", ISA_SSE41, OM_IMM, OM_RM32, OM_XMM, 0x66, 3, 0x22, 2, 1, 0),
        T3I("extractps", ISA_SSE41, OM_IMM, OM_XMM, OM_R32 | OM_MEM, 0x66, 3, 0x17, 1, 2, 0),

        /* SSE4.2 packed compare strings, qword compare, popcount companion. */
        RR("pcmpgtq", ISA_SSE42, 0x66, 2, 0x37, OM_XMMRM, OM_XMM),
        RI("pcmpestrm", ISA_SSE42, 0x66, 3, 0x60, OM_XMMRM, OM_XMM),
        RI("pcmpestri", ISA_SSE42, 0x66, 3, 0x61, OM_XMMRM, OM_XMM),
        RI("pcmpistrm", ISA_SSE42, 0x66, 3, 0x62, OM_XMMRM, OM_XMM),
        RI("pcmpistri", ISA_SSE42, 0x66, 3, 0x63, OM_XMMRM, OM_XMM),
        ST("movntsd", ISA_SSE4A, 0xf2, 1, 0x2b, OM_XMM, OM_MEM),
        ST("movntss", ISA_SSE4A, 0xf3, 1, 0x2b, OM_XMM, OM_MEM),
    };
    return emit_table(tab, sizeof(tab) / sizeof(tab[0]), mn, op, n);
}

static int emit_3dnow(const char *mn, Operand *op, int n)
{
    static const struct {
        const char *name;
        IsaMask feature;
        unsigned char code;
    } tab[] = {
        { "pi2fw", ISA_3DNOWA, 0x0c },   { "pi2fd", ISA_3DNOW, 0x0d },
        { "pf2iw", ISA_3DNOWA, 0x1c },   { "pf2id", ISA_3DNOW, 0x1d },
        { "pfnacc", ISA_3DNOWA, 0x8a },  { "pfpnacc", ISA_3DNOWA, 0x8e },
        { "pfcmpge", ISA_3DNOW, 0x90 },  { "pfmin", ISA_3DNOW, 0x94 },
        { "pfrcp", ISA_3DNOW, 0x96 },    { "pfrsqrt", ISA_3DNOW, 0x97 },
        { "pfsub", ISA_3DNOW, 0x9a },    { "pfadd", ISA_3DNOW, 0x9e },
        { "pfcmpgt", ISA_3DNOW, 0xa0 },  { "pfmax", ISA_3DNOW, 0xa4 },
        { "pfrcpit1", ISA_3DNOW, 0xa6 }, { "pfrsqit1", ISA_3DNOW, 0xa7 },
        { "pfsubr", ISA_3DNOW, 0xaa },   { "pfacc", ISA_3DNOW, 0xae },
        { "pfcmpeq", ISA_3DNOW, 0xb0 },  { "pfmul", ISA_3DNOW, 0xb4 },
        { "pfrcpit2", ISA_3DNOW, 0xb6 }, { "pmulhrw", ISA_3DNOW, 0xb7 },
        { "pswapd", ISA_3DNOWA, 0xbb },  { "pavgusb", ISA_3DNOW, 0xbf },
    };
    unsigned i;
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if (strcmp(mn, tab[i].name))
            continue;
        if (!require_n(n, 2, mn) || !(operand_mask(&op[0]) & OM_MMXRM) ||
            !(operand_mask(&op[1]) & OM_MMX)) {
            fail("bad operands for %s", mn);
            return 1;
        }
        if (!need_feature(tab[i].feature, mn))
            return 1;
        if (op[0].seg_prefix)
            emit8(op[0].seg_prefix);
        emit8(0x0f);
        emit8(0x0f);
        emit_modrm(op[1].reg, &op[0]);
        emit8(tab[i].code);
        return 1;
    }
    return 0;
}

static int emit_sse4a(const char *mn, Operand *op, int n)
{
    int extr = !strcmp(mn, "extrq");
    int insert = !strcmp(mn, "insertq");
    if (!extr && !insert)
        return 0;
    if (!need_feature(ISA_SSE4A, mn))
        return 1;
    if (extr && n == 3 && op[0].kind == O_IMM && op[1].kind == O_IMM &&
        op[2].reg_class == RC_XMM) {
        emit8(0x66);
        emit8(0x0f);
        emit8(0x78);
        emit8(0xc0 | op[2].reg);
        emit_expr(&op[1].expr, 1, 0);
        emit_expr(&op[0].expr, 1, 0);
        return 1;
    }
    if (n == 2 && op[0].reg_class == RC_XMM && op[1].reg_class == RC_XMM) {
        emit8(extr ? 0x66 : 0xf2);
        emit8(0x0f);
        emit8(0x79);
        emit_modrm(op[1].reg, &op[0]);
        return 1;
    }
    if (insert && n == 4 && op[0].kind == O_IMM && op[1].kind == O_IMM &&
        op[2].reg_class == RC_XMM && op[3].reg_class == RC_XMM) {
        emit8(0xf2);
        emit8(0x0f);
        emit8(0x78);
        emit_modrm(op[3].reg, &op[2]);
        emit_expr(&op[1].expr, 1, 0);
        emit_expr(&op[0].expr, 1, 0);
        return 1;
    }
    fail("bad operands for %s", mn);
    return 1;
}

static int emit_crc32(const char *mn, Operand *op, int n)
{
    unsigned srcmask;
    int word, byte;
    if (strcmp(mn, "crc32b") && strcmp(mn, "crc32w") && strcmp(mn, "crc32l"))
        return 0;
    byte = mn[5] == 'b';
    word = mn[5] == 'w';
    srcmask = byte ? OM_RM8 : word ? OM_RM16 : OM_RM32;
    if (!require_n(n, 2, mn) || !(operand_mask(&op[0]) & srcmask) ||
        !(operand_mask(&op[1]) & OM_R32)) {
        fail("bad operands for %s", mn);
        return 1;
    }
    if (!need_feature(ISA_SSE42, mn))
        return 1;
    if (op[0].seg_prefix)
        emit8(op[0].seg_prefix);
    if (word)
        emit8(0x66);
    emit8(0xf2);
    emit8(0x0f);
    emit8(0x38);
    emit8(byte ? 0xf0 : 0xf1);
    emit_modrm(op[1].reg, &op[0]);
    return 1;
}

static int emit_sse_compare_alias(const char *mn, Operand *op, int n)
{
    static const char *rel[] = { "eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord" };
    const char *suffix;
    unsigned prefix = 0;
    IsaMask feature = ISA_SSE;
    char relation[12];
    unsigned i, len;
    if (strncmp(mn, "cmp", 3))
        return 0;
    len = strlen(mn);
    if (len < 6)
        return 0;
    suffix = mn + len - 2;
    if (!strcmp(suffix, "ps"))
        prefix = 0;
    else if (!strcmp(suffix, "ss"))
        prefix = 0xf3;
    else if (!strcmp(suffix, "pd"))
        prefix = 0x66, feature = ISA_SSE2;
    else if (!strcmp(suffix, "sd"))
        prefix = 0xf2, feature = ISA_SSE2;
    else
        return 0;
    if (len - 5 >= sizeof(relation))
        return 0;
    memcpy(relation, mn + 3, len - 5);
    relation[len - 5] = 0;
    for (i = 0; i < sizeof(rel) / sizeof(rel[0]); i++)
        if (!strcmp(relation, rel[i]))
            break;
    if (i == sizeof(rel) / sizeof(rel[0]))
        return 0;
    if (!require_n(n, 2, mn) || !(operand_mask(&op[0]) & OM_XMMRM) ||
        !(operand_mask(&op[1]) & OM_XMM)) {
        fail("bad operands for %s", mn);
        return 1;
    }
    if (!need_feature(feature, mn))
        return 1;
    if (op[0].seg_prefix)
        emit8(op[0].seg_prefix);
    emit_opcode_map(prefix, 1, 0xc2);
    emit_modrm(op[1].reg, &op[0]);
    emit8(i);
    return 1;
}

static int emit_far_control(const char *mn, Operand *op, int n)
{
    int jump;
    if (!strcmp(mn, "lret") || !strcmp(mn, "retf")) {
        if (n == 0)
            emit8(0xcb);
        else if (n == 1 && op[0].kind == O_IMM) {
            emit8(0xca);
            emit_expr(&op[0].expr, 2, 0);
        } else
            fail("bad operands for %s", mn);
        return 1;
    }
    if (!strcmp(mn, "callw") || !strcmp(mn, "jmpw")) {
        jump = mn[0] == 'j';
        if (!require_n(n, 1, mn) || (!op[0].indirect && op[0].kind != O_REG)) {
            fail("%s currently requires an indirect operand", mn);
            return 1;
        }
        emit8(0x66);
        if (op[0].seg_prefix)
            emit8(op[0].seg_prefix);
        emit8(0xff);
        emit_modrm(jump ? 4 : 2, &op[0]);
        return 1;
    }
    if (strcmp(mn, "lcall") && strcmp(mn, "ljmp"))
        return 0;
    jump = mn[1] == 'j';
    if (n == 2 && op[0].kind == O_IMM && op[1].kind == O_IMM) {
        emit8(jump ? 0xea : 0x9a);
        emit_expr(&op[1].expr, 4, 0);
        emit_expr(&op[0].expr, 2, 0);
    } else if (n == 1 && op[0].kind == O_MEM) {
        if (op[0].seg_prefix)
            emit8(op[0].seg_prefix);
        emit8(0xff);
        emit_modrm(jump ? 5 : 3, &op[0]);
    } else
        fail("bad operands for %s", mn);
    return 1;
}

static void assemble_instruction(char *line)
{
    char *mn, *rest, *args[4];
    Operand op[4];
    int n, i, sz, cc, address16 = force_addr16 != 0;
    unsigned dot = cursec->size;
    mn = trim(line);
    rest = mn;
    while (*rest && !isspace((unsigned char)*rest)) {
        *rest = (char)tolower((unsigned char)*rest);
        rest++;
    }
    if (*rest)
        *rest++ = 0;
    rest = trim(rest);
    if (emit_prefix_instruction(mn, rest))
        return;
    n = split_args(rest, args, 4);
    for (i = 0; i < n; i++)
        if (!parse_operand(args[i], &op[i], (int)(cursec - sections), dot))
            return;
    for (i = 0; i < n; i++)
        if (op[i].kind == O_MEM) {
            if (force_addr16) {
                if ((op[i].base >= 0 || op[i].index >= 0) && op[i].addr_size != 2) {
                    fail("addr16 requires 16-bit address registers");
                    return;
                }
                op[i].addr_size = 2;
            }
            if (op[i].addr_size == 2)
                address16 = 1;
        }
    if (address16)
        emit8(0x67);

    if (emit_implicit(mn, op, n) || emit_extend(mn, op, n) || emit_fixed(mn, n) ||
        emit_special_mov(mn, op, n) || emit_segment_stack(mn, op, n) ||
        emit_aad_aam(mn, op, n) || emit_io(mn, op, n) || emit_loop(mn, op, n) ||
        emit_legacy_table(mn, op, n) || emit_simd_table(mn, op, n) || emit_3dnow(mn, op, n) ||
        emit_sse4a(mn, op, n) || emit_crc32(mn, op, n) ||
        emit_sse_compare_alias(mn, op, n) || emit_far_control(mn, op, n))
        return;

    if (mnemonic(mn, "mov", &sz)) {
        if (!require_n(n, 2, mn))
            return;
        if ((op[0].kind == O_REG && !is_gpr(&op[0])) ||
            (op[1].kind == O_REG && !is_gpr(&op[1])) || !is_gpr_or_mem(&op[1])) {
            fail("bad general-register operands for %s", mn);
            return;
        }
        sz = operand_size(&op[0], &op[1], sz);
        if (op[0].seg_prefix)
            emit8(op[0].seg_prefix);
        if (op[1].seg_prefix)
            emit8(op[1].seg_prefix);
        size_prefix(sz);
        if (op[0].kind == O_IMM && is_gpr(&op[1])) {
            emit8((sz == 1 ? 0xb0 : 0xb8) + op[1].reg);
            emit_expr(&op[0].expr, sz, op[0].expr.reloc);
        } else if (op[0].kind == O_IMM) {
            emit8(sz == 1 ? 0xc6 : 0xc7);
            emit_modrm(0, &op[1]);
            emit_expr(&op[0].expr, sz, op[0].expr.reloc);
        } else if (op[0].kind == O_MEM && op[0].base < 0 && op[0].index < 0 &&
                   is_gpr(&op[1]) && op[1].reg == 0) {
            emit8(sz == 1 ? 0xa0 : 0xa1);
            emit_expr(&op[0].expr, op[0].addr_size == 2 ? 2 : 4,
                      op[0].addr_size == 2 ? R_386_16 : op[0].expr.reloc);
        } else if (is_gpr(&op[0]) && op[0].reg == 0 && op[1].kind == O_MEM &&
                   op[1].base < 0 && op[1].index < 0) {
            emit8(sz == 1 ? 0xa2 : 0xa3);
            emit_expr(&op[1].expr, op[1].addr_size == 2 ? 2 : 4,
                      op[1].addr_size == 2 ? R_386_16 : op[1].expr.reloc);
        } else if (is_gpr(&op[0])) {
            emit8(sz == 1 ? 0x88 : 0x89);
            emit_modrm(op[0].reg, &op[1]);
        } else if (is_gpr(&op[1])) {
            emit8(sz == 1 ? 0x8a : 0x8b);
            emit_modrm(op[1].reg, &op[0]);
        } else
            fail("bad operands for %s", mn);
        return;
    }
    if (mnemonic(mn, "lea", &sz)) {
        if (!require_n(n, 2, mn) || op[0].kind != O_MEM || !is_gpr(&op[1])) {
            fail("bad lea operands");
            return;
        }
        if (op[0].seg_prefix)
            emit8(op[0].seg_prefix);
        size_prefix(operand_size(0, &op[1], sz));
        emit8(0x8d);
        emit_modrm(op[1].reg, &op[0]);
        return;
    }
    if (emit_binary_family(mn, op, n))
        return;
    if (mnemonic(mn, "test", &sz)) {
        if (!require_n(n, 2, mn))
            return;
        sz = operand_size(&op[0], &op[1], sz);
        if (op[1].seg_prefix)
            emit8(op[1].seg_prefix);
        size_prefix(sz);
        if (op[0].kind == O_IMM) {
            emit8(sz == 1 ? 0xf6 : 0xf7);
            emit_modrm(0, &op[1]);
            emit_expr(&op[0].expr, sz, op[0].expr.reloc);
        } else if (is_gpr(&op[0])) {
            emit8(sz == 1 ? 0x84 : 0x85);
            emit_modrm(op[0].reg, &op[1]);
        } else
            fail("bad test operands");
        return;
    }
    if (emit_unary_family(mn, op, n))
        return;
    if (mnemonic(mn, "imul", &sz)) {
        if (n == 2 && op[0].kind == O_IMM && is_gpr(&op[1])) {
            long long small;
            int short_imm = signed_byte_expr(&op[0].expr, &small);
            sz = operand_size(0, &op[1], sz);
            size_prefix(sz);
            emit8(short_imm ? 0x6b : 0x69);
            emit_modrm(op[1].reg, &op[1]);
            emit_expr(&op[0].expr, short_imm ? 1 : sz, op[0].expr.reloc);
            return;
        }
        if (n == 2 && is_gpr(&op[1]) && is_gpr_or_mem(&op[0])) {
            sz = operand_size(0, &op[1], sz);
            size_prefix(sz);
            emit8(0x0f);
            emit8(0xaf);
            emit_modrm(op[1].reg, &op[0]);
            return;
        }
        if (n == 3 && op[0].kind == O_IMM && is_gpr_or_mem(&op[1]) && is_gpr(&op[2])) {
            long long small;
            int short_imm = signed_byte_expr(&op[0].expr, &small);
            sz = operand_size(0, &op[2], sz);
            size_prefix(sz);
            emit8(short_imm ? 0x6b : 0x69);
            emit_modrm(op[2].reg, &op[1]);
            emit_expr(&op[0].expr, short_imm ? 1 : sz, op[0].expr.reloc);
            return;
        }
        fail("bad imul operands");
        return;
    }
    if (emit_shift_family(mn, op, n))
        return;
    if (mnemonic(mn, "shld", &sz) || mnemonic(mn, "shrd", &sz)) {
        int right = !strncmp(mn, "shrd", 4);
        Operand *count = n == 3 ? &op[0] : 0;
        Operand *src = n == 3 ? &op[1] : &op[0];
        Operand *dst = n == 3 ? &op[2] : &op[1];
        if ((n != 2 && n != 3) || !is_gpr(src) || !is_gpr_or_mem(dst)) {
            fail("bad double shift operands");
            return;
        }
        size_prefix(operand_size(0, dst, sz));
        emit8(0x0f);
        if (count && count->kind == O_IMM) {
            emit8(right ? 0xac : 0xa4);
            emit_modrm(src->reg, dst);
            emit_expr(&count->expr, 1, 0);
        } else if (!count || (count->kind == O_REG && count->reg == 1 &&
                              count->reg_size == 1)) {
            emit8(right ? 0xad : 0xa5);
            emit_modrm(src->reg, dst);
        } else
            fail("bad double shift count");
        return;
    }
    if (mnemonic(mn, "push", &sz)) {
        if (!require_n(n, 1, mn))
            return;
        sz = operand_size(&op[0], 0, sz);
        size_prefix(sz);
        if (is_gpr(&op[0]))
            emit8(0x50 + op[0].reg);
        else if (op[0].kind == O_IMM) {
            long long small;
            int short_imm = signed_byte_expr(&op[0].expr, &small);
            emit8(short_imm ? 0x6a : 0x68);
            emit_expr(&op[0].expr, short_imm ? 1 : sz == 2 ? 2 : 4, op[0].expr.reloc);
        } else if (op[0].kind == O_MEM) {
            if (op[0].seg_prefix)
                emit8(op[0].seg_prefix);
            emit8(0xff);
            emit_modrm(6, &op[0]);
        } else
            fail("bad push operand");
        return;
    }
    if (mnemonic(mn, "pop", &sz)) {
        if (!require_n(n, 1, mn))
            return;
        sz = operand_size(&op[0], 0, sz);
        size_prefix(sz);
        if (is_gpr(&op[0]))
            emit8(0x58 + op[0].reg);
        else if (op[0].kind == O_MEM) {
            if (op[0].seg_prefix)
                emit8(op[0].seg_prefix);
            emit8(0x8f);
            emit_modrm(0, &op[0]);
        } else
            fail("bad pop operand");
        return;
    }
    if (!strcmp(mn, "call") || !strcmp(mn, "calll")) {
        if (!require_n(n, 1, mn))
            return;
        if (op[0].indirect || is_gpr(&op[0])) {
            if (op[0].seg_prefix)
                emit8(op[0].seg_prefix);
            emit8(0xff);
            emit_modrm(2, &op[0]);
        } else if (op[0].kind == O_MEM && !op[0].indirect) {
            emit_relative(&op[0].expr, 0xe8, -1,
                          op[0].expr.reloc == R_386_PLT32 ? R_386_PLT32 : R_386_PC32);
        } else
            fail("bad call operand");
        return;
    }
    if (!strcmp(mn, "jmp") || !strcmp(mn, "jmpl")) {
        if (!require_n(n, 1, mn))
            return;
        if (op[0].indirect || is_gpr(&op[0])) {
            if (op[0].seg_prefix)
                emit8(op[0].seg_prefix);
            emit8(0xff);
            emit_modrm(4, &op[0]);
        } else if (op[0].kind == O_MEM && !op[0].indirect)
            if (local_branch_target(&op[0].expr))
                emit_short_relative(&op[0].expr, 0xeb, FIX_RELAX_JMP);
            else
                emit_relative(&op[0].expr, 0xe9, -1, R_386_PC32);
        else
            fail("bad jmp operand");
        return;
    }
    if (mn[0] == 'j' && (cc = condition_code(mn + 1)) >= 0) {
        if (!require_n(n, 1, mn) || op[0].kind != O_MEM) {
            fail("bad conditional branch");
            return;
        }
        if (local_branch_target(&op[0].expr))
            emit_short_relative(&op[0].expr, 0x70 + cc, FIX_RELAX_JCC);
        else
            emit_relative(&op[0].expr, 0x0f, 0x80 + cc, R_386_PC32);
        return;
    }
    if (!strncmp(mn, "set", 3) && (cc = condition_code(mn + 3)) >= 0) {
        if (!require_n(n, 1, mn))
            return;
        if ((op[0].kind == O_REG && (!is_gpr(&op[0]) || op[0].reg_size != 1)) ||
            (op[0].kind != O_REG && op[0].kind != O_MEM)) {
            fail("bad setcc destination");
            return;
        }
        if (op[0].seg_prefix)
            emit8(op[0].seg_prefix);
        emit8(0x0f);
        emit8(0x90 + cc);
        emit_modrm(0, &op[0]);
        return;
    }
    if (!strncmp(mn, "cmov", 4)) {
        char cname[16];
        unsigned clen = strlen(mn + 4);
        if (clen >= sizeof(cname)) {
            fail("bad cmov mnemonic %s", mn);
            return;
        }
        strcpy(cname, mn + 4);
        if (clen > 1 && (cname[clen - 1] == 'w' || cname[clen - 1] == 'l'))
            cname[--clen] = 0;
        cc = condition_code(cname);
    } else
        cc = -1;
    if (!strncmp(mn, "cmov", 4) && cc >= 0) {
        if (!need_feature(ISA_I686, mn))
            return;
        if (!require_n(n, 2, mn) || !is_gpr(&op[1]) || !is_gpr_or_mem(&op[0])) {
            fail("bad cmov operands");
            return;
        }
        size_prefix(op[1].reg_size);
        emit8(0x0f);
        emit8(0x40 + cc);
        emit_modrm(op[1].reg, &op[0]);
        return;
    }
    if (!strcmp(mn, "ret") || !strcmp(mn, "retl") || !strcmp(mn, "retw")) {
        if (!strcmp(mn, "retw"))
            emit8(0x66);
        if (n == 0)
            emit8(0xc3);
        else if (n == 1 && op[0].kind == O_IMM) {
            emit8(0xc2);
            emit_expr(&op[0].expr, 2, 0);
        } else
            fail("bad ret operands");
        return;
    }
    if (!strcmp(mn, "enter")) {
        if (!require_n(n, 2, mn) || op[0].kind != O_IMM || op[1].kind != O_IMM)
            return;
        emit8(0xc8);
        emit_expr(&op[0].expr, 2, 0);
        emit_expr(&op[1].expr, 1, 0);
        return;
    }
    if (!strcmp(mn, "int")) {
        if (!require_n(n, 1, mn) || op[0].kind != O_IMM)
            return;
        emit8(0xcd);
        emit_expr(&op[0].expr, 1, 0);
        return;
    }
    if (!strcmp(mn, "xchg") || !strcmp(mn, "xchgl") || !strcmp(mn, "xchgw") ||
        !strcmp(mn, "xchgb")) {
        if (!require_n(n, 2, mn) || !is_gpr(&op[0]) || !is_gpr_or_mem(&op[1]))
            return;
        sz = op[0].reg_size;
        if (sz == 2)
            emit8(0x66);
        emit8(sz == 1 ? 0x86 : 0x87);
        emit_modrm(op[0].reg, &op[1]);
        return;
    }
    if (!strcmp(mn, "bswap") || !strcmp(mn, "bswapl")) {
        if (!need_feature(ISA_I486, mn))
            return;
        if (!require_n(n, 1, mn) || !is_gpr(&op[0]) || op[0].reg_size != 4)
            return;
        emit8(0x0f);
        emit8(0xc8 + op[0].reg);
        return;
    }
    if (is_x87_name(mn)) {
        if (need_feature(ISA_X87, mn))
            emit_x87(mn, op, n);
        return;
    }
    fail("unknown instruction %s", mn);
}

static int string_bytes(const char *text, int nul)
{
    const char *p = trim((char *)text);
    if (*p++ != '"') {
        fail("string literal expected");
        return 0;
    }
    while (*p && *p != '"') {
        unsigned c = (unsigned char)*p++;
        if (c == '\\') {
            c = (unsigned char)*p++;
            if (c == 'n')
                c = '\n';
            else if (c == 'r')
                c = '\r';
            else if (c == 't')
                c = '\t';
            else if (c == 'b')
                c = '\b';
            else if (c == 'f')
                c = '\f';
            else if (c == 'v')
                c = '\v';
            else if (c >= '0' && c <= '7') {
                int k = 1;
                unsigned v = c - '0';
                while (k < 3 && *p >= '0' && *p <= '7') {
                    v = (v << 3) + (*p++ - '0');
                    k++;
                }
                c = v;
            } else if (c == 'x') {
                unsigned v = 0;
                int k = 0;
                while (isxdigit((unsigned char)*p)) {
                    v = v * 16 + (isdigit((unsigned char)*p)
                                      ? *p - '0'
                                      : tolower((unsigned char)*p) - 'a' + 10);
                    p++;
                    k++;
                }
                if (!k) {
                    fail("bad hex escape");
                    return 0;
                }
                c = v;
            }
        }
        emit8(c);
    }
    if (*p != '"') {
        fail("unterminated string");
        return 0;
    }
    if (nul)
        emit8(0);
    return 1;
}

static void do_align(unsigned align, int fill, int explicit_fill)
{
    Alignment *a;
    unsigned start, target;
    if (!power_of_two(align)) {
        fail("alignment must be a power of two");
        return;
    }
    if (align > cursec->align)
        cursec->align = align;
    if (nalignment >= MAX_ALIGNS)
        fatal("too many alignment directives");
    start = cursec->size;
    target = align_up(start, align);
    if (!explicit_fill)
        fill = (cursec->flags & SHF_EXECINSTR) ? 0x90 : 0;
    while (cursec->size < target)
        emit8(fill);
    a = &alignments[nalignment++];
    a->sec = cursec;
    a->offset = start;
    a->size = target - start;
    a->align = align;
    a->fill = (unsigned char)fill;
}

static int constant_arg(const char *s, long long *v)
{
    Expr e;
    if (!parse_expr(s, &e, (int)(cursec - sections), cursec->size))
        return 0;
    if (!expr_absolute(&e, v)) {
        fail("constant expression required");
        return 0;
    }
    return 1;
}

static void select_section(Section *s)
{
    if (cursec != s) {
        prevsec = cursec;
        cursec = s;
    }
}

static void directive(char *line)
{
    char *name = line, *rest, *arg[16];
    int n, i;
    long long v, fill = 0;
    rest = name;
    while (*rest && !isspace((unsigned char)*rest))
        *rest = (char)tolower((unsigned char)*rest), rest++;
    if (*rest)
        *rest++ = 0;
    rest = trim(rest);
    if (!strcmp(name, ".text")) {
        select_section(get_section(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 16));
        return;
    }
    if (!strcmp(name, ".data")) {
        select_section(get_section(".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 4));
        return;
    }
    if (!strcmp(name, ".bss")) {
        select_section(get_section(".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 4));
        return;
    }
    if (!strcmp(name, ".previous")) {
        Section *s = cursec;
        cursec = prevsec;
        prevsec = s;
        return;
    }
    if (!strcmp(name, ".pushsection")) {
        if (nsecstack >= 16) {
            fail("section stack overflow");
            return;
        }
        secstack[nsecstack++] = cursec;
        name = ".section";
    }
    if (!strcmp(name, ".popsection")) {
        if (!nsecstack) {
            fail("section stack underflow");
            return;
        }
        select_section(secstack[--nsecstack]);
        return;
    }
    if (!strcmp(name, ".section")) {
        char *a[4], *secname, *flags = 0, *type = 0;
        unsigned sf = 0, st = SHT_PROGBITS, al = 1;
        n = split_args(rest, a, 4);
        if (n < 1) {
            fail("section name required");
            return;
        }
        secname = trim(a[0]);
        if (n > 1)
            flags = trim(a[1]);
        if (flags && *flags == '"') {
            flags++;
            if (strchr(flags, 'a'))
                sf |= SHF_ALLOC;
            if (strchr(flags, 'w'))
                sf |= SHF_WRITE;
            if (strchr(flags, 'x'))
                sf |= SHF_EXECINSTR;
        }
        if (n > 2)
            type = trim(a[2]);
        if (type && (!strcmp(type, "@nobits") || !strcmp(type, "%nobits")))
            st = SHT_NOBITS;
        if (sf & SHF_EXECINSTR)
            al = 16;
        else if (sf & SHF_ALLOC)
            al = 4;
        select_section(get_section(secname, st, sf, al));
        return;
    }
    if (!strcmp(name, ".arch")) {
        if (!*rest) {
            fail(".arch requires a CPU or extension");
            return;
        }
        if (*rest == '.')
            set_extension(rest + 1);
        else
            set_march(rest);
        return;
    }
    n = split_args(rest, arg, 16);
    if (!strcmp(name, ".globl") || !strcmp(name, ".global")) {
        for (i = 0; i < n; i++) {
            Symbol *s = get_symbol(arg[i]);
            if (s->bind != STB_WEAK)
                s->bind = STB_GLOBAL;
            s->binding_set = 1;
        }
        return;
    }
    if (!strcmp(name, ".local")) {
        for (i = 0; i < n; i++) {
            get_symbol(arg[i])->bind = STB_LOCAL;
            get_symbol(arg[i])->binding_set = 1;
        }
        return;
    }
    if (!strcmp(name, ".weak")) {
        for (i = 0; i < n; i++) {
            get_symbol(arg[i])->bind = STB_WEAK;
            get_symbol(arg[i])->binding_set = 1;
        }
        return;
    }
    if (!strcmp(name, ".hidden")) {
        for (i = 0; i < n; i++)
            get_symbol(arg[i])->other = STV_HIDDEN;
        return;
    }
    if (!strcmp(name, ".type")) {
        Symbol *s;
        if (n != 2) {
            fail(".type expects two operands");
            return;
        }
        s = get_symbol(arg[0]);
        if (strstr(arg[1], "function"))
            s->type = STT_FUNC;
        else if (strstr(arg[1], "object"))
            s->type = STT_OBJECT;
        else if (strstr(arg[1], "notype"))
            s->type = STT_NOTYPE;
        else
            fail("unsupported symbol type %s", arg[1]);
        return;
    }
    if (!strcmp(name, ".size")) {
        Expr e;
        if (n != 2) {
            fail(".size expects two operands");
            return;
        }
        if (parse_expr(arg[1], &e, (int)(cursec - sections), cursec->size) && expr_absolute(&e, &v))
            get_symbol(arg[0])->size = (unsigned)v;
        else
            fail(".size expression is not resolvable");
        return;
    }
    if (!strcmp(name, ".set") || !strcmp(name, ".equ")) {
        Expr e;
        Symbol *s;
        if (n != 2) {
            fail("%s expects two operands", name);
            return;
        }
        if (!parse_expr(arg[1], &e, (int)(cursec - sections), cursec->size) ||
            !expr_absolute(&e, &v)) {
            fail("absolute expression required");
            return;
        }
        s = get_symbol(arg[0]);
        s->defined = 1;
        s->sec = SEC_ABS;
        s->value = (unsigned)v;
        return;
    }
    if (!strcmp(name, ".comm")) {
        Symbol *s;
        if (n < 2 || !constant_arg(arg[1], &v))
            return;
        s = get_symbol(arg[0]);
        s->defined = 1;
        s->common = 1;
        s->sec = -1;
        if (!s->binding_set)
            s->bind = STB_GLOBAL;
        s->value = (unsigned)v;
        s->size = (unsigned)v;
        s->align = 4;
        if (n > 2 && constant_arg(arg[2], &fill))
            s->align = (unsigned)fill;
        if (!power_of_two(s->align))
            fail("common alignment is not a power of two");
        return;
    }
    if (!strcmp(name, ".lcomm")) {
        Section *save = cursec;
        Symbol *s;
        if (n < 2 || !constant_arg(arg[1], &v))
            return;
        select_section(get_section(".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 4));
        fill = n > 2 && constant_arg(arg[2], &fill) ? fill : 4;
        do_align((unsigned)fill, 0, 1);
        define_symbol(arg[0], cursec, cursec->size);
        s = get_symbol(arg[0]);
        s->bind = STB_LOCAL;
        s->size = (unsigned)v;
        while (v-- > 0)
            emit8(0);
        cursec = save;
        return;
    }
    if (!strcmp(name, ".byte") || !strcmp(name, ".word") || !strcmp(name, ".short") ||
        !strcmp(name, ".long") || !strcmp(name, ".int") || !strcmp(name, ".quad")) {
        unsigned z = !strcmp(name, ".byte")                                ? 1
                     : (!strcmp(name, ".word") || !strcmp(name, ".short")) ? 2
                     : (!strcmp(name, ".quad"))                            ? 8
                                                                           : 4;
        for (i = 0; i < n; i++) {
            Expr e;
            if (parse_expr(arg[i], &e, (int)(cursec - sections), cursec->size))
                emit_expr(&e, z, e.reloc);
        }
        return;
    }
    if (!strcmp(name, ".float") || !strcmp(name, ".single")) {
        for (i = 0; i < n; i++) {
            char *p = arg[i];
            float f;
            unsigned bits;
            if (*p == '0' && p[1] == 'f')
                p += 2;
            f = (float)strtod(p, 0);
            memcpy(&bits, &f, 4);
            emit32(bits);
        }
        return;
    }
    if (!strcmp(name, ".double")) {
        for (i = 0; i < n; i++) {
            char *p = arg[i];
            double d;
            unsigned long long bits;
            if (*p == '0' && p[1] == 'd')
                p += 2;
            d = strtod(p, 0);
            memcpy(&bits, &d, 8);
            emit64(bits);
        }
        return;
    }
    if (!strcmp(name, ".ascii") || !strcmp(name, ".asciz") || !strcmp(name, ".string")) {
        for (i = 0; i < n; i++)
            string_bytes(arg[i], strcmp(name, ".ascii") != 0);
        return;
    }
    if (!strcmp(name, ".space") || !strcmp(name, ".skip") || !strcmp(name, ".zero")) {
        if (n < 1 || !constant_arg(arg[0], &v))
            return;
        if (n > 1 && !constant_arg(arg[1], &fill))
            return;
        while (v-- > 0)
            emit8((unsigned)fill);
        return;
    }
    if (!strcmp(name, ".align") || !strcmp(name, ".balign")) {
        if (n < 1 || !constant_arg(arg[0], &v))
            return;
        if (n > 1 && !constant_arg(arg[1], &fill))
            return;
        do_align((unsigned)v, (int)fill, n > 1);
        return;
    }
    if (!strcmp(name, ".p2align")) {
        if (n < 1 || !constant_arg(arg[0], &v) || v < 0 || v > 30) {
            fail("bad power-of-two alignment");
            return;
        }
        if (n > 1 && !constant_arg(arg[1], &fill))
            return;
        do_align(1u << (unsigned)v, (int)fill, n > 1);
        return;
    }
    if (!strcmp(name, ".ident")) {
        Section *save = cursec;
        select_section(get_section(".comment", SHT_PROGBITS, 0, 1));
        string_bytes(rest, 1);
        cursec = save;
        return;
    }
    if (!strcmp(name, ".file") || !strcmp(name, ".loc") || !strcmp(name, ".code32") ||
        !strcmp(name, ".subsection") || !strcmp(name, ".gnu_attribute"))
        return;
    if (!strcmp(name, ".att_syntax"))
        return;
    if (!strcmp(name, ".intel_syntax")) {
        fail("Intel syntax is not supported; use AT&T syntax");
        return;
    }
    if (!strcmp(name, ".weakref")) {
        if (n != 2) {
            fail(".weakref expects two operands");
            return;
        }
        get_symbol(arg[0])->bind = STB_WEAK;
        get_symbol(arg[0])->binding_set = 1;
        get_symbol(arg[1])->referenced = 1;
        return;
    }
    fail("unknown directive %s", name);
}

static void remove_comment(char *line)
{
    int quote = 0;
    char *p;
    for (p = line; *p; p++) {
        if (*p == '"' && (p == line || p[-1] != '\\'))
            quote = !quote;
        else if (*p == '#' && !quote) {
            *p = 0;
            break;
        }
    }
}

static void assemble_statement(char *line)
{
    char *p, *colon, *start;
    unsigned digit;
    p = trim(line);
    if (!*p)
        return;
    for (;;) {
        int quote = 0, paren = 0;
        colon = 0;
        for (start = p; *start; start++) {
            if (*start == '"' && (start == p || start[-1] != '\\'))
                quote = !quote;
            if (!quote) {
                if (*start == '(')
                    paren++;
                else if (*start == ')')
                    paren--;
                else if (*start == ':' && paren == 0) {
                    colon = start;
                    break;
                } else if (isspace((unsigned char)*start) && paren == 0)
                    break;
            }
        }
        if (!colon)
            break;
        *colon = 0;
        start = trim(p);
        if (start[0] >= '0' && start[0] <= '9' && !start[1]) {
            digit = start[0] - '0';
            local_count[digit]++;
            {
                char buf[32];
                numeric_name(buf, digit, local_count[digit]);
                define_symbol(buf, cursec, cursec->size);
            }
        } else
            define_symbol(start, cursec, cursec->size);
        p = trim(colon + 1);
        if (!*p)
            return;
    }
    if (*p == '.')
        directive(p);
    else
        assemble_instruction(p);
}

static void assemble_line(char *line)
{
    char *p, *start;
    int quote = 0;

    remove_comment(line);
    start = line;
    for (p = line; *p; p++) {
        if (*p == '"' && (p == line || p[-1] != '\\'))
            quote = !quote;
        else if (*p == ';' && !quote) {
            *p = 0;
            assemble_statement(start);
            start = p + 1;
        }
    }
    assemble_statement(start);
}

static int assemble_file(FILE *fp, const char *name)
{
    char line[MAX_LINE];
    const char *oldname = source_name;
    unsigned oldline = source_line;
    source_name = name;
    source_line = 0;
    while (fgets(line, sizeof(line), fp)) {
        source_line++;
        if (!strchr(line, '\n') && !feof(fp)) {
            fail("input line is too long");
            while (fgets(line, sizeof(line), fp) && !strchr(line, '\n'))
                ;
            continue;
        }
        assemble_line(line);
    }
    if (ferror(fp))
        fail("read error");
    source_name = oldname;
    source_line = oldline;
    return errors == 0;
}

static int keep_symbol(Symbol *s)
{
    if (s->referenced || s->bind != STB_LOCAL)
        return 1;
    if (xflag)
        return 0;
    if (Xflag && (s->name[0] == 'L' || s->name[0] == '.'))
        return 0;
    return 1;
}

static Symbol *fixup_symbol(Expr *e)
{
    reduce_expr(e);
    if (e->nterm != 1 || e->term[0].sign != 1 || !e->term[0].sym) {
        fail("relocation expression cannot be represented in ELF32");
        return 0;
    }
    e->term[0].sym->referenced = 1;
    return e->term[0].sym;
}

static int local_pc32_value(const Fixup *f, const Expr *e, long long *value)
{
    const Symbol *s;

    if (f->reloc != R_386_PC32 || e->nterm != 1 || e->term[0].sign != 1 ||
        !e->term[0].sym)
        return 0;
    s = e->term[0].sym;
    if (!s->defined || s->common || s->bind != STB_LOCAL ||
        s->sec != (int)(f->sec - sections))
        return 0;
    *value = e->addend + s->value - f->offset;
    return 1;
}

static void put_ehdr(Buffer *b, unsigned shoff, unsigned shnum, unsigned shstr)
{
    int i;
    buf8(b, 0x7f);
    buf8(b, 'E');
    buf8(b, 'L');
    buf8(b, 'F');
    buf8(b, ELFCLASS32);
    buf8(b, ELFDATA2LSB);
    buf8(b, EV_CURRENT);
    for (i = 7; i < 16; i++)
        buf8(b, 0);
    buf16(b, ET_REL);
    buf16(b, EM_386);
    buf32(b, EV_CURRENT);
    buf32(b, 0);
    buf32(b, 0);
    buf32(b, shoff);
    buf32(b, 0);
    buf16(b, 52);
    buf16(b, 0);
    buf16(b, 0);
    buf16(b, 40);
    buf16(b, shnum);
    buf16(b, shstr);
}

static void put_shdr(Buffer *b, unsigned name, unsigned type, unsigned flags, unsigned off,
                     unsigned size, unsigned link, unsigned info, unsigned align, unsigned entsize)
{
    buf32(b, name);
    buf32(b, type);
    buf32(b, flags);
    buf32(b, 0);
    buf32(b, off);
    buf32(b, size);
    buf32(b, link);
    buf32(b, info);
    buf32(b, align);
    buf32(b, entsize);
}

static void put_sym(Buffer *b, unsigned name, unsigned value, unsigned size, unsigned info,
                    unsigned other, unsigned shndx)
{
    buf32(b, name);
    buf32(b, value);
    buf32(b, size);
    buf8(b, info);
    buf8(b, other);
    buf16(b, shndx);
}

static void shift_section_tail(Section *sec, unsigned threshold, int delta,
                               unsigned span_start, Alignment *skip)
{
    int secno = (int)(sec - sections);
    int i, j;

    for (i = 0; i < nsymbol; i++) {
        Symbol *s = &symbols[i];
        unsigned long long end;
        if (!s->defined || s->common || s->sec != secno)
            continue;
        end = (unsigned long long)s->value + s->size;
        if (s->size && s->value < span_start && end >= threshold)
            s->size = (unsigned)((long long)s->size + delta);
        if (s->value >= threshold)
            s->value = (unsigned)((long long)s->value + delta);
    }
    for (i = 0; i < nfixup; i++) {
        Fixup *f = &fixups[i];
        if (f->sec == sec && f->offset >= threshold)
            f->offset = (unsigned)((long long)f->offset + delta);
        if (f->expr.parse_sec == secno && f->expr.parse_dot >= threshold)
            f->expr.parse_dot =
                (unsigned)((long long)f->expr.parse_dot + delta);
        for (j = 0; j < f->expr.nterm; j++)
            if (!f->expr.term[j].sym && f->expr.term[j].sec == secno &&
                f->expr.term[j].value >= threshold)
                f->expr.term[j].value =
                    (unsigned)((long long)f->expr.term[j].value + delta);
    }
    for (i = 0; i < nalignment; i++)
        if (&alignments[i] != skip && alignments[i].sec == sec &&
            alignments[i].offset >= threshold)
            alignments[i].offset =
                (unsigned)((long long)alignments[i].offset + delta);
}

static void insert_section_bytes(Section *sec, unsigned offset, unsigned count)
{
    if (offset > sec->size)
        fatal("internal branch relaxation offset");
    sec_need(sec, count);
    if (sec->type != SHT_NOBITS) {
        memmove(sec->data + offset + count, sec->data + offset,
                sec->size - offset);
        memset(sec->data + offset, 0, count);
    }
    sec->size += count;
    shift_section_tail(sec, offset, (int)count, offset, 0);
}

static void resize_alignment(Alignment *a, unsigned size)
{
    Section *sec = a->sec;
    unsigned oldend = a->offset + a->size;
    int delta = (int)size - (int)a->size;

    if (!delta)
        return;
    if (delta > 0)
        sec_need(sec, (unsigned)delta);
    if (sec->type != SHT_NOBITS) {
        memmove(sec->data + a->offset + size, sec->data + oldend,
                sec->size - oldend);
        memset(sec->data + a->offset, a->fill, size);
    }
    sec->size = (unsigned)((long long)sec->size + delta);
    shift_section_tail(sec, oldend, delta, a->offset, a);
    a->size = size;
}

static void relax_alignments(void)
{
    int i;
    for (i = 0; i < nalignment; i++) {
        Alignment *a = &alignments[i];
        unsigned target = align_up(a->offset, a->align);
        resize_alignment(a, target - a->offset);
    }
}

static int short_branch_value(Fixup *f, long long *value)
{
    Expr e = f->expr;
    reduce_expr(&e);
    if (expr_absolute(&e, value)) {
        *value -= f->offset + 1;
        return 1;
    }
    if (e.nterm == 1 && e.term[0].sign == 1 && e.term[0].sym &&
        e.term[0].sym->defined && !e.term[0].sym->common &&
        e.term[0].sym->sec == (int)(f->sec - sections)) {
        *value = e.addend + e.term[0].sym->value - (f->offset + 1);
        return 1;
    }
    return 0;
}

static void relax_branches(void)
{
    int changed, i;
    do {
        changed = 0;
        for (i = 0; i < nfixup; i++) {
            Fixup *f = &fixups[i];
            Section *sec;
            unsigned start, opcode, cc;
            long long value;
            if (f->reloc != FIX_RELAX_JMP && f->reloc != FIX_RELAX_JCC)
                continue;
            if (!short_branch_value(f, &value) ||
                (value >= -128 && value <= 127))
                continue;
            sec = f->sec;
            start = f->offset - 1;
            opcode = sec->data[start];
            if (f->reloc == FIX_RELAX_JMP) {
                if (opcode != 0xeb)
                    fatal("internal short jump opcode");
                insert_section_bytes(sec, f->offset + 1, 3);
                sec->data[start] = 0xe9;
            } else {
                if ((opcode & 0xf0) != 0x70)
                    fatal("internal short conditional jump opcode");
                cc = opcode & 15;
                insert_section_bytes(sec, f->offset + 1, 4);
                sec->data[start] = 0x0f;
                sec->data[start + 1] = 0x80 + cc;
                f->offset++;
            }
            f->size = 4;
            f->expr.addend -= 4;
            f->reloc = R_386_PC32;
            changed = 1;
        }
        if (changed)
            relax_alignments();
    } while (changed);
}

static void write_object(void)
{
    Buffer out = { 0 }, str = { 0 }, shstr = { 0 }, symtab = { 0 };
    Buffer rel[MAX_SECTIONS];
    unsigned secname[MAX_SECTIONS], relname[MAX_SECTIONS], secoff[MAX_SECTIONS];
    unsigned reloff[MAX_SECTIONS], *symname;
    unsigned symtab_index, strtab_index, shstr_index, shnum, shoff, first_global;
    unsigned i, j, nrelsec = 0, off;
    FILE *fp;
    relax_branches();
    if (errors)
        return;
    memset(rel, 0, sizeof(rel));
    buf8(&str, 0);
    buf8(&shstr, 0);
    /* Resolve relocations first, because this marks referenced local symbols. */
    for (i = 0; i < (unsigned)nfixup; i++) {
        Fixup *f = &fixups[i];
        Expr e = f->expr;
        Symbol *s;
        long long v;
        reduce_expr(&e);
        if (f->reloc == FIX_PC8 || f->reloc == FIX_RELAX_JMP ||
            f->reloc == FIX_RELAX_JCC) {
            if (!short_branch_value(f, &v)) {
                fail("short branch target must be defined in the same section");
                continue;
            }
            if (v < -128 || v > 127) {
                fail("short branch target is out of range");
                continue;
            }
            patch_value(f->sec, f->offset, 1, (unsigned long long)v);
            continue;
        }
        if (expr_absolute(&e, &v) || local_pc32_value(f, &e, &v)) {
            patch_value(f->sec, f->offset, f->size, (unsigned long long)v);
            continue;
        }
        s = fixup_symbol(&e);
        if (!s)
            continue;
        patch_value(f->sec, f->offset, f->size, (unsigned)e.addend);
    }
    if (errors)
        return;
    /* Size this workspace for the input instead of putting 32 KiB on stack. */
    symname = xrealloc(0, (nsymbol ? (unsigned)nsymbol : 1) * sizeof(*symname));
    for (i = 0; i < (unsigned)nsymbol; i++)
        if (!symbols[i].defined && !symbols[i].binding_set &&
            !local_name(symbols[i].name))
            symbols[i].bind = STB_GLOBAL;
    for (i = 0; i < (unsigned)nsection; i++) {
        sections[i].out_index = i + 1;
        secname[i] = buf_string(&shstr, sections[i].name);
    }
    /* ELF locals: null, one section symbol per section, then named locals. */
    put_sym(&symtab, 0, 0, 0, 0, 0, 0);
    for (i = 0; i < (unsigned)nsection; i++)
        put_sym(&symtab, 0, 0, 0, ELF_ST_INFO(STB_LOCAL, STT_SECTION), 0, i + 1);
    j = 1 + nsection;
    for (i = 0; i < (unsigned)nsymbol; i++)
        if (symbols[i].bind == STB_LOCAL && keep_symbol(&symbols[i]))
            symbols[i].out_index = j++;
    first_global = j;
    for (i = 0; i < (unsigned)nsymbol; i++)
        if (symbols[i].bind != STB_LOCAL && keep_symbol(&symbols[i]))
            symbols[i].out_index = j++;
    for (i = 0; i < (unsigned)nsymbol; i++)
        if (keep_symbol(&symbols[i]))
            symname[i] = buf_string(&str, symbols[i].name);
    for (j = 0; j < 2; j++)
        for (i = 0; i < (unsigned)nsymbol; i++) {
            Symbol *s = &symbols[i];
            unsigned shndx, value;
            if (!keep_symbol(s) || (j == 0) != (s->bind == STB_LOCAL))
                continue;
            if (s->common) {
                shndx = SHN_COMMON;
                value = s->align ? s->align : 4;
            } else if (!s->defined) {
                shndx = SHN_UNDEF;
                value = 0;
                if (uflag)
                    fail("undefined symbol %s", s->name);
            } else if (s->sec == SEC_ABS) {
                shndx = SHN_ABS;
                value = s->value;
            } else {
                shndx = sections[s->sec].out_index;
                value = s->value;
            }
            put_sym(&symtab, symname[i], value, s->size, ELF_ST_INFO(s->bind, s->type), s->other,
                    shndx);
        }
    if (errors) {
        free(symname);
        return;
    }
    /* Emit REL records in fixup order, grouped by target section. */
    for (i = 0; i < (unsigned)nsection; i++) {
        for (j = 0; j < (unsigned)nfixup; j++)
            if (fixups[j].sec == &sections[i]) {
                Expr e = fixups[j].expr;
                Symbol *s;
                long long v;
                if (fixups[j].reloc == FIX_PC8 ||
                    fixups[j].reloc == FIX_RELAX_JMP ||
                    fixups[j].reloc == FIX_RELAX_JCC)
                    continue;
                reduce_expr(&e);
                if (expr_absolute(&e, &v) || local_pc32_value(&fixups[j], &e, &v))
                    continue;
                s = fixup_symbol(&e);
                if (!s)
                    continue;
                buf32(&rel[i], fixups[j].offset);
                buf32(&rel[i], ELF_R_INFO(s->out_index, fixups[j].reloc));
            }
        if (rel[i].size) {
            char rn[600];
            sprintf(rn, ".rel%s", sections[i].name);
            relname[i] = buf_string(&shstr, rn);
            nrelsec++;
        }
    }
    symtab_index = 1 + nsection + nrelsec;
    strtab_index = symtab_index + 1;
    shstr_index = strtab_index + 1;
    shnum = shstr_index + 1;
    buf_string(&shstr, ".symtab");
    buf_string(&shstr, ".strtab");
    buf_string(&shstr, ".shstrtab");
    /* Header placeholder. */
    for (i = 0; i < 52; i++)
        buf8(&out, 0);
    for (i = 0; i < (unsigned)nsection; i++) {
        while (out.size % sections[i].align)
            buf8(&out, 0);
        secoff[i] = out.size;
        if (sections[i].type != SHT_NOBITS)
            buf_bytes(&out, sections[i].data, sections[i].size);
    }
    for (i = 0; i < (unsigned)nsection; i++)
        if (rel[i].size) {
            while (out.size % 4)
                buf8(&out, 0);
            reloff[i] = out.size;
            buf_bytes(&out, rel[i].data, rel[i].size);
        }
    while (out.size % 4)
        buf8(&out, 0);
    off = out.size;
    buf_bytes(&out, symtab.data, symtab.size);
    {
        unsigned symoff = off;
        (void)symoff;
    }
    {
        unsigned stroff = out.size;
        buf_bytes(&out, str.data, str.size);
        while (out.size % 4)
            buf8(&out, 0);
        {
            unsigned shstroff = out.size;
            buf_bytes(&out, shstr.data, shstr.size);
            while (out.size % 4)
                buf8(&out, 0);
            shoff = out.size;
            put_shdr(&out, 0, 0, 0, 0, 0, 0, 0, 0, 0);
            for (i = 0; i < (unsigned)nsection; i++)
                put_shdr(&out, secname[i], sections[i].type, sections[i].flags, secoff[i],
                         sections[i].size, 0, 0, sections[i].align, 0);
            for (i = 0; i < (unsigned)nsection; i++)
                if (rel[i].size)
                    put_shdr(&out, relname[i], SHT_REL, 0, reloff[i], rel[i].size, symtab_index,
                             sections[i].out_index, 4, 8);
            /* Recover the three names: they are consecutive final additions. */
            {
                unsigned symsh = shstr.size - strlen(".shstrtab") - 1 - strlen(".strtab") - 1 -
                                 strlen(".symtab") - 1;
                unsigned strsh = symsh + strlen(".symtab") + 1;
                unsigned shstrsh = strsh + strlen(".strtab") + 1;
                put_shdr(&out, symsh, SHT_SYMTAB, 0, off, symtab.size, strtab_index, first_global,
                         4, 16);
                put_shdr(&out, strsh, SHT_STRTAB, 0, stroff, str.size, 0, 0, 1, 0);
                put_shdr(&out, shstrsh, SHT_STRTAB, 0, shstroff, shstr.size, 0, 0, 1, 0);
            }
        }
    }
    {
        Buffer h = { 0 };
        put_ehdr(&h, shoff, shnum, shstr_index);
        memcpy(out.data, h.data, 52);
    }
    fp = fopen(outfile, "wb");
    if (!fp)
        fatal("cannot create %s", outfile);
    if (fwrite(out.data, 1, out.size, fp) != out.size || fclose(fp))
        fatal("cannot write %s", outfile);
    free(symname);
}

static IsaMask cpu_level(unsigned level)
{
    IsaMask f = ISA_I386 | ISA_X87;
    if (level >= 486)
        f |= ISA_I486;
    if (level >= 586)
        f |= ISA_I586;
    if (level >= 686)
        f |= ISA_I686 | ISA_FXSR;
    return f;
}

static IsaMask feature_closure(IsaMask f)
{
    if (f & ISA_VMFUNC)
        f |= ISA_EPT;
    if (f & ISA_EPT)
        f |= ISA_VMX;
    if (f & ISA_XSAVEOPT)
        f |= ISA_XSAVE;
    if (f & ISA_SSE42)
        f |= ISA_SSE41 | ISA_POPCNT;
    if (f & ISA_SSE41)
        f |= ISA_SSSE3;
    if (f & ISA_SSSE3)
        f |= ISA_SSE3;
    if (f & (ISA_SSE3 | ISA_SSE4A))
        f |= ISA_SSE2;
    if (f & ISA_SSE2)
        f |= ISA_SSE;
    if (f & ISA_SSE)
        f |= ISA_MMXEXT | ISA_FXSR;
    if (f & ISA_3DNOWA)
        f |= ISA_3DNOW | ISA_MMXEXT;
    if (f & ISA_3DNOW)
        f |= ISA_MMX | ISA_PRFCHW;
    return f;
}

static int cpu_features_for(const char *name, IsaMask *features)
{
    IsaMask f;
    if (!strcmp(name, "default"))
        f = ISA_ALL;
    else if (!strcmp(name, "generic32"))
        f = cpu_level(386);
    else if (!strcmp(name, "i386"))
        f = cpu_level(386);
    else if (!strcmp(name, "i486"))
        f = cpu_level(486);
    else if (!strcmp(name, "i586") || !strcmp(name, "pentium"))
        f = cpu_level(586);
    else if (!strcmp(name, "i686") || !strcmp(name, "pentiumpro"))
        f = cpu_level(686);
    else if (!strcmp(name, "pentiumii"))
        f = cpu_level(686) | ISA_MMX;
    else if (!strcmp(name, "pentiumiii"))
        f = cpu_level(686) | ISA_MMX | ISA_SSE;
    else if (!strcmp(name, "pentium4"))
        f = cpu_level(686) | ISA_MMX | ISA_SSE | ISA_SSE2 | ISA_CLFLUSH;
    else if (!strcmp(name, "prescott") || !strcmp(name, "nocona") || !strcmp(name, "core"))
        f = cpu_level(686) | ISA_MMX | ISA_SSE | ISA_SSE2 | ISA_SSE3 | ISA_MONITOR |
            ISA_CLFLUSH;
    else if (!strcmp(name, "core2"))
        f = cpu_level(686) | ISA_MMX | ISA_SSE | ISA_SSE2 | ISA_SSE3 | ISA_SSSE3 |
            ISA_MONITOR | ISA_CLFLUSH;
    else if (!strcmp(name, "corei7") || !strcmp(name, "generic64"))
        f = cpu_level(686) | ISA_MMX | ISA_SSE42 | ISA_RDTSCP | ISA_SYSCALL | ISA_MONITOR |
            ISA_CLFLUSH;
    else if (!strcmp(name, "bdver1") || !strcmp(name, "bdver2") ||
             !strcmp(name, "bdver3") || !strcmp(name, "bdver4") ||
             !strcmp(name, "znver1") || !strcmp(name, "znver2") ||
             !strcmp(name, "znver3") || !strcmp(name, "znver4") ||
             !strcmp(name, "znver5") || !strcmp(name, "btver1") ||
             !strcmp(name, "btver2")) {
        f = cpu_level(686) | ISA_MMX | ISA_SSE42 | ISA_SSE4A | ISA_LZCNT | ISA_POPCNT |
            ISA_SYSCALL | ISA_RDTSCP | ISA_SVME | ISA_MONITOR | ISA_CLFLUSH | ISA_PRFCHW;
        if (!strncmp(name, "bdver", 5) || !strncmp(name, "znver", 5) ||
            !strcmp(name, "btver2"))
            f |= ISA_XSAVE;
        if (!strcmp(name, "bdver3") || !strcmp(name, "bdver4") ||
            !strncmp(name, "znver", 5) || !strcmp(name, "btver2"))
            f |= ISA_XSAVEOPT;
        if (!strncmp(name, "znver", 5) || !strcmp(name, "bdver4") ||
            !strcmp(name, "btver2"))
            f |= ISA_MOVBE;
        if (!strncmp(name, "znver", 5) || !strcmp(name, "bdver4"))
            f |= ISA_MWAITX;
        if (!strncmp(name, "znver", 5))
            f |= ISA_CLZERO;
        if (!strcmp(name, "znver2") || !strcmp(name, "znver3") ||
            !strcmp(name, "znver4") || !strcmp(name, "znver5"))
            f |= ISA_RDPRU;
    } else if (!strcmp(name, "atom") || !strcmp(name, "bonnell"))
        f = cpu_level(686) | ISA_MMX | ISA_SSSE3 | ISA_MONITOR | ISA_MOVBE;
    else if (!strcmp(name, "silvermont") || !strcmp(name, "slm"))
        f = cpu_level(686) | ISA_MMX | ISA_SSE42 | ISA_MONITOR | ISA_MOVBE | ISA_CLFLUSH;
    else if (!strcmp(name, "k6"))
        f = cpu_level(586) | ISA_MMX | ISA_SYSCALL;
    else if (!strcmp(name, "k6_2") || !strcmp(name, "k6-2") || !strcmp(name, "k6_3") ||
             !strcmp(name, "k6-3"))
        f = cpu_level(586) | ISA_MMX | ISA_3DNOW | ISA_SYSCALL;
    else if (!strcmp(name, "athlon") || !strcmp(name, "athlon-tbird"))
        f = cpu_level(686) | ISA_MMX | ISA_3DNOWA | ISA_SYSCALL;
    else if (!strcmp(name, "athlon_4") || !strcmp(name, "athlon-4") ||
             !strcmp(name, "athlon_xp") || !strcmp(name, "athlon-xp") ||
             !strcmp(name, "athlon_mp") || !strcmp(name, "athlon-mp"))
        f = cpu_level(686) | ISA_MMX | ISA_3DNOWA | ISA_SYSCALL | ISA_SSE;
    else if (!strcmp(name, "opteron") || !strcmp(name, "k8"))
        f = cpu_level(686) | ISA_MMX | ISA_3DNOWA | ISA_SSE2 | ISA_SYSCALL | ISA_RDTSCP |
            ISA_CLFLUSH;
    else if (!strcmp(name, "k8_sse3") || !strcmp(name, "k8-sse3") ||
             !strcmp(name, "opteron_sse3") || !strcmp(name, "opteron-sse3"))
        f = cpu_level(686) | ISA_MMX | ISA_3DNOWA | ISA_SSE3 | ISA_SYSCALL | ISA_RDTSCP |
            ISA_CLFLUSH;
    else if (!strcmp(name, "amdfam10") || !strcmp(name, "barcelona"))
        f = cpu_level(686) | ISA_MMX | ISA_3DNOWA | ISA_SSE3 | ISA_SSE4A | ISA_LZCNT |
            ISA_POPCNT | ISA_SYSCALL | ISA_RDTSCP | ISA_MONITOR | ISA_CLFLUSH |
            ISA_PRFCHW;
    else if (!strcmp(name, "geode"))
        f = cpu_level(586) | ISA_MMX | ISA_3DNOWA;
    else if (!strcmp(name, "winchip-c6"))
        f = cpu_level(586) | ISA_MMX;
    else if (!strcmp(name, "winchip2"))
        f = cpu_level(586) | ISA_MMX | ISA_3DNOW;
    else if (!strcmp(name, "c3") || !strcmp(name, "samuel-2"))
        f = cpu_level(586) | ISA_MMX | ISA_3DNOW;
    else if (!strcmp(name, "c3_2") || !strcmp(name, "c3-2") ||
             !strcmp(name, "nehemiah") || !strcmp(name, "c7") || !strcmp(name, "esther"))
        f = cpu_level(686) | ISA_MMX | ISA_SSE | ISA_PADLOCK;
    else if (!strcmp(name, "cyrix") || !strcmp(name, "cyrix6x86"))
        f = cpu_level(586) | ISA_CYRIX;
    else if (!strcmp(name, "cyrix_m2") || !strcmp(name, "cyrix-m2") ||
             !strcmp(name, "cyrix6x86mx"))
        f = cpu_level(586) | ISA_MMX | ISA_CYRIX;
    else
        return 0;
    *features = feature_closure(f);
    return 1;
}

static IsaMask extension_mask(const char *name)
{
    if (!strcmp(name, "8087") || !strcmp(name, "287") || !strcmp(name, "387") ||
        !strcmp(name, "687") || !strcmp(name, "87"))
        return ISA_X87;
    if (!strcmp(name, "cmov"))
        return ISA_I686;
    if (!strcmp(name, "mmx"))
        return ISA_MMX;
    if (!strcmp(name, "3dnow"))
        return ISA_3DNOW | ISA_MMX;
    if (!strcmp(name, "3dnowa"))
        return ISA_3DNOWA | ISA_3DNOW | ISA_MMX | ISA_MMXEXT;
    if (!strcmp(name, "fxsr"))
        return ISA_FXSR;
    if (!strcmp(name, "sse"))
        return ISA_SSE;
    if (!strcmp(name, "sse2"))
        return ISA_SSE | ISA_SSE2;
    if (!strcmp(name, "sse3"))
        return ISA_SSE | ISA_SSE2 | ISA_SSE3;
    if (!strcmp(name, "monitor"))
        return ISA_MONITOR;
    if (!strcmp(name, "ssse3"))
        return ISA_SSE | ISA_SSE2 | ISA_SSE3 | ISA_SSSE3;
    if (!strcmp(name, "sse4.1"))
        return ISA_SSE | ISA_SSE2 | ISA_SSE3 | ISA_SSSE3 | ISA_SSE41;
    if (!strcmp(name, "sse4") || !strcmp(name, "sse4.2"))
        return ISA_SSE | ISA_SSE2 | ISA_SSE3 | ISA_SSSE3 | ISA_SSE41 | ISA_SSE42;
    if (!strcmp(name, "sse4a"))
        return ISA_SSE4A | ISA_SSE3;
    if (!strcmp(name, "abm"))
        return ISA_LZCNT | ISA_POPCNT;
    if (!strcmp(name, "lzcnt"))
        return ISA_LZCNT;
    if (!strcmp(name, "popcnt"))
        return ISA_POPCNT;
    if (!strcmp(name, "syscall"))
        return ISA_SYSCALL;
    if (!strcmp(name, "rdtscp"))
        return ISA_RDTSCP;
    if (!strcmp(name, "svme"))
        return ISA_SVME;
    if (!strcmp(name, "padlock"))
        return ISA_PADLOCK;
    if (!strcmp(name, "cyrix"))
        return ISA_CYRIX;
    if (!strcmp(name, "movbe"))
        return ISA_MOVBE;
    if (!strcmp(name, "vmx"))
        return ISA_VMX;
    if (!strcmp(name, "vmfunc"))
        return ISA_VMFUNC;
    if (!strcmp(name, "smx"))
        return ISA_SMX;
    if (!strcmp(name, "xsave"))
        return ISA_XSAVE;
    if (!strcmp(name, "xsaveopt"))
        return ISA_XSAVEOPT;
    if (!strcmp(name, "ept"))
        return ISA_EPT;
    if (!strcmp(name, "prfchw"))
        return ISA_PRFCHW;
    if (!strcmp(name, "clflush"))
        return ISA_CLFLUSH;
    if (!strcmp(name, "padlockrng2"))
        return ISA_PADLOCK_RNG2;
    if (!strcmp(name, "padlockphe2"))
        return ISA_PADLOCK_PHE2;
    if (!strcmp(name, "padlockxmodx"))
        return ISA_PADLOCK_XMODX;
    if (!strcmp(name, "gmism2"))
        return ISA_GMISM2;
    if (!strcmp(name, "gmiccs"))
        return ISA_GMICCS;
    if (!strcmp(name, "mwaitx"))
        return ISA_MWAITX;
    if (!strcmp(name, "clzero"))
        return ISA_CLZERO;
    if (!strcmp(name, "rdpru"))
        return ISA_RDPRU;
    return 0;
}

static void set_extension(const char *name)
{
    const char *ext = name;
    IsaMask mask;
    int disable = 0;
    if (!strcmp(name, "no87")) {
        isa_features &= ~ISA_X87;
        return;
    }
    if (!strncmp(name, "no", 2)) {
        disable = 1;
        ext += 2;
    }
    mask = extension_mask(ext);
    if (!mask)
        fatal("unsupported -march extension '%s'", name);
    if (disable) {
        if (!strcmp(ext, "sse"))
            isa_features &= ~(ISA_SSE | ISA_SSE2 | ISA_SSE3 | ISA_SSSE3 | ISA_SSE41 | ISA_SSE42);
        else if (!strcmp(ext, "sse2"))
            isa_features &= ~(ISA_SSE2 | ISA_SSE3 | ISA_SSSE3 | ISA_SSE41 | ISA_SSE42);
        else if (!strcmp(ext, "sse3"))
            isa_features &= ~(ISA_SSE3 | ISA_SSSE3 | ISA_SSE41 | ISA_SSE42);
        else if (!strcmp(ext, "ssse3"))
            isa_features &= ~(ISA_SSSE3 | ISA_SSE41 | ISA_SSE42);
        else if (!strcmp(ext, "sse4.1"))
            isa_features &= ~(ISA_SSE41 | ISA_SSE42);
        else if (!strcmp(ext, "sse4") || !strcmp(ext, "sse4.2"))
            isa_features &= ~(ISA_SSE42 | ISA_POPCNT);
        else if (!strcmp(ext, "3dnow"))
            isa_features &= ~(ISA_3DNOW | ISA_3DNOWA);
        else if (!strcmp(ext, "mmx"))
            isa_features &= ~(ISA_MMX | ISA_3DNOW | ISA_3DNOWA);
        else
            isa_features &= ~mask;
        if (!(isa_features & (ISA_SSE | ISA_3DNOWA)))
            isa_features &= ~ISA_MMXEXT;
    } else
        isa_features = feature_closure(isa_features | mask);
}

static void set_march(const char *arg)
{
    char *copy, *p, *next;
    IsaMask features;
    copy = xstrdup(arg);
    p = copy;
    next = strpbrk(p, "+,");
    if (next)
        *next++ = 0;
    if (!strcmp(p, "push")) {
        if (isa_stack_depth >= (int)(sizeof(isa_stack) / sizeof(isa_stack[0])))
            fatal("-march=push stack overflow");
        isa_stack[isa_stack_depth++] = isa_features;
    } else if (!strcmp(p, "pop")) {
        if (!isa_stack_depth)
            fatal("-march=pop without matching push");
        isa_features = isa_stack[--isa_stack_depth];
    } else {
        if (!cpu_features_for(p, &features))
            fatal("unsupported -march CPU '%s'", p);
        isa_features = features;
        isa_cpu = xstrdup(p);
    }
    while (next) {
        p = next;
        next = strpbrk(p, "+,");
        if (next)
            *next++ = 0;
        if (*p)
            set_extension(p);
    }
    free(copy);
}

static void set_mtune(const char *name)
{
    IsaMask ignored;
    if (strcmp(name, "i8086") && strcmp(name, "i186") && strcmp(name, "i286") &&
        !cpu_features_for(name, &ignored))
        fatal("unsupported -mtune CPU '%s'", name);
    tune_cpu = name;
}

static void target_help(void)
{
    puts("i386 target options:");
    puts("  --32, -m32                 generate ELF32/i386 objects");
    puts("  -march=CPU[,+EXTENSION...] select instruction set");
    puts("  -mtune=CPU                 select scheduling CPU (syntax compatibility)");
    puts("  CPUs: default generic32 i386 i486 i586 pentium i686 pentiumpro");
    puts("        pentiumii pentiumiii pentium4 prescott nocona core core2 corei7");
    puts("        k6 k6_2 k6_3 athlon athlon-4 athlon-xp athlon-mp");
    puts("        opteron k8 k8-sse3 amdfam10 bdverN znverN btverN");
    puts("        geode winchip-c6 winchip2 c3 c3-2 nehemiah c7 esther");
    puts("        cyrix cyrix6x86 cyrix_m2 cyrix6x86mx atom silvermont");
    puts("  Extensions: 8087 287 387 687 cmov mmx 3dnow 3dnowa fxsr sse sse2");
    puts("              sse3 ssse3 sse4.1 sse4.2 sse4 sse4a monitor movbe");
    puts("              syscall rdtscp svme vmx vmfunc smx ept xsave xsaveopt");
    puts("              abm lzcnt popcnt prfchw clflush padlock cyrix no87");
    puts("              padlockrng2 padlockphe2 padlockxmodx gmism2 gmiccs");
    puts("              mwaitx clzero rdpru");
    puts("  Extensions may be prefixed with 'no'.");
}

static void usage(void)
{
    fprintf(stderr,
            "Usage:\n  as-i386 [--elf] [--32] [-march=CPU[,+EXT...]] [-mtune=CPU] "
            "[-gkuvxX] [-O[level]] [-EL] [-o outfile] [infile]\n");
    fprintf(stderr,
            "Options:\n  -o filename     Set output file name, default a.out\n  -u              "
            "Treat undefined names as error\n  -x              Discard local symbols\n  -X         "
            "     Discard locals starting with L or .\n  -EL             Select i386 little-endian "
            "output\n  --elf, --32     Write ELF32 i386 relocatable object\n  -I directory    Add "
            "an include search directory\n  --target-help   Print i386 target options\n  "
            "--target-info   Print configured target information\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int i;
    FILE *fp = stdin;
    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (!strcmp(a, "--target-info")) {
            printf("rebsd-as target=i386 elf_class=32 target_big_endian=0 elf_default=1 "
                   "march=%s mtune=%s features=0x%llx\n",
                   isa_cpu, tune_cpu, isa_features);
            return 0;
        }
        if (!strcmp(a, "--target-help")) {
            target_help();
            return 0;
        }
        if (!strcmp(a, "--elf") || !strcmp(a, "--32") || !strcmp(a, "-EL") || !strcmp(a, "-k") ||
            !strcmp(a, "-g") || !strcmp(a, "-v"))
            continue;
        if (!strcmp(a, "--aout") || !strcmp(a, "-EB")) {
            fprintf(stderr, "as-i386: %s is not supported for the ELF32 i386 target\n", a);
            return 1;
        }
        if (!strncmp(a, "-O", 2) || !strcmp(a, "-m32"))
            continue;
        if (!strncmp(a, "-march=", 7)) {
            set_march(a + 7);
            continue;
        }
        if (!strncmp(a, "-mtune=", 7)) {
            set_mtune(a + 7);
            continue;
        }
        if (!strcmp(a, "-u")) {
            uflag = 1;
            continue;
        }
        if (!strcmp(a, "-x")) {
            xflag = 1;
            continue;
        }
        if (!strcmp(a, "-X")) {
            Xflag = 1;
            continue;
        }
        if (!strcmp(a, "-o")) {
            if (++i >= argc)
                usage();
            outfile = argv[i];
            continue;
        }
        if (!strncmp(a, "-o", 2) && a[2]) {
            outfile = a + 2;
            continue;
        }
        if (!strcmp(a, "-I")) {
            if (++i >= argc)
                usage();
            if (ninclude < MAX_INCLUDE)
                include_dir[ninclude++] = argv[i];
            continue;
        }
        if (!strncmp(a, "-I", 2) && a[2]) {
            if (ninclude < MAX_INCLUDE)
                include_dir[ninclude++] = a + 2;
            continue;
        }
        if (a[0] == '-' && a[1])
            usage();
        if (infile)
            fatal("too many input files");
        infile = a;
    }
    cursec = get_section(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 16);
    get_section(".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 4);
    get_section(".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 4);
    if (infile) {
        fp = fopen(infile, "r");
        if (!fp)
            fatal("cannot open %s", infile);
    }
    assemble_file(fp, infile ? infile : "<stdin>");
    if (fp != stdin)
        fclose(fp);
    if (!errors)
        write_object();
    if (errors) {
        remove(outfile);
        return 1;
    }
    return 0;
}
