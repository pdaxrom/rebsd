/*
 * Look up symbols in a 32-bit ELF object.
 */
#include <sys/types.h>
#include <elf32.h>
#include <nlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ISVALID(p) ((p)->n_name && (p)->n_name[0])

static unsigned
get16(const unsigned char *p, int little)
{
    if (little)
        return p[0] | ((unsigned)p[1] << 8);
    return ((unsigned)p[0] << 8) | p[1];
}

static unsigned
get32(const unsigned char *p, int little)
{
    if (little)
        return p[0] | ((unsigned)p[1] << 8) |
            ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
        ((unsigned)p[2] << 8) | p[3];
}

static int
read_at(FILE *fp, unsigned off, void *buf, unsigned size)
{
    return fseek(fp, (off_t)off, SEEK_SET) == 0 &&
        fread(buf, 1, size, fp) == size;
}

static int
read_shdr(FILE *fp, unsigned shoff, unsigned shentsize, unsigned index,
    int little, Elf32_Shdr *sh)
{
    unsigned char b[40];

    if (shentsize < sizeof(b) ||
        !read_at(fp, shoff + index * shentsize, b, sizeof(b)))
        return 0;
    sh->sh_name = get32(b + 0, little);
    sh->sh_type = get32(b + 4, little);
    sh->sh_flags = get32(b + 8, little);
    sh->sh_addr = get32(b + 12, little);
    sh->sh_offset = get32(b + 16, little);
    sh->sh_size = get32(b + 20, little);
    sh->sh_link = get32(b + 24, little);
    sh->sh_info = get32(b + 28, little);
    sh->sh_addralign = get32(b + 32, little);
    sh->sh_entsize = get32(b + 36, little);
    return 1;
}

static unsigned short
symbol_type(const Elf32_Sym *sym, const Elf32_Shdr *sections,
    unsigned section_count)
{
    unsigned type, bind;

    if (sym->st_shndx == SHN_UNDEF)
        type = N_UNDF;
    else if (sym->st_shndx == SHN_ABS)
        type = N_ABS;
    else if (sym->st_shndx == SHN_COMMON)
        type = N_COMM;
    else if (sym->st_shndx >= section_count)
        type = N_ABS;
    else if (sections[sym->st_shndx].sh_type == SHT_NOBITS)
        type = N_BSS;
    else if (sections[sym->st_shndx].sh_flags & SHF_WRITE)
        type = N_DATA;
    else
        type = N_TEXT;

    bind = ELF_ST_BIND(sym->st_info);
    if (bind == STB_GLOBAL)
        type |= N_EXT;
    else if (bind == STB_WEAK)
        type |= N_EXT | N_WEAK;
    return type;
}

int
nlist(char *name, struct nlist *list)
{
    unsigned char ehdr[52], rawsym[16];
    Elf32_Shdr *sections, symtab, strtab;
    FILE *fp;
    struct nlist *request;
    unsigned machine, shoff, shentsize, shnum, symtab_index;
    unsigned sym_entsize, sym_count, request_count, i, request_index;
    int little, missing;
    unsigned char *matched;
    char *strings;

    fp = fopen(name, "r");
    if (fp == NULL)
        return -1;
    sections = NULL;
    strings = NULL;
    matched = NULL;
    missing = -1;

    if (!read_at(fp, 0, ehdr, sizeof(ehdr)) ||
        ehdr[0] != ELFMAG0 || ehdr[1] != ELFMAG1 ||
        ehdr[2] != ELFMAG2 || ehdr[3] != ELFMAG3 ||
        ehdr[4] != ELFCLASS32 ||
        (ehdr[EI_DATA] != ELFDATA2LSB &&
         ehdr[EI_DATA] != ELFDATA2MSB))
        goto done;
    little = ehdr[EI_DATA] == ELFDATA2LSB;
    machine = get16(ehdr + 18, little);
    shoff = get32(ehdr + 32, little);
    shentsize = get16(ehdr + 46, little);
    shnum = get16(ehdr + 48, little);
    if ((machine != EM_MIPS && machine != EM_386) || shoff == 0 ||
        shnum == 0 || shentsize < sizeof(Elf32_Shdr))
        goto done;

    sections = malloc(shnum * sizeof(*sections));
    if (sections == NULL)
        goto done;
    symtab_index = shnum;
    for (i = 0; i < shnum; i++) {
        if (!read_shdr(fp, shoff, shentsize, i, little, &sections[i]))
            goto done;
        if (sections[i].sh_type == SHT_SYMTAB)
            symtab_index = i;
    }
    if (symtab_index == shnum)
        goto done;
    symtab = sections[symtab_index];
    if (symtab.sh_link >= shnum ||
        sections[symtab.sh_link].sh_type != SHT_STRTAB)
        goto done;
    strtab = sections[symtab.sh_link];
    if (strtab.sh_size == 0)
        goto done;
    strings = malloc(strtab.sh_size);
    if (strings == NULL ||
        !read_at(fp, strtab.sh_offset, strings, strtab.sh_size))
        goto done;

    for (request = list, request_count = 0; ISVALID(request); request++) {
        request->n_type = 0;
        request->n_value = 0;
        request_count++;
    }
    matched = calloc(request_count ? request_count : 1, 1);
    if (matched == NULL)
        goto done;
    missing = request_count;
    sym_entsize = symtab.sh_entsize ? symtab.sh_entsize : sizeof(rawsym);
    if (sym_entsize < sizeof(rawsym)) {
        missing = -1;
        goto done;
    }
    sym_count = symtab.sh_size / sym_entsize;
    for (i = 0; i < sym_count && missing != 0; i++) {
        Elf32_Sym sym;

        if (!read_at(fp, symtab.sh_offset + i * sym_entsize,
            rawsym, sizeof(rawsym))) {
            missing = -1;
            goto done;
        }
        sym.st_name = get32(rawsym + 0, little);
        sym.st_value = get32(rawsym + 4, little);
        sym.st_size = get32(rawsym + 8, little);
        sym.st_info = rawsym[12];
        sym.st_other = rawsym[13];
        sym.st_shndx = get16(rawsym + 14, little);
        if (sym.st_name == 0 || sym.st_name >= strtab.sh_size ||
            memchr(strings + sym.st_name, 0,
                strtab.sh_size - sym.st_name) == NULL)
            continue;
        for (request = list, request_index = 0; ISVALID(request);
            request++, request_index++) {
            if (matched[request_index])
                continue;
            if (strcmp(request->n_name, strings + sym.st_name) != 0)
                continue;
            request->n_value = sym.st_value;
            request->n_type = symbol_type(&sym, sections, shnum);
            matched[request_index] = 1;
            missing--;
            break;
        }
    }

done:
    free(matched);
    free(strings);
    free(sections);
    fclose(fp);
    return missing;
}
