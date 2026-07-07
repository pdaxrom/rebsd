#ifndef _REBSD_ELF32_MIPS_H_
#define _REBSD_ELF32_MIPS_H_

#define EI_DATA         5
#define ELFMAG0         0x7f
#define ELFMAG1         'E'
#define ELFMAG2         'L'
#define ELFMAG3         'F'
#define ELFCLASS32      1
#define ELFDATA2LSB     1
#define ELFDATA2MSB     2
#define EV_CURRENT      1

#define ET_REL          1
#define ET_EXEC         2
#define EM_MIPS         8

#define EF_MIPS_NOREORDER  0x00000001
#define EF_MIPS_ABI_O32    0x00001000
#define EF_MIPS_ARCH_3     0x20000000
#define EF_MIPS_ARCH_32R2  0x70000000

#define PT_LOAD         1
#define PF_X            1
#define PF_W            2
#define PF_R            4

#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_NOBITS      8
#define SHT_REL         9

#define SHF_WRITE       0x1
#define SHF_ALLOC       0x2
#define SHF_EXECINSTR   0x4

#define SHN_UNDEF       0
#define SHN_ABS         0xfff1
#define SHN_COMMON      0xfff2

#define STB_LOCAL       0
#define STB_GLOBAL      1
#define STB_WEAK        2
#define STT_NOTYPE      0
#define STT_OBJECT      1
#define STT_FUNC        2
#define STT_SECTION     3
#define STT_FILE        4

#define ELF_ST_BIND(i)      ((i) >> 4)
#define ELF_ST_TYPE(i)      ((i) & 0xf)
#define ELF_ST_INFO(b, t)   (((b) << 4) | ((t) & 0xf))

#define ELF_R_SYM(i)        ((i) >> 8)
#define ELF_R_TYPE(i)       ((unsigned char)(i))
#define ELF_R_INFO(s, t)    (((s) << 8) | (unsigned char)(t))

#define R_MIPS_NONE         0
#define R_MIPS_16           1
#define R_MIPS_32           2
#define R_MIPS_26           4
#define R_MIPS_HI16         5
#define R_MIPS_LO16         6
#define R_MIPS_GPREL16      7
#define R_MIPS_PC16         10
#define R_MIPS_GPREL32      12

typedef struct {
    unsigned char e_ident[16];
    unsigned short e_type;
    unsigned short e_machine;
    unsigned e_version;
    unsigned e_entry;
    unsigned e_phoff;
    unsigned e_shoff;
    unsigned e_flags;
    unsigned short e_ehsize;
    unsigned short e_phentsize;
    unsigned short e_phnum;
    unsigned short e_shentsize;
    unsigned short e_shnum;
    unsigned short e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    unsigned p_type;
    unsigned p_offset;
    unsigned p_vaddr;
    unsigned p_paddr;
    unsigned p_filesz;
    unsigned p_memsz;
    unsigned p_flags;
    unsigned p_align;
} Elf32_Phdr;

typedef struct {
    unsigned sh_name;
    unsigned sh_type;
    unsigned sh_flags;
    unsigned sh_addr;
    unsigned sh_offset;
    unsigned sh_size;
    unsigned sh_link;
    unsigned sh_info;
    unsigned sh_addralign;
    unsigned sh_entsize;
} Elf32_Shdr;

typedef struct {
    unsigned st_name;
    unsigned st_value;
    unsigned st_size;
    unsigned char st_info;
    unsigned char st_other;
    unsigned short st_shndx;
} Elf32_Sym;

typedef struct {
    unsigned r_offset;
    unsigned r_info;
} Elf32_Rel;

#endif
