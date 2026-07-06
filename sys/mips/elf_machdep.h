/*      $NetBSD: elf_machdep.h,v 1.7 2000/04/02 15:35:50 minoura Exp $  */

#define ELF_MACHDEP_ID_CASES                                            \
                case EM_MIPS:                                           \
                        break;

#define ARCH_ELFSIZE            32
#ifdef TARGET_LITTLE_ENDIAN
#define ELF_TARGET_DATA         ELFDATA2LSB
#else
#define ELF_TARGET_DATA         ELFDATA2MSB
#endif

#define R_MIPS_NONE             0
#define R_MIPS_16               1
#define R_MIPS_32               2
#define R_MIPS_REL32            3
#define R_MIPS_REL              R_MIPS_REL32
#define R_MIPS_26               4
#define R_MIPS_HI16             5
#define R_MIPS_LO16             6
#define R_MIPS_GPREL16          7
#define R_MIPS_LITERAL          8
#define R_MIPS_GOT16            9
#define R_MIPS_GOT              R_MIPS_GOT16
#define R_MIPS_PC16             10
#define R_MIPS_CALL16           11
#define R_MIPS_CALL             R_MIPS_CALL16
#define R_MIPS_GPREL32          12

#define R_MIPS_max              37
#define R_TYPE(name)            __CONCAT(R_MIPS_,name)

#ifdef _KERNEL
#define ELF_INTERP_NON_RELOCATABLE
#endif
