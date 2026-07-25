#ifndef _I386_ELF_MACHDEP_H_
#define _I386_ELF_MACHDEP_H_

#define ELF_MACHDEP_ID_CASES                                            \
                case EM_386:                                            \
                        break;

#define ARCH_ELFSIZE            32
#define ELF_TARGET_DATA         ELFDATA2LSB

#define R_386_NONE              0
#define R_386_32                1
#define R_386_PC32              2
#define R_386_GOT32             3
#define R_386_PLT32             4
#define R_386_COPY              5
#define R_386_GLOB_DAT          6
#define R_386_JMP_SLOT          7
#define R_386_RELATIVE          8
#define R_386_GOTOFF            9
#define R_386_GOTPC             10

#define R_386_max               10
#define R_TYPE(name)            __CONCAT(R_386_,name)

#ifdef _KERNEL
#define ELF_INTERP_NON_RELOCATABLE
#endif

#endif
