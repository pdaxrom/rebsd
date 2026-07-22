/*
 * Assembler for MIPS32.
 * The syntax is compatible with GCC output.
 *
 * Copyright (C) 2011-2012 Serge Vakulenko, <serge@vak.ru>
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
#include <nlist.h>
#include <stdio.h>
#else
#include <stdio.h>
#endif
#include <a.out.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../aoutio.h"
#include "../elf32_mips.h"

#define WORDSZ 4 /* word size in bytes */
#ifdef TARGET_VR4300
#define DEFAULT_TEXT_ALIGN_BITS 3 /* VR4300 double loads require 8-byte alignment. */
#else
#define DEFAULT_TEXT_ALIGN_BITS 2
#endif

/*
 * Locals beginning with L or dot are stripped off by -X flag.
 */
#define IS_LOCAL(s) ((s)->n_name[0] == 'L' || (s)->n_name[0] == '.' || \
                     (s)->n_name[0] == '$')
#define REBSD_CTORS_SIZE_SYM ".__rebsd_ctors_size"
#define REBSD_DTORS_SIZE_SYM ".__rebsd_dtors_size"

/*
 * Types of lexemes.
 */
enum {
    LEOF = 1,  /* end of file */
    LEOL,      /* end of line */
    LNAME,     /* identifier */
    LREG,      /* machine register */
    LFREG,     /* floating-point register */
    LNUM,      /* integer number */
    LLSHIFT,   /* << */
    LRSHIFT,   /* >> */
    LASCII,    /* .ascii */
    LFCC,      /* floating-point condition-code register */
    LBSS,      /* .bss */
    LCOMM,     /* .comm */
    LDATA,     /* .data */
    LGLOBL,    /* .globl */
    LEXTERN,   /* .extern */
    LHALF,     /* .half */
    LSTRNG,    /* .strng */
    LRDATA,    /* .rdata */
    LTEXT,     /* .text */
    LEQU,      /* .equ */
    LWORD,     /* .word */
    LBYTE,     /* .byte */
    LSPACE,    /* .space */
    LFILE,     /* .file */
    LSECTION,  /* .section */
    LSYMTYPE,  /* @type of symbol */
    LSECTYPE,  /* @type of section */
    LPREVIOUS, /* .previous */
    LGNUATTR,  /* .gnu_attribute */
    LALIGN,    /* .align */
    LSET,      /* .set */
    LENT,      /* .ent */
    LTYPE,     /* .type */
    LFRAME,    /* .frame */
    LMASK,     /* .mask */
    LFMASK,    /* .fmask */
    LMODULE,   /* .module */
    LEND,      /* .end */
    LSIZE,     /* .size */
    LIDENT,    /* .ident */
    LWEAK,     /* .weak */
    LLOCAL,    /* .local */
    LLCOMM,    /* .lcomm */
    LNAN,      /* .nan */
};

/*
 * Segment ids.
 */
enum {
    STEXT,
    SDATA,
    SSTRNG,
    SCTORS,
    SDTORS,
    SBSS,
    SEXT,
    SABS, /* special case for getexpr() */
};

/*
 * Sizes of tables.
 * Hash sizes should be powers of 2!
 */
#define HASHSZ 8192              /* symbol name hash table size */
#define HCMDSZ 256               /* instruction hash table size */
#define STSIZE (HASHSZ * 3 / 4)  /* symbol name table size */
#define STSPACE (STSIZE * 32)    /* symbol string storage size */
#define MAXRLAB 200              /* max relative (digit) labels */
#define MAXSEGM 128              /* max output sections in ELF mode */
#define SECTION_RELOC_INDEX_BASE STSIZE

/*
 * On second pass, hashtab[] is not needed.
 * We use it under name newindex[] to reindex symbol references
 * when -x or -X options are enabled.
 */
#define newindex hashtab

/*
 * Convert segment id to symbol type.
 */
const int segmtype[] = {
    N_TEXT,  /* STEXT */
    N_DATA,  /* SDATA */
    N_STRNG, /* SSTRNG */
    N_CTORS, /* SCTORS */
    N_DTORS, /* SDTORS */
    N_BSS,   /* SBSS */
    N_UNDF,  /* SEXT */
    N_ABS,   /* SABS */
};

/*
 * Convert segment id to relocation type.
 */
const int segmrel[] = {
    RTEXT,  /* STEXT */
    RDATA,  /* SDATA */
    RSTRNG, /* SSTRNG */
    RCTORS, /* SCTORS */
    RDTORS, /* SDTORS */
    RBSS,   /* SBSS */
    REXT,   /* SEXT */
    RABS,   /* SABS */
};

/*
 * Convert symbol type to segment id.
 */
const int typesegm[] = {
    SEXT,   /* N_UNDF */
    SABS,   /* N_ABS */
    STEXT,  /* N_TEXT */
    SDATA,  /* N_DATA */
    SBSS,   /* N_BSS */
    SSTRNG, /* N_STRNG */
    SEXT,   /* N_COMM */
    SCTORS, /* N_CTORS */
    SDTORS, /* N_DTORS */
};

/*
 * Table of local (numeric) labels.
 */
struct labeltab {
    int num;
    int value;
};

struct asm_section {
    const char *name;
    unsigned type;
    unsigned flags;
    unsigned align;
    int ntype;
    int rel;
    int valid;
    int touched;
    int shndx;
    int symndx;
};

#define RLAB_OFFSET (1 << 23) /* index offset of relative label */
#define RLAB_MAXVAL 1000000   /* max value of relative label */

/*
 * Main opcode table.
 */
struct optable {
    unsigned opcode;                        /* instruction code */
    const char *name;                       /* instruction name */
    unsigned type;                          /* flags */
    void (*func)(unsigned, struct reloc *); /* handler for pseudo-instructions */
};

/*
 * Flags of instruction formats.
 */
#define FRD1 (1 << 0)     /* rd, ... */
#define FRD2 (1 << 1)     /* .., rd, ... */
#define FRT1 (1 << 2)     /* rt, ... */
#define FRT2 (1 << 3)     /* .., rt, ... */
#define FRT3 (1 << 4)     /* .., .., rt */
#define FRS1 (1 << 5)     /* rs, ... */
#define FRS2 (1 << 6)     /* .., rs, ... */
#define FRS3 (1 << 7)     /* .., .., rs */
#define FRSB (1 << 8)     /* ... (rs) */
#define FCODE (1 << 9)    /* immediate shifted <<6 */
#define FDSLOT (1 << 10)  /* have delay slot */
#define FOFF16 (1 << 11)  /* 16-bit relocatable offset */
#define FHIGH16 (1 << 12) /* high 16-bit relocatable offset */
#define FOFF18 (1 << 13)  /* 18-bit PC-relative relocatable offset shifted >>2 */
#define FAOFF18 (1 << 14) /* 18-bit PC-relative relocatable offset shifted >>2 */
#define FAOFF28 (1 << 15) /* 28-bit absolute relocatable offset shifted >>2 */
#define FSA (1 << 16)     /* 5-bit shift amount */
#define FSEL (1 << 17)    /* optional 3-bit COP0 register select */
#define FSIZE (1 << 18)   /* bit field size */
#define FMSB (1 << 19)    /* bit field msb */
#define FRTD (1 << 20)    /* set rt to rd number */
#define FCODE16 (1 << 21) /* immediate shifted <<16 */
#define FMOD (1 << 22)    /* modifies the first register */
#define FNO_VR4300 (1 << 23) /* not supported by NEC VR4300 */
#define FFD1 (1 << 24)    /* fd, ... */
#define FFS1 (1 << 25)    /* fs, ... */
#define FFT1 (1 << 26)    /* ft, ... */
#define FFS2 (1 << 27)    /* .., fs, ... */
#define FFT2 (1 << 28)    /* .., ft, ... */
#define FFT3 (1 << 29)    /* .., .., ft */
#define FCACHEOP (1 << 30) /* 5-bit cache operation code */
#define FVR4300_ONLY (1U << 31) /* requires VR4300/MIPS3 mode */

/*
 * Implement pseudo-instructions.
 * TODO: bge, bgeu, bgt, bgtu, ble, bleu, blt, bltu
 */
void emit_abs_load(unsigned, struct reloc *, unsigned);
void emit_li(unsigned, struct reloc *);
void emit_la(unsigned, struct reloc *);
void emit_mul(unsigned, struct reloc *);
void emit_fp4(unsigned, struct reloc *);
void emit_fpidxmem(unsigned, struct reloc *);
void emit_mem_pseudo(unsigned, unsigned, struct reloc *, int);
void emit_mem_base_pseudo(unsigned, unsigned, struct reloc *, int, int, int);
void reorder_flush(void);
void switchsection(int);
void previoussection(void);
int fits_signed16(unsigned);
int has_empty_mem_offset(void);

const struct optable optable[] = {
    { 0x00000020, "add", FRD1 | FRS2 | FRT3 | FMOD },
    { 0x20000000, "addi", FRT1 | FRS2 | FOFF16 | FMOD },
    { 0x24000000, "addiu", FRT1 | FRS2 | FOFF16 | FMOD },
    { 0x00000021, "addu", FRD1 | FRS2 | FRT3 | FMOD },
    { 0x00000024, "and", FRD1 | FRS2 | FRT3 | FMOD },
    { 0x30000000, "andi", FRT1 | FRS2 | FOFF16 | FMOD },
    { 0x10000000, "b", FAOFF18 | FDSLOT },
    { 0x04110000, "bal", FAOFF18 | FDSLOT },
    { 0x45000000, "bc1f", FAOFF18 | FDSLOT },
    { 0x45020000, "bc1fl", FAOFF18 | FDSLOT },
    { 0x45010000, "bc1t", FAOFF18 | FDSLOT },
    { 0x45030000, "bc1tl", FAOFF18 | FDSLOT },
    { 0x10000000, "beq", FRS1 | FRT2 | FOFF18 | FDSLOT },
    { 0x50000000, "beql", FRS1 | FRT2 | FOFF18 | FDSLOT },
    { 0x10000000, "beqz", FRS1 | FOFF18 | FDSLOT },
    { 0x50000000, "beqzl", FRS1 | FOFF18 | FDSLOT },
    { 0x04010000, "bgez", FRS1 | FOFF18 | FDSLOT },
    { 0x04110000, "bgezal", FRS1 | FOFF18 | FDSLOT },
    { 0x04130000, "bgezall", FRS1 | FOFF18 | FDSLOT },
    { 0x04030000, "bgezl", FRS1 | FOFF18 | FDSLOT },
    { 0x1c000000, "bgtz", FRS1 | FOFF18 | FDSLOT },
    { 0x5c000000, "bgtzl", FRS1 | FOFF18 | FDSLOT },
    { 0x18000000, "blez", FRS1 | FOFF18 | FDSLOT },
    { 0x58000000, "blezl", FRS1 | FOFF18 | FDSLOT },
    { 0x04000000, "bltz", FRS1 | FOFF18 | FDSLOT },
    { 0x04100000, "bltzal", FRS1 | FOFF18 | FDSLOT },
    { 0x04120000, "bltzall", FRS1 | FOFF18 | FDSLOT },
    { 0x04020000, "bltzl", FRS1 | FOFF18 | FDSLOT },
    { 0x14000000, "bne", FRS1 | FRT2 | FOFF18 | FDSLOT },
    { 0x54000000, "bnel", FRS1 | FRT2 | FOFF18 | FDSLOT },
    { 0x14000000, "bnez", FRS1 | FOFF18 | FDSLOT },
    { 0x54000000, "bnezl", FRS1 | FOFF18 | FDSLOT },
    { 0x0000000d, "break", FCODE16 },
    { 0x70000021, "clo", FRD1 | FRS2 | FRTD | FMOD | FNO_VR4300 },
    { 0x70000020, "clz", FRD1 | FRS2 | FRTD | FMOD | FNO_VR4300 },
    { 0x46200032, "c.eq.d", FFS1 | FFT2 },
    { 0x46000032, "c.eq.s", FFS1 | FFT2 },
    { 0x46200030, "c.f.d", FFS1 | FFT2 },
    { 0x46000030, "c.f.s", FFS1 | FFT2 },
    { 0x4620003e, "c.le.d", FFS1 | FFT2 },
    { 0x4600003e, "c.le.s", FFS1 | FFT2 },
    { 0x4620003c, "c.lt.d", FFS1 | FFT2 },
    { 0x4600003c, "c.lt.s", FFS1 | FFT2 },
    { 0x4620003d, "c.nge.d", FFS1 | FFT2 },
    { 0x4600003d, "c.nge.s", FFS1 | FFT2 },
    { 0x46200039, "c.ngle.d", FFS1 | FFT2 },
    { 0x46000039, "c.ngle.s", FFS1 | FFT2 },
    { 0x4620003b, "c.ngl.d", FFS1 | FFT2 },
    { 0x4600003b, "c.ngl.s", FFS1 | FFT2 },
    { 0x4620003f, "c.ngt.d", FFS1 | FFT2 },
    { 0x4600003f, "c.ngt.s", FFS1 | FFT2 },
    { 0x46200036, "c.ole.d", FFS1 | FFT2 },
    { 0x46000036, "c.ole.s", FFS1 | FFT2 },
    { 0x46200034, "c.olt.d", FFS1 | FFT2 },
    { 0x46000034, "c.olt.s", FFS1 | FFT2 },
    { 0x4620003a, "c.seq.d", FFS1 | FFT2 },
    { 0x4600003a, "c.seq.s", FFS1 | FFT2 },
    { 0x46200038, "c.sf.d", FFS1 | FFT2 },
    { 0x46000038, "c.sf.s", FFS1 | FFT2 },
    { 0x46200033, "c.ueq.d", FFS1 | FFT2 },
    { 0x46000033, "c.ueq.s", FFS1 | FFT2 },
    { 0x46200037, "c.ule.d", FFS1 | FFT2 },
    { 0x46000037, "c.ule.s", FFS1 | FFT2 },
    { 0x46200035, "c.ult.d", FFS1 | FFT2 },
    { 0x46000035, "c.ult.s", FFS1 | FFT2 },
    { 0x46200031, "c.un.d", FFS1 | FFT2 },
    { 0x46000031, "c.un.s", FFS1 | FFT2 },
    { 0xbc000000, "cache", FCACHEOP | FOFF16 | FRSB },
    { 0x44400000, "cfc1", FRT1 | FRD2 | FMOD },
    { 0x44c00000, "ctc1", FRT1 | FRD2 },
    { 0x4200001f, "deret", FNO_VR4300 },
    { 0x41606000, "di", FRT1 | FMOD | FNO_VR4300 },
    { 0x0000001a, "div", FRS1 | FRT2 },
    { 0x0000001b, "divu", FRS1 | FRT2 },
    { 0x000000c0, "ehb", 0 },
    { 0x41606020, "ei", FRT1 | FMOD | FNO_VR4300 },
    { 0x42000018, "eret", 0 },
    { 0x7c000000, "ext", FRT1 | FRS2 | FSA | FSIZE | FMOD | FNO_VR4300 },
    { 0x7c000004, "ins", FRT1 | FRS2 | FSA | FMSB | FMOD | FNO_VR4300 },
    { 0x08000000, "j", FAOFF28 | FDSLOT },
    { 0x0c000000, "jal", FAOFF28 | FDSLOT },
    { 0x00000009, "jalr", FRD1 | FRS2 | FDSLOT },
    { 0x00000409, "jalr.hb", FRD1 | FRS2 | FDSLOT | FNO_VR4300 },
    { 0x00000008, "jr", FRS1 | FDSLOT },
    { 0x00000408, "jr.hb", FRS1 | FDSLOT | FNO_VR4300 },
    { 0, "la", FRT1 | FMOD, emit_la },
    { 0x80000000, "lb", FRT1 | FOFF16 | FRSB | FMOD },
    { 0x90000000, "lbu", FRT1 | FOFF16 | FRSB | FMOD },
    { 0xd4000000, "l.d", FFT1 | FOFF16 | FRSB },
    { 0xc4000000, "l.s", FFT1 | FOFF16 | FRSB },
    { 0xdc000000, "ld", FRT1 | FOFF16 | FRSB | FMOD | FVR4300_ONLY },
    { 0xd4000000, "ldc1", FFT1 | FOFF16 | FRSB },
    { 0x4c000001, "ldxc1", FNO_VR4300, emit_fpidxmem },
    { 0x84000000, "lh", FRT1 | FOFF16 | FRSB | FMOD },
    { 0x94000000, "lhu", FRT1 | FOFF16 | FRSB | FMOD },
    { 0, "li", FRT1 | FMOD, emit_li },
    { 0xc0000000, "ll", FRT1 | FOFF16 | FRSB | FMOD },
    { 0x3c000000, "lui", FRT1 | FHIGH16 | FMOD },
    { 0x8c000000, "lw", FRT1 | FOFF16 | FRSB | FMOD },
    { 0xc4000000, "lwc1", FFT1 | FOFF16 | FRSB },
    { 0x4c000000, "lwxc1", FNO_VR4300, emit_fpidxmem },
    { 0x88000000, "lwl", FRT1 | FOFF16 | FRSB | FMOD },
    { 0x98000000, "lwr", FRT1 | FOFF16 | FRSB | FMOD },
    { 0x70000000, "madd", FRS1 | FRT2 | FMOD | FNO_VR4300 },
    { 0x70000001, "maddu", FRS1 | FRT2 | FMOD | FNO_VR4300 },
    { 0x40000000, "mfc0", FRT1 | FRD2 | FSEL | FMOD },
    { 0x44000000, "mfc1", FRT1 | FFS2 | FMOD },
    { 0x00000010, "mfhi", FRD1 | FMOD },
    { 0x00000012, "mflo", FRD1 | FMOD },
    { 0x00000025, "move", FRD1 | FRS2 | FMOD }, // or
    { 0x0000000b, "movn", FRD1 | FRS2 | FRT3 | FMOD | FNO_VR4300 },
    { 0x0000000a, "movz", FRD1 | FRS2 | FRT3 | FMOD | FNO_VR4300 },
    { 0x70000004, "msub", FRS1 | FRT2 | FMOD | FNO_VR4300 },
    { 0x70000005, "msubu", FRS1 | FRT2 | FMOD | FNO_VR4300 },
    { 0x4c000021, "madd.d", FNO_VR4300, emit_fp4 },
    { 0x4c000020, "madd.s", FNO_VR4300, emit_fp4 },
    { 0x4c000029, "msub.d", FNO_VR4300, emit_fp4 },
    { 0x4c000028, "msub.s", FNO_VR4300, emit_fp4 },
    { 0x4c000031, "nmadd.d", FNO_VR4300, emit_fp4 },
    { 0x4c000030, "nmadd.s", FNO_VR4300, emit_fp4 },
    { 0x4c000039, "nmsub.d", FNO_VR4300, emit_fp4 },
    { 0x4c000038, "nmsub.s", FNO_VR4300, emit_fp4 },
    { 0x40800000, "mtc0", FRT1 | FRD2 | FSEL },
    { 0x44800000, "mtc1", FRT1 | FFS2 },
    { 0x00000011, "mthi", FRS1 },
    { 0x00000013, "mtlo", FRS1 },
    { 0x70000002, "mul", FRD1 | FRS2 | FRT3 | FMOD | FNO_VR4300 },
    { 0x00000018, "mult", FRS1 | FRT2 },
    { 0x00000019, "multu", FRS1 | FRT2 },
    { 0x00000022, "neg", FRD1 | FRT2 | FMOD },
    { 0x00000023, "negu", FRD1 | FRT2 | FMOD },
    { 0x00000000, "nop", 0 },
    { 0x00000027, "nor", FRD1 | FRS2 | FRT3 | FMOD },
    { 0x00000025, "or", FRD1 | FRS2 | FRT3 | FMOD },
    { 0x34000000, "ori", FRT1 | FRS2 | FOFF16 | FMOD },
    { 0x7c00003b, "rdhwr", FRT1 | FRD2 | FMOD | FNO_VR4300 },
    { 0x41400000, "rdpgpr", FRD1 | FRT2 | FMOD | FNO_VR4300 },
    { 0x00200002, "ror", FRD1 | FRT2 | FSA | FMOD | FNO_VR4300 },
    { 0x00000046, "rorv", FRD1 | FRT2 | FRS3 | FMOD | FNO_VR4300 },
    { 0xa0000000, "sb", FRT1 | FOFF16 | FRSB },
    { 0xe0000000, "sc", FRT1 | FOFF16 | FRSB },
    { 0xf4000000, "s.d", FFT1 | FOFF16 | FRSB },
    { 0xe4000000, "s.s", FFT1 | FOFF16 | FRSB },
    { 0xfc000000, "sd", FRT1 | FOFF16 | FRSB | FVR4300_ONLY },
    { 0xf4000000, "sdc1", FFT1 | FOFF16 | FRSB },
    { 0x4c000009, "sdxc1", FNO_VR4300, emit_fpidxmem },
    { 0x7000003f, "sdbbp", FCODE | FNO_VR4300 },
    { 0x7c000420, "seb", FRD1 | FRT2 | FNO_VR4300 },
    { 0x7c000620, "seh", FRD1 | FRT2 | FNO_VR4300 },
    { 0xa4000000, "sh", FRT1 | FOFF16 | FRSB },
    { 0x00000000, "sll", FRD1 | FRT2 | FSA | FMOD },
    { 0x00000004, "sllv", FRD1 | FRT2 | FRS3 | FMOD },
    { 0x0000002a, "slt", FRD1 | FRS2 | FRT3 | FMOD },
    { 0x28000000, "slti", FRT1 | FRS2 | FOFF16 | FMOD },
    { 0x2c000000, "sltiu", FRT1 | FRS2 | FOFF16 | FMOD },
    { 0x0000002b, "sltu", FRD1 | FRS2 | FRT3 | FMOD },
    { 0x00000003, "sra", FRD1 | FRT2 | FSA | FMOD },
    { 0x00000007, "srav", FRD1 | FRT2 | FRS3 | FMOD },
    { 0x00000002, "srl", FRD1 | FRT2 | FSA | FMOD },
    { 0x00000006, "srlv", FRD1 | FRT2 | FRS3 | FMOD },
    { 0x00000040, "ssnop", 0 },
    { 0x00000022, "sub", FRD1 | FRS2 | FRT3 | FMOD },
    { 0x00000023, "subu", FRD1 | FRS2 | FRT3 | FMOD },
    { 0xac000000, "sw", FRT1 | FOFF16 | FRSB },
    { 0xe4000000, "swc1", FFT1 | FOFF16 | FRSB },
    { 0x4c000008, "swxc1", FNO_VR4300, emit_fpidxmem },
    { 0xa8000000, "swl", FRT1 | FOFF16 | FRSB },
    { 0xb8000000, "swr", FRT1 | FOFF16 | FRSB },
    { 0x0000000f, "sync", FCODE },
    { 0x0000000c, "syscall", FCODE },
    { 0x00000034, "teq", FRS1 | FRT2 | FCODE },
    { 0x040c0000, "teqi", FRS1 | FOFF16 },
    { 0x00000030, "tge", FRS1 | FRT2 | FCODE },
    { 0x04080000, "tgei", FRS1 | FOFF16 },
    { 0x04090000, "tgeiu", FRS1 | FOFF16 },
    { 0x00000031, "tgeu", FRS1 | FRT2 | FCODE },
    { 0x00000032, "tlt", FRS1 | FRT2 | FCODE },
    { 0x040a0000, "tlti", FRS1 | FOFF16 },
    { 0x040b0000, "tltiu", FRS1 | FOFF16 },
    { 0x00000033, "tltu", FRS1 | FRT2 | FCODE },
    { 0x00000036, "tne", FRS1 | FRT2 | FCODE },
    { 0x040e0000, "tnei", FRS1 | FOFF16 },
    { 0x42000008, "tlbp", 0 },
    { 0x42000001, "tlbr", 0 },
    { 0x42000002, "tlbwi", 0 },
    { 0x42000006, "tlbwr", 0 },
    { 0x42000020, "wait", FCODE },
    { 0x41c00000, "wrpgpr", FRD1 | FRT2 | FNO_VR4300 },
    { 0x7c0000a0, "wsbh", FRD1 | FRT2 | FMOD | FNO_VR4300 },
    { 0x00000026, "xor", FRD1 | FRS2 | FRT3 | FMOD },
    { 0x38000000, "xori", FRT1 | FRS2 | FOFF16 | FMOD },
    { 0x46200000, "add.d", FFD1 | FFS2 | FFT3 },
    { 0x46000000, "add.s", FFD1 | FFS2 | FFT3 },
    { 0x46200005, "abs.d", FFD1 | FFS2 },
    { 0x46000005, "abs.s", FFD1 | FFS2 },
    { 0x46200021, "cvt.d.d", FFD1 | FFS2 },
    { 0x46000021, "cvt.d.s", FFD1 | FFS2 },
    { 0x46a00021, "cvt.d.l", FFD1 | FFS2 },
    { 0x46800021, "cvt.d.w", FFD1 | FFS2 },
    { 0x46200025, "cvt.l.d", FFD1 | FFS2 },
    { 0x46000025, "cvt.l.s", FFD1 | FFS2 },
    { 0x46200020, "cvt.s.d", FFD1 | FFS2 },
    { 0x46000020, "cvt.s.s", FFD1 | FFS2 },
    { 0x46a00020, "cvt.s.l", FFD1 | FFS2 },
    { 0x46800020, "cvt.s.w", FFD1 | FFS2 },
    { 0x46200024, "cvt.w.d", FFD1 | FFS2 },
    { 0x46000024, "cvt.w.s", FFD1 | FFS2 },
    { 0x46200003, "div.d", FFD1 | FFS2 | FFT3 },
    { 0x46000003, "div.s", FFD1 | FFS2 | FFT3 },
    { 0x46200006, "mov.d", FFD1 | FFS2 },
    { 0x46000006, "mov.s", FFD1 | FFS2 },
    { 0x46200002, "mul.d", FFD1 | FFS2 | FFT3 },
    { 0x46000002, "mul.s", FFD1 | FFS2 | FFT3 },
    { 0x46200007, "neg.d", FFD1 | FFS2 },
    { 0x46000007, "neg.s", FFD1 | FFS2 },
    { 0x4620000a, "ceil.l.d", FFD1 | FFS2 },
    { 0x4600000a, "ceil.l.s", FFD1 | FFS2 },
    { 0x4620000e, "ceil.w.d", FFD1 | FFS2 },
    { 0x4600000e, "ceil.w.s", FFD1 | FFS2 },
    { 0x4620000b, "floor.l.d", FFD1 | FFS2 },
    { 0x4600000b, "floor.l.s", FFD1 | FFS2 },
    { 0x4620000f, "floor.w.d", FFD1 | FFS2 },
    { 0x4600000f, "floor.w.s", FFD1 | FFS2 },
    { 0x46200008, "round.l.d", FFD1 | FFS2 },
    { 0x46000008, "round.l.s", FFD1 | FFS2 },
    { 0x4620000c, "round.w.d", FFD1 | FFS2 },
    { 0x4600000c, "round.w.s", FFD1 | FFS2 },
    { 0x46200004, "sqrt.d", FFD1 | FFS2 },
    { 0x46000004, "sqrt.s", FFD1 | FFS2 },
    { 0x46200009, "trunc.l.d", FFD1 | FFS2 },
    { 0x46000009, "trunc.l.s", FFD1 | FFS2 },
    { 0x4620000d, "trunc.w.d", FFD1 | FFS2 },
    { 0x4600000d, "trunc.w.s", FFD1 | FFS2 },
    { 0x46200001, "sub.d", FFD1 | FFS2 | FFT3 },
    { 0x46000001, "sub.s", FFD1 | FFS2 | FFT3 },
    { 0, 0, 0 },
};

/*
 * Character classes.
 */
#define ISHEX(c) (ctype[(c) & 0377] & 1)
#define ISOCTAL(c) (ctype[(c) & 0377] & 2)
#define ISDIGIT(c) (ctype[(c) & 0377] & 4)
#define ISLETTER(c) (ctype[(c) & 0377] & 8)
#define ISNONASCII(c) ((c) >= 0200)
#define ISSYMCHAR(c) (ISLETTER(c) || ISDIGIT(c) || ISNONASCII(c))

const char ctype[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 0, 0, 0, 0, 0, 0,
    8, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0, 0, 0, 0, 8,
    0, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0,
};

FILE *sfile[MAXSEGM], *rfile[MAXSEGM];
unsigned count[MAXSEGM];
int segm;
int prev_segm;
char *infile, *outfile = "a.out";
char tfilename[] = "/tmp/asXXXXXX";
int line; /* Source line number */
int xflags, Xflag, uflag;
int stlength; /* Symbol table size in bytes */
int stalign;  /* Symbol table alignment */
unsigned tbase, dbase, adbase, ctbase, dtbase, bbase;
struct nlist stab[STSIZE];
unsigned char stabsegm[STSIZE];
unsigned commalign[STSIZE];
int stabfree;
char space[STSPACE]; /* Area for symbol names */
int lastfree;           /* Free space offset */
char name[256];
unsigned intval;
int extref;
int blexflag, backlex, blextype;
short hashtab[HASHSZ], hashctab[HCMDSZ];
struct labeltab labeltab[MAXRLAB]; /* relative labels */
int nlabels;
int mode_reorder = 1;           /* .set reorder option (default) */
int mode_macro;                 /* .set macro option */
int mode_mips16;                /* .set mips16 option */
int mode_micromips;             /* .set micromips option */
int mode_at = 1;                /* .set at option */
int mode_vr4300;                /* reject opcodes unsupported by NEC VR4300 */
int optimize_level;             /* -O level; delay-slot filling needs -O2 */
int text_align_bits = DEFAULT_TEXT_ALIGN_BITS;
int reorder_full;               /* instruction buffered for reorder */
unsigned reorder_word;          /* buffered instruction... */
unsigned reorder_clobber;       /* ...modified this register */
struct reloc reorder_rel;       /* buffered relocation */
int reorder_segm;               /* section for buffered instruction */
unsigned char hilo_read_pending[MAXSEGM];
unsigned char hilo_read_gap[MAXSEGM];
struct reloc relabs = { RABS }; /* absolute relocation */
struct asm_section sections[MAXSEGM];
int maxsegm = SABS + 1;
int elf_output;

int expr_flags;      /* flags set by getexpr */
#define EXPR_GPREL 1 /* gp relative relocation */
#define EXPR_HI 2    /* %hi function */
#define EXPR_LO 4    /* %lo function */

/* Forward declarations. */
unsigned getexpr(int *s);

static void
set_cpu_vr4300(int enabled)
{
    mode_vr4300 = enabled;
    text_align_bits = enabled ? 3 : 2;
}

/*
 * Fatal error message.
 */
void uerror(char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "as: ");
    if (infile)
        fprintf(stderr, "%s, ", infile);
    if (line)
        fprintf(stderr, "%d: ", line);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

/*
 * Read a 4-byte word from the file.
 */
unsigned fgetword(FILE *f)
{
    return aout_get32(f);
}

/*
 * Write a 4-byte word to the file.
 */
void fputword(unsigned w, FILE *f)
{
    aout_put32(w, f);
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

/*
 * Write the a.out header to the file.
 */
void fputhdr(struct exec *filhdr, FILE *coutb)
{
    aout_write_exec(coutb, filhdr);
}

/*
 * Emit the nlist record for the symbol.
 */
void fputsym(struct nlist *s, FILE *file)
{
    struct nlist out;

    out = *s;
    out.n_type &= ~N_LOC;
    aout_write_sym(file, &out);
}

size_t xstrlen(const char *s)
{
    const char *p;

    p = s;
    while (*p)
        p++;
    return p - s;
}

char *xstrdup(const char *s)
{
    char *p;
    size_t len;
    size_t i;

    len = xstrlen(s) + 1;
    p = malloc(len);
    if (!p)
        uerror("out of memory");
    for (i = 0; i < len; i++)
        p[i] = s[i];
    return p;
}

void xstrcpy(char *dst, const char *src)
{
    while ((*dst++ = *src++) != 0)
        ;
}

void xstrcat(char *dst, const char *src)
{
    while (*dst)
        dst++;
    xstrcpy(dst, src);
}

unsigned
align_up(unsigned value, unsigned align)
{
    if (align <= 1)
        return value;
    return (value + align - 1) & ~(align - 1);
}

int
valid_section(int s)
{
    return s >= 0 && s < MAXSEGM && sections[s].valid;
}

int
section_has_contents(int s)
{
    return valid_section(s) && sections[s].type != SHT_NOBITS;
}

int
section_is_text(int s)
{
    return valid_section(s) && (sections[s].flags & SHF_EXECINSTR) != 0;
}

int
section_nlist_type(int s)
{
    if (!valid_section(s))
        return segmtype[s];
    return sections[s].ntype;
}

int
section_reloc_type(int s)
{
    if (!valid_section(s))
        return segmrel[s];
    return sections[s].rel;
}

int
section_from_nlist_type(int t)
{
    return typesegm[t & N_TYPE];
}

void
open_section_files(int s)
{
    if (sfile[s])
        return;
    sfile[s] = fopen(tfilename, "w+");
    if (!sfile[s])
        uerror("cannot open %s", tfilename);
    unlink(tfilename);
    rfile[s] = fopen(tfilename, "w+");
    if (!rfile[s])
        uerror("cannot open %s", tfilename);
    unlink(tfilename);
}

void
define_section(int s, const char *secname, unsigned type, unsigned flags,
    unsigned align, int ntype, int rel)
{
    sections[s].name = secname;
    sections[s].type = type;
    sections[s].flags = flags;
    sections[s].align = align;
    sections[s].ntype = ntype;
    sections[s].rel = rel;
    sections[s].valid = 1;
    sections[s].touched = 0;
    sections[s].shndx = 0;
    sections[s].symndx = 0;
    if (s >= maxsegm)
        maxsegm = s + 1;
}

void
init_sections(void)
{
    memset(sections, 0, sizeof(sections));
    maxsegm = SABS + 1;
    define_section(STEXT, ".text", SHT_PROGBITS,
        SHF_ALLOC | SHF_EXECINSTR, 1U << text_align_bits, N_TEXT, RTEXT);
    define_section(SDATA, ".data", SHT_PROGBITS,
        SHF_ALLOC | SHF_WRITE, WORDSZ, N_DATA, RDATA);
    define_section(SSTRNG, ".rodata", SHT_PROGBITS,
        SHF_ALLOC, WORDSZ, N_STRNG, RSTRNG);
    define_section(SCTORS, ".ctors", SHT_PROGBITS,
        SHF_ALLOC | SHF_WRITE, WORDSZ, N_CTORS, RCTORS);
    define_section(SDTORS, ".dtors", SHT_PROGBITS,
        SHF_ALLOC | SHF_WRITE, WORDSZ, N_DTORS, RDTORS);
    define_section(SBSS, ".bss", SHT_NOBITS,
        SHF_ALLOC | SHF_WRITE, WORDSZ, N_BSS, RBSS);
}

unsigned
default_section_flags(const char *secname)
{
    if (strncmp(secname, ".text", 5) == 0 &&
        (secname[5] == 0 || secname[5] == '.'))
        return SHF_ALLOC | SHF_EXECINSTR;
    if (strncmp(secname, ".rodata", 7) == 0 &&
        (secname[7] == 0 || secname[7] == '.'))
        return SHF_ALLOC;
    if (strncmp(secname, ".data", 5) == 0 &&
        (secname[5] == 0 || secname[5] == '.') )
        return SHF_ALLOC | SHF_WRITE;
    if (strncmp(secname, ".sdata", 6) == 0 &&
        (secname[6] == 0 || secname[6] == '.') )
        return SHF_ALLOC | SHF_WRITE;
    if (strncmp(secname, ".bss", 4) == 0 &&
        (secname[4] == 0 || secname[4] == '.') )
        return SHF_ALLOC | SHF_WRITE;
    if (strncmp(secname, ".sbss", 5) == 0 &&
        (secname[5] == 0 || secname[5] == '.') )
        return SHF_ALLOC | SHF_WRITE;
    if (strcmp(secname, ".ctors") == 0 || strcmp(secname, ".dtors") == 0 ||
        strcmp(secname, ".init_array") == 0 ||
        strcmp(secname, ".fini_array") == 0)
        return SHF_ALLOC | SHF_WRITE;
    if (strcmp(secname, ".init") == 0 || strcmp(secname, ".fini") == 0)
        return SHF_ALLOC | SHF_EXECINSTR;
    if (strcmp(secname, ".eh_frame") == 0)
        return SHF_ALLOC;
    return 0;
}

unsigned
default_section_type(const char *secname)
{
    if ((strncmp(secname, ".bss", 4) == 0 &&
         (secname[4] == 0 || secname[4] == '.')) ||
        (strncmp(secname, ".sbss", 5) == 0 &&
         (secname[5] == 0 || secname[5] == '.')))
        return SHT_NOBITS;
    return SHT_PROGBITS;
}

unsigned
default_section_align(unsigned flags)
{
    if (flags & SHF_EXECINSTR)
        return 1U << text_align_bits;
    return WORDSZ;
}

int
default_section_nlist_type(unsigned type, unsigned flags)
{
    if (type == SHT_NOBITS)
        return N_BSS;
    if (flags & SHF_EXECINSTR)
        return N_TEXT;
    if ((flags & SHF_ALLOC) && !(flags & SHF_WRITE))
        return N_STRNG;
    return N_DATA;
}

int
default_section_reloc_type(int ntype)
{
    switch (ntype) {
    case N_TEXT:
        return RTEXT;
    case N_STRNG:
        return RSTRNG;
    case N_CTORS:
        return RCTORS;
    case N_DTORS:
        return RDTORS;
    case N_BSS:
        return RBSS;
    case N_DATA:
    default:
        return RDATA;
    }
}

void
mark_section_touched(int s)
{
    if (valid_section(s))
        sections[s].touched = 1;
}

void
set_reloc_segment(struct reloc *r, int format, int segment)
{
    r->flags = format;
    r->index = 0;
    r->offset = 0;
    if (segment == SEXT) {
        r->flags |= REXT;
        r->index = extref;
    } else if (elf_output && segment != SABS) {
        r->flags |= REXT;
        r->index = SECTION_RELOC_INDEX_BASE + segment;
    } else {
        r->flags |= section_reloc_type(segment);
    }
}

/*
 * Create temporary files for allocatable output segments.
 */
void startup()
{
    register int i;

    int fd = mkstemp(tfilename);
    if (fd == -1) {
        uerror("cannot create temporary file %s", tfilename);
    } else {
        close(fd);
    }
    init_sections();
    for (i = STEXT; i <= SBSS; i++) {
        open_section_files(i);
    }
    line = 1;
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

void hashinit()
{
    register int i, h;
    register const struct optable *p;

    for (i = 0; i < HCMDSZ; i++)
        hashctab[i] = -1;
    for (i = 0, p = optable; p->name; i++, p++) {
        h = hash_rot13(p->name) & (HCMDSZ - 1);
        while (hashctab[h] != -1)
            if (--h < 0)
                h += HCMDSZ;
        hashctab[h] = i;
    }
    for (i = 0; i < HASHSZ; i++)
        hashtab[i] = -1;
}

int hexdig(int c)
{
    if (c <= '9')
        return (c - '0');
    else if (c <= 'F')
        return (c - 'A' + 10);
    else
        return (c - 'a' + 10);
}

/*
 * Get hexadecimal number 0xZZZ
 */
void gethnum()
{
    register int c;
    register char *cp;

    c = getchar();
    for (cp = name; ISHEX(c); c = getchar())
        *cp++ = hexdig(c);
    while (c == 'u' || c == 'U' || c == 'l' || c == 'L')
        c = getchar();
    ungetc(c, stdin);
    intval = 0;
    for (c = 0; c < 32; c += 4) {
        if (--cp < name)
            return;
        intval |= *cp << c;
    }
}

/*
 * Get a number.
 * 1234 - decimal
 * 01234 - octal
 */
void getnum(int c)
{
    register char *cp;
    int leadingzero;

    leadingzero = (c == '0');
    for (cp = name; ISDIGIT(c); c = getchar())
        *cp++ = hexdig(c);
    while (c == 'u' || c == 'U' || c == 'l' || c == 'L')
        c = getchar();
    ungetc(c, stdin);
    intval = 0;
    if (leadingzero) {
        /* Octal. */
        for (c = 0; c <= 27; c += 3) {
            if (--cp < name)
                return;
            intval |= *cp << c;
        }
        if (--cp < name)
            return;
        intval |= *cp << 30;
        return;
    }
    /* Decimal. */
    for (c = 1;; c *= 10) {
        if (--cp < name)
            return;
        intval += *cp * c;
    }
}

void getname(int c)
{
    register char *cp;

    for (cp = name; ISSYMCHAR(c); c = getchar())
        *cp++ = c;
    *cp = 0;
    ungetc(c, stdin);
}

int looktype()
{
    switch (name[1]) {
    case 'c':
        if (!strcmp("@common", name))
            return (LSYMTYPE);
        break;
    case 'f':
        if (!strcmp("@fini_array", name))
            return (LSECTYPE);
        if (!strcmp("@function", name))
            return (LSYMTYPE);
        break;
    case 'g':
        if (!strcmp("@gnu_indirect_function", name))
            return (LSYMTYPE);
        if (!strcmp("@gnu_unique_object", name))
            return (LSYMTYPE);
        break;
    case 'i':
        if (!strcmp("@init_array", name))
            return (LSECTYPE);
        break;
    case 'n':
        if (!strcmp("@nobits", name))
            return (LSECTYPE);
        if (!strcmp("@note", name))
            return (LSECTYPE);
        if (!strcmp("@notype", name))
            return (LSYMTYPE);
        break;
    case 'o':
        if (!strcmp("@object", name))
            return (LSYMTYPE);
        break;
    case 'p':
        if (!strcmp("@progbits", name))
            return (LSECTYPE);
        if (!strcmp("@preinit_array", name))
            return (LSECTYPE);
        break;
    case 't':
        if (!strcmp("@tls_object", name))
            return (LSYMTYPE);
        break;
    }
    return (-1);
}

int lookacmd()
{
    switch (name[1]) {
    case 'a':
        if (!strcmp(".ascii", name))
            return (LASCII);
        if (!strcmp(".align", name))
            return (LALIGN);
        break;
    case 'b':
        if (!strcmp(".bss", name))
            return (LBSS);
        if (!strcmp(".byte", name))
            return (LBYTE);
        break;
    case 'c':
        if (!strcmp(".comm", name))
            return (LCOMM);
        break;
    case 'd':
        if (!strcmp(".data", name))
            return (LDATA);
        break;
    case 'e':
        if (!strcmp(".equ", name))
            return (LEQU);
        if (!strcmp(".end", name))
            return (LEND);
        if (!strcmp(".ent", name))
            return (LENT);
        if (!strcmp(".extern", name))
            return (LEXTERN);
        break;
    case 'f':
        if (!strcmp(".file", name))
            return (LFILE);
        if (!strcmp(".fmask", name))
            return (LFMASK);
        if (!strcmp(".frame", name))
            return (LFRAME);
        break;
    case 'g':
        if (!strcmp(".globl", name))
            return (LGLOBL);
        if (!strcmp(".gnu_attribute", name))
            return (LGNUATTR);
        break;
    case 'h':
        if (!strcmp(".half", name))
            return (LHALF);
        break;
    case 'i':
        if (!strcmp(".ident", name))
            return (LIDENT);
        break;
    case 'l':
        if (!strcmp(".lcomm", name))
            return (LLCOMM);
        if (!strcmp(".local", name))
            return (LLOCAL);
        if (!strcmp(".long", name))
            return (LWORD);
        break;
    case 'm':
        if (!strcmp(".mask", name))
            return (LMASK);
        if (!strcmp(".module", name))
            return (LMODULE);
        break;
    case 'n':
        if (!strcmp(".nan", name))
            return (LNAN);
        break;
    case 'p':
        if (!strcmp(".p2align", name))
            return (LALIGN);
        if (!strcmp(".previous", name))
            return (LPREVIOUS);
        break;
    case 'r':
        if (!strcmp(".rdata", name))
            return (LRDATA);
        break;
    case 's':
        if (!strcmp(".section", name))
            return (LSECTION);
        if (!strcmp(".set", name))
            return (LSET);
        if (!strcmp(".size", name))
            return (LSIZE);
        if (!strcmp(".space", name))
            return (LSPACE);
        if (!strcmp(".strng", name))
            return (LSTRNG);
        break;
    case 't':
        if (!strcmp(".text", name))
            return (LTEXT);
        if (!strcmp(".type", name))
            return (LTYPE);
        break;
    case 'w':
        if (!strcmp(".word", name))
            return (LWORD);
        if (!strcmp(".weak", name))
            return (LWEAK);
        break;
    case 'z':
        if (!strcmp(".zero", name))
            return (LSPACE);
        break;
    }
    return (-1);
}

void switchsection(int newsegm)
{
    if (!valid_section(newsegm))
        uerror("bad section");
    if (newsegm == segm) {
        prev_segm = segm;
        mark_section_touched(segm);
        return;
    }
    reorder_flush();
    prev_segm = segm;
    segm = newsegm;
    open_section_files(segm);
    mark_section_touched(segm);
}

void previoussection()
{
    int newsegm;

    reorder_flush();
    newsegm = prev_segm;
    if (!valid_section(newsegm))
        uerror("bad previous section");
    prev_segm = segm;
    segm = newsegm;
    open_section_files(segm);
    mark_section_touched(segm);
}

void
read_quoted_string(char *buf, size_t len)
{
    int c;
    char *p, *end;

    p = buf;
    end = buf + len - 1;
    for (;;) {
        c = getchar();
        switch (c) {
        case EOF:
            uerror("EOF in string");
        case '"':
            *p = 0;
            return;
        case '\\':
            c = getchar();
            if (c == EOF)
                uerror("EOF in string");
            break;
        }
        if (p < end)
            *p++ = c;
    }
}

unsigned
section_flags_from_string(const char *flags)
{
    unsigned value;

    value = 0;
    for (; *flags; flags++) {
        switch (*flags) {
        case 'a':
            value |= SHF_ALLOC;
            break;
        case 'w':
            value |= SHF_WRITE;
            break;
        case 'x':
            value |= SHF_EXECINSTR;
            break;
        case 'M':
        case 'S':
        case 'G':
        case 'T':
        case 'e':
        case 'o':
            /* Accepted for common ELF .section syntax; unsupported flags are
             * ignored because this assembler does not implement their
             * associated metadata fields. */
            break;
        default:
            uerror("bad .section flags");
        }
    }
    return value;
}

unsigned
section_type_from_name(const char *type_name)
{
    if (strcmp(type_name, "@progbits") == 0)
        return SHT_PROGBITS;
    if (strcmp(type_name, "@nobits") == 0)
        return SHT_NOBITS;
    if (strcmp(type_name, "@note") == 0)
        return SHT_PROGBITS;
    if (strcmp(type_name, "@init_array") == 0 ||
        strcmp(type_name, "@fini_array") == 0 ||
        strcmp(type_name, "@preinit_array") == 0)
        return SHT_PROGBITS;
    uerror("bad type of .section");
    return SHT_PROGBITS;
}

int
find_or_create_elf_section(const char *secname, unsigned type, unsigned flags)
{
    int s, ntype, rel;

    for (s = 0; s < maxsegm; s++) {
        if (!valid_section(s))
            continue;
        if (strcmp(sections[s].name, secname) == 0 &&
            sections[s].type == type && sections[s].flags == flags)
            return s;
    }
    for (s = SABS + 1; s < MAXSEGM; s++) {
        if (!valid_section(s))
            break;
    }
    if (s >= MAXSEGM)
        uerror("too many sections");
    ntype = default_section_nlist_type(type, flags);
    rel = default_section_reloc_type(ntype);
    define_section(s, xstrdup(secname), type, flags,
        default_section_align(flags), ntype, rel);
    open_section_files(s);
    return s;
}

int
section_from_name_aout(const char *secname)
{
    struct {
        const char *name;
        int len;
        int segm;
    } const *p, map[] = {
        { ".text", 5, STEXT },    { ".data", 5, SDATA },
        { ".sdata", 6, SDATA },   { ".rodata", 7, SSTRNG },
        { ".bss", 4, SBSS },      { ".sbss", 5, SBSS },
        { ".init", 5, STEXT },     { ".fini", 5, STEXT },
        { ".ctors", 6, SCTORS },   { ".dtors", 6, SDTORS },
        { ".init_array", 11, SCTORS }, { ".fini_array", 11, SDTORS },
        { ".eh_frame", 9, SSTRNG }, { ".mdebug", 7, SSTRNG }, { 0 },
    };

    for (p = map; p->name; p++) {
        if (strncmp(secname, p->name, p->len) == 0 &&
            (secname[p->len] == 0 || secname[p->len] == '.')) {
            return p->segm;
        }
    }
    uerror("bad .section name");
    return STEXT;
}

/*
 * Change a segment based on a section name.
 */
void setsection(const char *secname, unsigned flags, int has_flags,
    unsigned type, int has_type)
{
    int newsegm;

    if (!elf_output) {
        switchsection(section_from_name_aout(secname));
        return;
    }
    if (!has_flags)
        flags = default_section_flags(secname);
    if (!has_type)
        type = default_section_type(secname);
    newsegm = find_or_create_elf_section(secname, type, flags);
    switchsection(newsegm);
}

int lookreg()
{
    int val;
    char *cp;

    switch (name[1]) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        val = 0;
        for (cp = name + 1; ISDIGIT(*cp); cp++) {
            val *= 10;
            val += *cp - '0';
        }
        if (*cp != 0)
            break;
        return val;
    case 'a':
        if (name[3] == 0) {
            switch (name[2]) {
            case '0':
                return 4; /* $a0 */
            case '1':
                return 5; /* $a1 */
            case '2':
                return 6; /* $a2 */
            case '3':
                return 7; /* $a3 */
            case 't':
                return 1; /* $at */
            }
        }
        break;
    case 'f':
        if (name[3] == 0 && name[2] == 'p')
            return 30; /* $fp */
        break;
    case 'g':
        if (name[3] == 0 && name[2] == 'p')
            return 28; /* $gp */
        break;
    case 'k':
        if (name[3] == 0) {
            switch (name[2]) {
            case '0':
                return 26; /* $k0 */
            case '1':
                return 27; /* $k1 */
            }
        }
        break;
    case 'r':
        if (name[3] == 0 && name[2] == 'a')
            return 31; /* $ra */
        break;
    case 's':
        if (name[3] == 0) {
            switch (name[2]) {
            case '0':
                return 16; /* $s0 */
            case '1':
                return 17; /* $s1 */
            case '2':
                return 18; /* $s2 */
            case '3':
                return 19; /* $s3 */
            case '4':
                return 20; /* $s4 */
            case '5':
                return 21; /* $s5 */
            case '6':
                return 22; /* $s6 */
            case '7':
                return 23; /* $s7 */
            case '8':
                return 30; /* $s8 */
            case 'p':
                return 29; /* $sp */
            }
        }
        break;
    case 't':
        if (name[3] == 0) {
            switch (name[2]) {
            case '0':
                return 8; /* $t0 */
            case '1':
                return 9; /* $t1 */
            case '2':
                return 10; /* $t2 */
            case '3':
                return 11; /* $t3 */
            case '4':
                return 12; /* $t4 */
            case '5':
                return 13; /* $t5 */
            case '6':
                return 14; /* $t6 */
            case '7':
                return 15; /* $t7 */
            case '8':
                return 24; /* $t8 */
            case '9':
                return 25; /* $t9 */
            }
        }
        break;
    case 'v':
        if (name[3] == 0) {
            switch (name[2]) {
            case '0':
                return 2; /* $v0 */
            case '1':
                return 3; /* $v1 */
            }
        }
        break;
    case 'z':
        if (!strcmp(name + 2, "ero"))
            return 0; /* $zero */
        break;
    }
    return -1;
}

int lookfreg()
{
    int val;
    char *cp;

    if (name[1] != 'f' || !ISDIGIT(name[2]))
        return -1;
    val = 0;
    for (cp = name + 2; ISDIGIT(*cp); cp++) {
        val *= 10;
        val += *cp - '0';
    }
    if (*cp != 0 || val > 31)
        return -1;
    return val;
}

int lookfcc()
{
    int val;
    char *cp;

    if (strncmp(name, "$fcc", 4) != 0 || !ISDIGIT(name[4]))
        return -1;
    val = 0;
    for (cp = name + 4; ISDIGIT(*cp); cp++) {
        val *= 10;
        val += *cp - '0';
    }
    if (*cp != 0 || val > 7)
        return -1;
    return val;
}

int lookcmd()
{
    register int i, h;

    h = hash_rot13(name) & (HCMDSZ - 1);
    while ((i = hashctab[h]) != -1) {
        if (!strcmp(optable[i].name, name))
            return (i);
        if (--h < 0)
            h += HCMDSZ;
    }
    return (-1);
}

int getgpr(int lex, int *reg)
{
    if (lex == LREG)
        return 1;
    if (lex == LNUM && intval == 0) {
        *reg = 0;
        return 1;
    }
    return 0;
}

int has_empty_mem_offset(void)
{
    int c, next;

    do {
        c = getchar();
    } while (c == ' ' || c == '\t');
    if (c != '(') {
        if (c != EOF)
            ungetc(c, stdin);
        return 0;
    }

    do {
        next = getchar();
    } while (next == ' ' || next == '\t');
    if (next != EOF)
        ungetc(next, stdin);
    ungetc(c, stdin);

    return next == '$';
}

char *alloc(int len)
{
    register int r;

    r = lastfree;
    lastfree += len;
    if (lastfree > sizeof(space))
        uerror("out of memory");
    return (space + r);
}

int lookname()
{
    register int i, h;

    /* Search for symbol name. */
    h = hash_rot13(name) & (HASHSZ - 1);
    while ((i = hashtab[h]) != -1) {
        if (!strcmp(stab[i].n_name, name))
            return (i);
        if (--h < 0)
            h += HASHSZ;
    }

    /* Add a new symbol to table. */
    if ((i = stabfree++) >= STSIZE)
        uerror("symbol table overflow");
    stab[i].n_len = xstrlen(name);
    stab[i].n_name = alloc(1 + stab[i].n_len);
    xstrcpy(stab[i].n_name, name);
    stab[i].n_value = 0;
    stab[i].n_type = N_UNDF;
    stabsegm[i] = SEXT;
    commalign[i] = WORDSZ;
    hashtab[h] = i;
    return (i);
}

/*
 * Read a lexical element, return it's type and store a value into *val.
 * Returned type codes:
 * LEOL    - End of line.  Value is a line number.
 * LEOF    - End of file.
 * LNUM    - Integer value (into intval), *val undefined.
 * LNAME   - Identifier.  String value is in name[] array.
 * LREG    - Machine register.  Value is a register number.
 * LLSHIFT - << operator.
 * LRSHIFT - >> operator.
 * LASCII  - .ascii assembler instruction.
 * LBSS    - .bss assembler instruction.
 * LCOMM   - .comm assembler instruction.
 * LDATA   - .data assembler instruction.
 * LGLOBL  - .globl assembler instruction.
 * LHALF   - .half assembler instruction.
 * LSTRNG  - .strng assembler instruction.
 * LTEXT   - .text assembler instruction.
 * LEQU    - .equ assembler instruction.
 * LWORD   - .word assembler instruction.
 * LFILE   - .file assembler instruction.
 * LSECTION - .section assembler instruction.
 */
int getlex(int *pval)
{
    register int c;

    if (blexflag) {
        blexflag = 0;
        *pval = blextype;
        return (backlex);
    }
    for (;;) {
        switch (c = getchar()) {
        case '#':
        skiptoeol:
            while ((c = getchar()) != '\n')
                if (c == EOF)
                    return (LEOF);
        case '\n':
            ++line;
            c = getchar();
            if (c == '#')
                goto skiptoeol;
            ungetc(c, stdin);
        case ';':
            *pval = line;
            return (LEOL);
        case ' ':
        case '\t':
            continue;
        case EOF:
            return (LEOF);
        case '\\':
            c = getchar();
            if (c == '<')
                return (LLSHIFT);
            if (c == '>')
                return (LRSHIFT);
            ungetc(c, stdin);
            return ('\\');
        case '\'':
        case '%':
        case '^':
        case '&':
        case '|':
        case '~':
        case '+':
        case '-':
        case '*':
        case '/':
        case '"':
        case ',':
        case '[':
        case ']':
        case '(':
        case ')':
        case '{':
        case '}':
        case '<':
        case '>':
        case '=':
        case ':':
            return (c);
        case '0':
            if ((c = getchar()) == 'x' || c == 'X') {
                gethnum();
                return (LNUM);
            }
            ungetc(c, stdin);
            c = '0';
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            getnum(c);
            return (LNUM);
        default:
            if (!ISLETTER(c) && !ISNONASCII(c))
                uerror("bad character: \\%o", c & 0377);
            getname(c);
            if (name[0] == '.') {
                if (name[1] == 0)
                    return ('.');
                *pval = lookacmd();
                if (*pval != -1)
                    return (*pval);
            }
            if (name[0] == '$') {
                *pval = lookfcc();
                if (*pval != -1)
                    return (LFCC);
                *pval = lookfreg();
                if (*pval != -1)
                    return (LFREG);
                *pval = lookreg();
                if (*pval != -1)
                    return (LREG);
            }
            if (name[0] == '@') {
                *pval = looktype();
                if (*pval != -1)
                    return (*pval);
            }
            return (LNAME);
        }
    }
}

void ungetlex(int val, int type)
{
    blexflag = 1;
    backlex = val;
    blextype = type;
}

int getterm()
{
    register int ty;
    int cval, s;

    switch (getlex(&cval)) {
    default:
        uerror("operand missed");
    case '+':
        return getterm();
    case '-':
        s = getterm();
        if (s != SABS)
            uerror("too complex expression");
        intval = -intval;
        return SABS;
    case '~':
        s = getterm();
        if (s != SABS)
            uerror("too complex expression");
        intval = ~intval;
        return SABS;
    case LNUM:
        cval = getchar();
        if (cval == 'b' || cval == 'B')
            extref = RLAB_OFFSET - intval;
        else if (cval == 'f' || cval == 'F')
            extref = RLAB_OFFSET + intval;
        else {
            /* Integer literal. */
            ungetc(cval, stdin);
            return (SABS);
        }
        /* Local label. */
        if (intval >= RLAB_MAXVAL)
            uerror("too large relative label");
        intval = 0;
        return (SEXT);
    case LNAME:
        intval = 0;
        cval = lookname();
        ty = stab[cval].n_type & N_TYPE;
        if (ty == N_UNDF || ty == N_COMM) {
            extref = cval;
            return (SEXT);
        }
        intval = stab[cval].n_value;
        if (elf_output && ty != N_ABS)
            return stabsegm[cval];
        return (section_from_nlist_type(ty));
    case '.':
        intval = count[segm];
        return (segm);
    case '%':
        if (getlex(&cval) != LNAME)
            uerror("bad %function name");
        if (strcmp(name, "gp_rel") == 0) {
            /* GP relative reverence. */
            expr_flags |= EXPR_GPREL;
        } else if (strcmp(name, "hi") == 0) {
            expr_flags |= EXPR_HI;
        } else if (strcmp(name, "lo") == 0) {
            expr_flags |= EXPR_LO;
        } else
            uerror("unknown function %s", name);
        if (getlex(&cval) != '(')
            uerror("bad %s syntax", name);
        /* fall through */
    case '(':
        getexpr(&s);
        if (getlex(&cval) != ')')
            uerror("bad () syntax");
        return (s);
    }
}

int
expr_start(int ty)
{
    return ty == LNUM || ty == LNAME || ty == '.' || ty == '(' ||
        ty == '%' || ty == '+' || ty == '-' || ty == '~';
}

void
expr_abs_only(int s1, int s2)
{
    if (s1 != SABS || s2 != SABS)
        uerror("too complex expression");
}

void
expr_add_segment(int *s, int s2)
{
    if (*s == SABS)
        *s = s2;
    else if (s2 != SABS)
        uerror("too complex expression");
}

void
expr_sub_segment(int *s, int s2)
{
    if (s2 == *s && s2 != SEXT)
        *s = SABS;
    else if (s2 != SABS)
        uerror("too complex expression");
}

unsigned
getmul(int *s)
{
    int clex, cval, s2;
    unsigned rez;

    *s = getterm();
    rez = intval;
    for (;;) {
        clex = getlex(&cval);
        switch (clex) {
        case '*':
            s2 = getterm();
            expr_abs_only(*s, s2);
            rez *= intval;
            break;
        case '/':
            s2 = getterm();
            expr_abs_only(*s, s2);
            if (intval == 0)
                uerror("division by zero");
            rez /= intval;
            break;
        default:
            ungetlex(clex, cval);
            intval = rez;
            return rez;
        }
    }
}

unsigned
getadd(int *s)
{
    int clex, cval, s2;
    unsigned rez;

    rez = getmul(s);
    for (;;) {
        clex = getlex(&cval);
        switch (clex) {
        case '+':
            getmul(&s2);
            expr_add_segment(s, s2);
            rez += intval;
            break;
        case '-':
            getmul(&s2);
            expr_sub_segment(s, s2);
            rez -= intval;
            break;
        default:
            ungetlex(clex, cval);
            intval = rez;
            return rez;
        }
    }
}

unsigned
getshift(int *s)
{
    int clex, cval, s2;
    unsigned rez;

    rez = getadd(s);
    for (;;) {
        clex = getlex(&cval);
        switch (clex) {
        case LLSHIFT:
            getadd(&s2);
            expr_abs_only(*s, s2);
            rez <<= intval & 037;
            break;
        case LRSHIFT:
            getadd(&s2);
            expr_abs_only(*s, s2);
            rez >>= intval & 037;
            break;
        default:
            ungetlex(clex, cval);
            intval = rez;
            return rez;
        }
    }
}

unsigned
getand(int *s)
{
    int clex, cval, s2;
    unsigned rez;

    rez = getshift(s);
    for (;;) {
        clex = getlex(&cval);
        if (clex != '&') {
            ungetlex(clex, cval);
            intval = rez;
            return rez;
        }
        getshift(&s2);
        expr_abs_only(*s, s2);
        rez &= intval;
    }
}

unsigned
getxor(int *s)
{
    int clex, cval, s2;
    unsigned rez;

    rez = getand(s);
    for (;;) {
        clex = getlex(&cval);
        if (clex != '^') {
            ungetlex(clex, cval);
            intval = rez;
            return rez;
        }
        getand(&s2);
        expr_abs_only(*s, s2);
        rez ^= intval;
    }
}

unsigned
getor(int *s)
{
    int clex, cval, s2;
    unsigned rez;

    rez = getxor(s);
    for (;;) {
        clex = getlex(&cval);
        if (clex != '|') {
            ungetlex(clex, cval);
            intval = rez;
            return rez;
        }
        getxor(&s2);
        expr_abs_only(*s, s2);
        rez |= intval;
    }
}

/*
 * Get an expression.
 * Return a value, put a base segment id to *s.
 * A copy of value is saved in intval.
 *
 * expression = [term] {op term}..., with C-like operator precedence.
 * term       = LNAME | LNUM | "." | "(" expression ")"
 * op         = "+" | "-" | "&" | "|" | "^" | "~" | "<<" | ">>" | "/" | "*"
 */
unsigned getexpr(int *s)
{
    register int clex;
    int cval;

    /* look a first lexeme */
    clex = getlex(&cval);
    if (!expr_start(clex)) {
        ungetlex(clex, cval);
        *s = SABS;
        intval = 0;
        return 0;
    }
    ungetlex(clex, cval);
    return getor(s);
    /* NOTREACHED */
}

static int
is_hilo_read(unsigned w)
{
    unsigned funct;

    if ((w & 0xffff07ff) != 0x00000010 &&
        (w & 0xffff07ff) != 0x00000012)
        return 0;
    funct = w & 0x3f;
    return funct == 0x10 || funct == 0x12; /* mfhi, mflo */
}

static int
is_hilo_write(unsigned w)
{
    unsigned funct;

    if ((w & 0xfc00ffc0) != 0)
        return 0;
    funct = w & 0x3f;
    return funct >= 0x18 && funct <= 0x1f; /* mult[u], div[u], d* */
}

/*
 * Decode the deliberately small instruction class that this assembler may
 * move into a delay slot.  Memory, coprocessor, HI/LO, control-transfer and
 * trapping instructions are excluded rather than guessed about.  This is a
 * local implementation of the safety rules used by mature MIPS assemblers,
 * not a general instruction scheduler.
 */
static int
delay_candidate_gpr(unsigned w, unsigned *read_mask, unsigned *write_mask)
{
    unsigned op, funct, rs, rt, rd;

    op = w >> 26;
    rs = (w >> 21) & 31;
    rt = (w >> 16) & 31;
    rd = (w >> 11) & 31;
    *read_mask = 0;
    *write_mask = 0;

    if (op == 0) {
        funct = w & 0x3f;
        switch (funct) {
        case 0x00: /* sll */
        case 0x02: /* srl */
        case 0x03: /* sra */
        case 0x38: /* dsll */
        case 0x3a: /* dsrl */
        case 0x3b: /* dsra */
        case 0x3c: /* dsll32 */
        case 0x3e: /* dsrl32 */
        case 0x3f: /* dsra32 */
            if (w == 0)
                return 0; /* Moving a NOP has no benefit. */
            *read_mask = 1U << rt;
            *write_mask = 1U << rd;
            return 1;
        case 0x04: /* sllv */
        case 0x06: /* srlv */
        case 0x07: /* srav */
        case 0x14: /* dsllv */
        case 0x16: /* dsrlv */
        case 0x17: /* dsrav */
            *read_mask = (1U << rs) | (1U << rt);
            *write_mask = 1U << rd;
            return 1;
        case 0x21: /* addu */
        case 0x23: /* subu */
        case 0x24: /* and */
        case 0x25: /* or */
        case 0x26: /* xor */
        case 0x27: /* nor */
        case 0x2a: /* slt */
        case 0x2b: /* sltu */
        case 0x2d: /* daddu */
        case 0x2f: /* dsubu */
            *read_mask = (1U << rs) | (1U << rt);
            *write_mask = 1U << rd;
            return 1;
        default:
            return 0;
        }
    }

    switch (op) {
    case 0x09: /* addiu */
    case 0x0a: /* slti */
    case 0x0b: /* sltiu */
    case 0x0c: /* andi */
    case 0x0d: /* ori */
    case 0x0e: /* xori */
    case 0x19: /* daddiu */
        *read_mask = 1U << rs;
        *write_mask = 1U << rt;
        return 1;
    case 0x0f: /* lui */
        *write_mask = 1U << rt;
        return 1;
    default:
        return 0;
    }
}

/*
 * Return the GPRs read before a non-linking, non-likely control transfer
 * reaches its delay slot.  Calls and coprocessor branches stay conservative.
 */
static int
delay_branch_gpr(unsigned w, unsigned *read_mask)
{
    unsigned op, rs, rt;

    op = w >> 26;
    rs = (w >> 21) & 31;
    rt = (w >> 16) & 31;
    *read_mask = 0;

    switch (op) {
    case 0x01: /* REGIMM */
        if (rt != 0 && rt != 1) /* reject likely and link forms */
            return 0;
        *read_mask = 1U << rs;
        return 1;
    case 0x02: /* j, but not jal */
        return 1;
    case 0x04: /* beq */
    case 0x05: /* bne */
        *read_mask = (1U << rs) | (1U << rt);
        return 1;
    case 0x06: /* blez */
    case 0x07: /* bgtz */
        *read_mask = 1U << rs;
        return 1;
    case 0x00: /* jr, but not jalr */
        if ((w & 0x3f) != 0x08)
            return 0;
        *read_mask = 1U << rs;
        return 1;
    default:
        return 0;
    }
}

static int
can_fill_delay_slot(unsigned branch)
{
    unsigned candidate_read, candidate_write, branch_read;

    if (optimize_level < 2 || !reorder_full || reorder_segm != segm)
        return 0;
    if (reorder_rel.flags != RABS)
        return 0;
    if (!delay_candidate_gpr(reorder_word, &candidate_read, &candidate_write))
        return 0;
    if (!delay_branch_gpr(branch, &branch_read))
        return 0;

    /* The branch must observe the value produced before it in source order. */
    if (branch_read & candidate_write)
        return 0;
    return 1;
}

static void
write_instruction(unsigned w, struct reloc *r, int s)
{
    fputword(w, sfile[s]);
    fputrel(r, rfile[s]);

    if (!mode_vr4300 || !section_is_text(s))
        return;
    if (is_hilo_read(w)) {
        hilo_read_pending[s] = 1;
        hilo_read_gap[s] = 0;
    } else if (hilo_read_pending[s]) {
        if (hilo_read_gap[s] < 2)
            hilo_read_gap[s]++;
        if (hilo_read_gap[s] >= 2)
            hilo_read_pending[s] = 0;
    }
}

/*
 * VR4300 requires two complete instructions between mfhi/mflo and a later
 * instruction that overwrites HI/LO.  GNU as enforces this even inside
 * .set noreorder blocks: noreorder controls instruction scheduling and delay
 * slots, not architecturally required hazard padding.
 */
static void
pad_hilo_write(unsigned w, int s)
{
    if (!mode_vr4300 || !section_is_text(s) || !is_hilo_write(w))
        return;
    while (hilo_read_pending[s] && hilo_read_gap[s] < 2) {
        write_instruction(0, &relabs, s);
        count[s] += WORDSZ;
    }
}

void reorder_flush()
{
    if (reorder_full) {
        write_instruction(reorder_word, &reorder_rel, reorder_segm);
        reorder_full = 0;
    }
}

/*
 * Default emit function.
 */
void emitword(unsigned w, struct reloc *r, int clobber_reg)
{
    if (mode_reorder && section_is_text(segm)) {
        reorder_flush();
        pad_hilo_write(w, segm);
        reorder_word = w;
        reorder_rel = *r;
        reorder_segm = segm;
        reorder_full = 1;
        reorder_clobber = clobber_reg & 31;
    } else {
        pad_hilo_write(w, segm);
        write_instruction(w, r, segm);
    }
    count[segm] += WORDSZ;
}

/*
 * Load an absolute immediate value into a register.
 */
void emit_abs_load(unsigned opcode, struct reloc *relinfo, unsigned value)
{
    int reg;

    reg = opcode >> 16;
    relinfo->flags = RABS;
    if (value <= 0xffff) {
        /* ori d, $zero, value */
        opcode |= 0x34000000 | value;
    } else if (value >= -0x8000) {
        /* addiu d, $zero, value */
        opcode |= 0x24000000 | (value & 0xffff);
    } else if ((value & 0xffff) == 0) {
        /* lui d, value[31:16] */
        opcode |= 0x3c000000 | (value >> 16);
    } else {
        /* lui d, value[31:16]
         * ori d, d, value[15:0]) */
        emitword(opcode | 0x3c000000 | (value >> 16), &relabs, reg);
        opcode |= 0x34000000 | (opcode & 0x1f0000) << 5 | (value & 0xffff);
    }
    emitword(opcode, relinfo, reg);
}

int
fits_signed16(unsigned value)
{
    int svalue = (int)value;

    return svalue >= -0x8000 && svalue <= 0x7fff;
}

/*
 * LI pseudo instruction.
 */
void emit_li(unsigned opcode, struct reloc *relinfo)
{
    register unsigned value;
    int cval, segment;

    if (getlex(&cval) != ',')
        uerror("comma expected");
    value = getexpr(&segment);
    if (segment != SABS)
        uerror("absolute value required");
    emit_abs_load(opcode, relinfo, value);
}

/*
 * LA pseudo instruction.
 */
void emit_la(unsigned opcode, struct reloc *relinfo)
{
    register unsigned value, hi;
    int cval, segment;

    if (getlex(&cval) != ',')
        uerror("comma expected");
    expr_flags = 0;
    value = getexpr(&segment);
    if (segment == SABS) {
        emit_abs_load(opcode, relinfo, value);
        return;
    }
    set_reloc_segment(relinfo, 0, segment);
    if (expr_flags & EXPR_GPREL)
        relinfo->flags |= RGPREL;

    /* lui d, %hi(value)
     * addiu d, d, %lo(value) */
    relinfo->flags |= RHIGH16S;
    relinfo->offset = value & 0xffff;
    hi = (value + 0x8000) >> 16;
    emitword(opcode | 0x3c000000 | hi, relinfo, hi);

    relinfo->flags &= ~RHIGH16S;
    opcode |= 0x24000000 | (opcode & 0x1f0000) << 5 | (value & 0xffff);
    emitword(opcode, relinfo, value >> 16);
}

/*
 * Memory pseudo instruction: load/store reg,symbol.
 */
void emit_mem_pseudo(unsigned opcode, unsigned value, struct reloc *relinfo, int clobber_reg)
{
    struct reloc hirel, lorel;
    unsigned hi;

    if (!mode_at)
        uerror("macro requires $at");

    hirel = *relinfo;
    lorel = *relinfo;

    hirel.flags &= ~RFMASK;
    hirel.flags |= RHIGH16S;
    hirel.offset = value & 0xffff;
    hi = (value + 0x8000) >> 16;
    emitword(0x3c010000 | hi, &hirel, 1);

    lorel.flags &= ~RFMASK;
    opcode &= ~(31 << 21);
    opcode |= (1 << 21) | (value & 0xffff);
    emitword(opcode, &lorel, clobber_reg);
}

/*
 * Memory pseudo instruction: load/store reg,offset(base).
 */
void emit_mem_base_pseudo(unsigned opcode, unsigned value, struct reloc *relinfo,
    int base_reg, int clobber_reg, int type)
{
    struct reloc hirel, lorel;
    unsigned hi;

    if (!mode_at)
        uerror("macro requires $at");
    if (base_reg == 1)
        uerror("macro requires non-$at base register");
    if ((type & FRT1) && ((opcode >> 16) & 31) == 1)
        uerror("macro requires non-$at target register");

    hirel = *relinfo;
    lorel = *relinfo;

    if (relinfo->flags == RABS) {
        emit_abs_load(1 << 16, &relabs, value);
    } else {
        hirel.flags &= ~RFMASK;
        hirel.flags |= RHIGH16S;
        hirel.offset = value & 0xffff;
        hi = (value + 0x8000) >> 16;
        emitword(0x3c010000 | hi, &hirel, 1); /* lui $at,%hi(value) */

        lorel.flags &= ~RFMASK;
        emitword(0x24210000 | (value & 0xffff), &lorel, 1); /* addiu $at,$at,%lo(value) */
    }

    /* addu $at,$at,base */
    emitword(0x00000021 | (1 << 21) | (base_reg << 16) | (1 << 11),
        &relabs, 1);

    opcode &= ~((31 << 21) | 0xffff);
    opcode |= 1 << 21; /* 0($at) */
    emitword(opcode, &relabs, clobber_reg);
}

/*
 * GAS accepts "mul rd,rs,rt" for VR4300 as a macro.
 * The real MIPS32 opcode is not present on VR4300.
 */
void emit_mul(unsigned opcode, struct reloc *relinfo)
{
    unsigned rd = (opcode >> 11) & 31;
    unsigned rt = (opcode >> 16) & 31;
    unsigned rs = (opcode >> 21) & 31;

    (void)relinfo;
    emitword(0x00000019 | (rs << 21) | (rt << 16), &relabs, 0); /* multu rs,rt */
    emitword(0x00000012 | (rd << 11), &relabs, rd);             /* mflo rd */
}

static int
get_fpreg_operand(const char *name)
{
    int clex, cval;

    clex = getlex(&cval);
    if (clex != LFREG)
        uerror("bad %s register", name);
    return cval;
}

static void
need_comma(void)
{
    int cval;

    if (getlex(&cval) != ',')
        uerror("comma expected");
}

void
emit_fp4(unsigned opcode, struct reloc *relinfo)
{
    int fd, fr, fs, ft;

    fd = get_fpreg_operand("fd");
    need_comma();
    fr = get_fpreg_operand("fr");
    need_comma();
    fs = get_fpreg_operand("fs");
    need_comma();
    ft = get_fpreg_operand("ft");

    opcode |= fr << 21;
    opcode |= ft << 16;
    opcode |= fs << 11;
    opcode |= fd << 6;
    emitword(opcode, relinfo, fd);
}

void
emit_fpidxmem(unsigned opcode, struct reloc *relinfo)
{
    int ft, index, base, clex, cval;

    ft = get_fpreg_operand("ft");
    need_comma();
    clex = getlex(&cval);
    if (!getgpr(clex, &cval))
        uerror("bad index register");
    index = cval;
    if (getlex(&cval) != '(')
        uerror("left par expected");
    clex = getlex(&cval);
    if (!getgpr(clex, &cval))
        uerror("bad base register");
    base = cval;
    if (getlex(&cval) != ')')
        uerror("right par expected");

    opcode |= base << 21;
    opcode |= index << 16;
    if (opcode & 8)
        opcode |= ft << 11;
    else
        opcode |= ft << 6;
    emitword(opcode, relinfo, 0);
}

/*
 * Build and emit a machine instruction code.
 */
void makecmd(unsigned opcode, unsigned type,
             void (*emitfunc)(unsigned, struct reloc *))
{
    unsigned offset, orig_opcode = 0;
    struct reloc relinfo;
    int clex, cval, segment, clobber_reg, negate_literal, mem_offset_fits;

    type &= ~(FNO_VR4300 | FVR4300_ONLY);
    offset = 0;
    relinfo.flags = RABS;
    negate_literal = 0;
    mem_offset_fits = 1;

    /*
     * GCC can generate "j" instead of "jr".
     * Need to detect it early.
     */
    if (type == (FAOFF28 | FDSLOT)) {
        clex = getlex(&cval);
        ungetlex(clex, cval);
        if (clex == LREG) {
            if (opcode == 0x08000000) { /* j - replace by jr */
                opcode = 0x00000008;
                type = FRS1 | FDSLOT;
            }
            if (opcode == 0x0c000000) { /* jal - replace by jalr */
                opcode = 0x00000009;
                type = FRD1 | FRS2 | FDSLOT;
            }
        }
    }

    /*
     * First register.
     */
    cval = 0;
    clobber_reg = 0;
    if (type & FFD1) {
        clex = getlex(&cval);
        if (clex != LFREG)
            uerror("bad fd register");
        opcode |= cval << 6; /* fd, ... */
    }
    if (type & FFS1) {
        clex = getlex(&cval);
        if (clex == LFCC && (type & FFT2)) {
            if (mode_vr4300 && cval != 0)
                uerror("bad fcc register");
            opcode |= cval << 8;
            if (getlex(&cval) != ',')
                uerror("comma expected");
            clex = getlex(&cval);
        }
        if (clex != LFREG)
            uerror("bad fs register");
        opcode |= cval << 11; /* fs, ... */
    }
    if (type & FFT1) {
        clex = getlex(&cval);
        if (clex != LFREG)
            uerror("bad ft register");
        opcode |= cval << 16; /* ft, ... */
    }
    if (type & FCACHEOP) {
        offset = getexpr(&segment);
        if (segment != SABS)
            uerror("absolute value required");
        if (offset > 31)
            uerror("bad cache operation");
        opcode |= (offset & 31) << 16; /* cache op, ... */
    }
    if (type & FRD1) {
        clex = getlex(&cval);
        if (!getgpr(clex, &cval))
            uerror("bad rd register");
        opcode |= cval << 11; /* rd, ... */
    }
    if (type & FRT1) {
        clex = getlex(&cval);
        if (!getgpr(clex, &cval))
            uerror("bad rt register");
        opcode |= cval << 16; /* rt, ... */
    }
    if (type & FRS1) {
    frs1:
        clex = getlex(&cval);
        if (!getgpr(clex, &cval))
            uerror("bad rs register");
        if (cval == 0 && (opcode == 0x0000001a ||  /* div */
                          opcode == 0x0000001b)) { /* divu */
            /* Div instruction with three args.
             * Treat it as a 2-arg variant. */
            if (getlex(&cval) != ',')
                uerror("comma expected");
            goto frs1;
        }
        opcode |= cval << 21; /* rs, ... */
    }
    if (type & FRTD) {
        opcode |= cval << 16; /* rt equals rd */
    }
    if ((type & FMOD) && (type & (FRD1 | FRT1 | FRS1)))
        clobber_reg = cval;

    /*
     * Second register.
     */
    if (type & FRD2) {
        if (getlex(&cval) != ',')
            uerror("comma expected");
        clex = getlex(&cval);
        if (!getgpr(clex, &cval))
            uerror("bad rd register");
        opcode |= cval << 11; /* .., rd, ... */
    }
    if (type & FRT2) {
        if (getlex(&cval) != ',')
            uerror("comma expected");
        clex = getlex(&cval);
        if (!getgpr(clex, &cval)) {
            if ((type & FRD1) && (type & FSA)) {
                /* Second register operand omitted.
                 * Need to restore the missing operand. */
                ungetlex(clex, cval);
                cval = (opcode >> 11) & 31; /* get 1-st register */
                opcode |= cval << 16;       /* use 1-st reg as 2-nd */
                goto fsa;
            }
            uerror("bad rt register");
        }
        opcode |= cval << 16; /* .., rt, ... */
    }
    if (type & FRS2) {
        clex = getlex(&cval);
        if (clex != ',') {
            if ((opcode & 0xfc00003f) != 0x00000009)
                uerror("comma expected");
            /* Jalr with one argument.
             * Treat as if the first argument is $31. */
            ungetlex(clex, cval);
            cval = (opcode >> 11) & 31; /* get 1-st register */
            opcode |= cval << 21;       /* use 1-st reg as 2-nd */
            opcode |= 31 << 11;         /* set 1-st reg to 31 */
            clobber_reg = 31;
            goto done3;
        }
        clex = getlex(&cval);
        if (!getgpr(clex, &cval)) {
            if ((type & FRD1) && (type & FRT3)) {
                /* Three-operand ALU instruction used as "op rd, imm".
                 * Treat it as "op rd, rd, imm". */
                unsigned newop;
                switch (opcode & 0xfc0007ff) {
                case 0x00000020:
                    newop = 0x20000000;
                    break; // add -> addi
                case 0x00000021:
                    newop = 0x24000000;
                    break; // addu -> addiu
                case 0x00000022:
                    newop = 0x20000000;
                    negate_literal = 1;
                    break; // sub -> addi, negate
                case 0x00000023:
                    newop = 0x24000000;
                    negate_literal = 1;
                    break; // subu -> addiu, negate
                default:
                    uerror("bad rs register");
                    return;
                }
                ungetlex(clex, cval);
                cval = (opcode >> 11) & 31; /* get rd */
                newop |= cval << 16;        /* rt = rd */
                newop |= cval << 21;        /* rs = rd */
                orig_opcode = opcode;
                opcode = newop;
                type = FOFF16 | FMOD;
                goto foff16;
            }
            if ((type & FRT1) && (type & FOFF16)) {
                /* Second register operand omitted.
                 * Need to restore the missing operand. */
                ungetlex(clex, cval);
                cval = (opcode >> 16) & 31; /* get 1-st register */
                opcode |= cval << 21;       /* use 1-st reg as 2-nd */
                goto foff16;
            }
            uerror("bad rs register");
        }
        opcode |= cval << 21; /* .., rs, ... */
    }
    if (type & FFS2) {
        if (getlex(&cval) != ',')
            uerror("comma expected");
        clex = getlex(&cval);
        if (clex != LFREG)
            uerror("bad fs register");
        opcode |= cval << 11; /* .., fs, ... */
    }
    if (type & FFT2) {
        if (getlex(&cval) != ',')
            uerror("comma expected");
        clex = getlex(&cval);
        if (clex != LFREG)
            uerror("bad ft register");
        opcode |= cval << 16; /* .., ft, ... */
    }

    /*
     * Third register.
     */
    if (type & FRT3) {
        clex = getlex(&cval);
        if (clex != ',') {
            /* Three-operand instruction used with two operands.
             * Need to restore the missing operand. */
            ungetlex(clex, cval);
            cval = (opcode >> 21) & 31;
            opcode &= ~(31 << 21);                 /* clear 2-nd register */
            opcode |= ((opcode >> 11) & 31) << 21; /* use 1-st reg as 2-nd */
            opcode |= cval << 16;                  /* add 3-rd register */
            goto done3;
        }
        clex = getlex(&cval);
        if (!getgpr(clex, &cval)) {
            if ((type & FRD1) && (type & FRS2)) {
                /* Three-operand instruction used with literal operand.
                 * Convert it to immediate type. */
                unsigned newop;
                switch (opcode & 0xfc0007ff) {
                case 0x00000020:
                    newop = 0x20000000;
                    break; // add -> addi
                case 0x00000021:
                    newop = 0x24000000;
                    break; // addu -> addiu
                case 0x00000024:
                    newop = 0x30000000;
                    break; // and -> andi
                case 0x00000025:
                    newop = 0x34000000;
                    break; // or -> ori
                case 0x0000002a:
                    newop = 0x28000000;
                    break; // slt -> slti
                case 0x0000002b:
                    newop = 0x2c000000;
                    break; // sltu -> sltiu
                case 0x00000022:
                    newop = 0x20000000; // sub -> addi, negate
                    negate_literal = 1;
                    break;
                case 0x00000023:
                    newop = 0x24000000; // subu -> addiu, negate
                    negate_literal = 1;
                    break;
                case 0x00000026:
                    newop = 0x38000000;
                    break; // xor -> xori
                default:
                    uerror("bad rt register");
                    return;
                }
                ungetlex(clex, cval);
                cval = (opcode >> 11) & 31;   /* get 1-st register */
                newop |= cval << 16;          /* set 1-st register */
                newop |= opcode & (31 << 21); /* set 2-nd register */
                orig_opcode = opcode;
                opcode = newop;
                type = FRT1 | FRS2 | FOFF16 | FMOD;
                goto foff16;
            }
            uerror("bad rt register");
        }
        opcode |= cval << 16; /* .., .., rt */
    }
    if (type & FRS3) {
        clex = getlex(&cval);
        if (clex != ',') {
            /* Three-operand instruction used with two operands.
             * Need to restore the missing operand. */
            ungetlex(clex, cval);
            cval = (opcode >> 16) & 31;
            opcode &= ~(31 << 16);                 /* clear 2-nd register */
            opcode |= ((opcode >> 11) & 31) << 16; /* use 1-st reg as 2-nd */
            opcode |= cval << 21;                  /* add 3-rd register */
            goto done3;
        }
        clex = getlex(&cval);
        if (!getgpr(clex, &cval))
            uerror("bad rs register");
        opcode |= cval << 21; /* .., .., rs */
    }
    if (type & FFT3) {
        if (getlex(&cval) != ',')
            uerror("comma expected");
        clex = getlex(&cval);
        if (clex != LFREG)
            uerror("bad ft register");
        opcode |= cval << 16; /* .., .., ft */
    }
done3:

    /*
     * Immediate argument.
     */
    if (type & FSEL) {
        /* optional COP0 register select */
        clex = getlex(&cval);
        if (clex == ',') {
            offset = getexpr(&segment);
            if (segment != SABS)
                uerror("absolute value required");
            opcode |= offset & 7;
        } else
            ungetlex(clex, cval);

    } else if (type & (FCODE | FCODE16 | FSA)) {
        /* Non-relocatable offset */
        if (type & FSA) {
            if (getlex(&cval) != ',')
                uerror("comma expected");
            clex = getlex(&cval);
            if (clex == LREG && type == (FRD1 | FRT2 | FSA | FMOD)) {
                /* Literal-operand shift instruction used with register operand.
                 * Convert it to 3-register type. */
                unsigned newop;
                switch (opcode & 0xffe0003f) {
                case 0x00200002:
                    newop = 0x00000046;
                    break; // ror -> rorv
                case 0x00000000:
                    newop = 0x00000004;
                    break; // sll -> sllv
                case 0x00000003:
                    newop = 0x00000007;
                    break; // sra -> srav
                case 0x00000002:
                    newop = 0x00000006;
                    break; // srl -> srlv
                default:
                    uerror("bad shift amount");
                    return;
                }
                newop |= opcode & (0x3ff << 11); /* set 1-st and 2-nd regs */
                newop |= cval << 21;             /* set 3-rd register */
                opcode = newop;
                type = FRD1 | FRT2 | FRS3 | FMOD;
                goto done3;
            }
            ungetlex(clex, cval);
        }
        if ((type & FCODE) && (type & FRT2)) {
            /* Optional code for trap instruction. */
            clex = getlex(&cval);
            if (clex != ',') {
                ungetlex(clex, cval);
                goto done;
            }
        }
    fsa:
        offset = getexpr(&segment);
        if (segment != SABS)
            uerror("absolute value required");
        switch (type & (FCODE | FCODE16 | FSA)) {
        case FCODE: /* immediate shifted <<6 */
            opcode |= offset << 6;
            break;
        case FCODE16: /* immediate shifted <<16 */
            opcode |= offset << 16;
            break;
        case FSA: /* shift amount */
            opcode |= (offset & 0x1f) << 6;
            break;
        }
    } else if (type & (FOFF16 | FOFF18 | FAOFF18 | FAOFF28 | FHIGH16)) {
        /* Relocatable offset */
        int valid_range;

        if ((type & FAOFF18) && (opcode & 0xff000000) == 0x45000000) {
            clex = getlex(&cval);
            if (clex == LFCC) {
                if (mode_vr4300 && cval != 0)
                    uerror("bad fcc register");
                opcode |= cval << 18;
                if (getlex(&cval) != ',')
                    uerror("comma expected");
            } else {
                ungetlex(clex, cval);
            }
        }
        if ((type & (FOFF16 | FOFF18 | FHIGH16)) && getlex(&cval) != ',')
            uerror("comma expected");
    foff16:
        if ((type & (FOFF16 | FRSB)) == (FOFF16 | FRSB) && has_empty_mem_offset()) {
            offset = 0;
            segment = SABS;
            relinfo.flags = RABS;
            goto have_offset;
        }
        expr_flags = 0;
        offset = getexpr(&segment);
        set_reloc_segment(&relinfo, 0, segment);
        if (expr_flags & EXPR_GPREL)
            relinfo.flags |= RGPREL;
    have_offset:
        switch (type & (FOFF16 | FOFF18 | FAOFF18 | FAOFF28 | FHIGH16)) {
        case FOFF16: /* low 16-bit byte address */
            /* Test whether the immediate is in valid range
             * for the opcode. */
            if (negate_literal) {
                // Negate literal arg for sub and subu
                offset = -offset;
                if (relinfo.flags != RABS)
                    uerror("cannot negate relocatable literal");
            }
            if (expr_flags & EXPR_LO) {
                /*
                 * %lo deliberately selects bits 15:0 of the expression.
                 * The complete symbol addend therefore need not fit the
                 * instruction's signed immediate.  Preserve its low half in
                 * the instruction; R_MIPS_LO16/RBYTE16 carries the relocation
                 * that pairs it with the preceding %hi.
                 */
                valid_range = 1;
            } else {
                switch (opcode & 0xfc000000) {
                default: /* addi, addiu, slti, sltiu, lw, sw */
                    /* 16-bit signed value. */
                    valid_range = fits_signed16(offset);
                    break;
                case 0x30000000: /* andi */
                case 0x34000000: /* ori */
                case 0x38000000: /* xori */
                    /* 16-bit unsigned value. */
                    valid_range = (offset <= 0xffff);
                    break;
                }
            }
            if (valid_range) {
                opcode |= offset & 0xffff;
            } else if (type & FRSB) {
                mem_offset_fits = 0;
            } else if (orig_opcode == 0 || !mode_at) {
                uerror("value out of range");
            } else {
                /* Convert back to 3-reg opcode.
                 * Insert an extra LI instruction. */
                if (segment != SABS)
                    uerror("absolute value required");
                if (negate_literal)
                    offset = -offset;

                if (offset <= 0xffff) {
                    /* ori $1, $zero, value */
                    emitword(0x34010000 | offset, &relabs, 1);
                } else if (offset >= -0x8000) {
                    /* addiu $1, $zero, value */
                    emitword(0x24010000 | (offset & 0xffff), &relabs, 1);
                } else if ((offset & 0xffff) == 0) {
                    /* lui $1, value[31:16] */
                    emitword(0x3c010000 | (offset >> 16), &relabs, 1);
                } else {
                    /* lui $1, value[31:16]
                     * ori $1, $1, value[15:0]) */
                    emitword(0x3c010000 | (offset >> 16), &relabs, 1);
                    emitword(0x34210000 | (offset & 0xffff), &relabs, 1);
                }
                opcode = orig_opcode | 0x10000;
            }
            break;
        case FHIGH16: /* high 16-bit byte address */
            if (expr_flags & EXPR_HI) {
                /* %hi function - assume signed offset */
                relinfo.flags |= RHIGH16S;
                relinfo.offset = offset & 0xffff;
                offset += 0x8000;
                opcode |= offset >> 16;
            } else {
                opcode |= offset & 0xffff;
            }
            break;
        case FOFF18: /* 18-bit PC-relative word address */
        case FAOFF18:
            if (segment == segm) {
                offset -= count[segm] + 4;
                relinfo.flags = RABS;
            } else if (segment == SEXT) {
                relinfo.flags |= RWORD16;
            } else
                uerror("invalid segment %d", segment);
            opcode |= (offset >> 2) & 0xffff;
            break;
        case FAOFF28: /* 28-bit word address */
            opcode |= (offset >> 2) & 0x3ffffff;
            relinfo.flags |= RWORD26;
            break;
        }
    }

    /*
     * Last argument.
     */
    if (type & FRSB) {
        clex = getlex(&cval);
        if (clex != '(') {
            if (type & FOFF16) {
                ungetlex(clex, cval);
                emit_mem_pseudo(opcode, offset, &relinfo, clobber_reg);
                return;
            }
            uerror("left par expected");
        }
        clex = getlex(&cval);
        if (!getgpr(clex, &cval))
            uerror("bad rs register");
        if (getlex(&cval) != ')')
            uerror("right par expected");
        if ((type & FOFF16) && !mem_offset_fits) {
            emit_mem_base_pseudo(opcode, offset, &relinfo, cval,
                clobber_reg, type);
            return;
        }
        opcode |= cval << 21; /* ... (rs) */
    }
    if (type & FSIZE) {
        if (getlex(&cval) != ',')
            uerror("comma expected");
        offset = getexpr(&segment);
        if (segment != SABS)
            uerror("absolute value required");
        opcode |= ((offset - 1) & 0x1f) << 11; /* bit field size */
    }
    if (type & FMSB) {
        if (getlex(&cval) != ',')
            uerror("comma expected");
        offset += getexpr(&segment);
        if (segment != SABS)
            uerror("absolute value required");
        if (offset > 32)
            offset = 32;
        opcode |= ((offset - 1) & 0x1f) << 11; /* msb */
    }
done:

    /* Output resulting values. */
    if (emitfunc) {
        emitfunc(opcode, &relinfo);
    } else if (mode_reorder && (type & FDSLOT) && section_is_text(segm)) {
        /*
         * Reorder mode owns the delay slot, but does not imply optimization.
         * At -O0/-O1 preserve source order and insert a NOP.  At -O2 a very
         * small, fully decoded GPR-only subset may use the buffered preceding
         * instruction.  Explicitly scheduled compiler output uses
         * .set noreorder and bypasses this path.
         */
        if (can_fill_delay_slot(opcode)) {
            if ((type & (FOFF18 | FAOFF18)) && relinfo.flags == RABS) {
                /* The branch moves one word earlier than its parsed PC. */
                offset = (opcode & 0xffff) + 1;
                opcode = (opcode & ~0xffff) | (offset & 0xffff);
            }
            write_instruction(opcode, &relinfo, segm);
            reorder_flush();
            count[segm] += WORDSZ; /* candidate was counted when buffered */
        } else {
            reorder_flush();
            write_instruction(opcode, &relinfo, segm);
            write_instruction(0, &relabs, segm);
            count[segm] += 2 * WORDSZ;
        }
    } else {
        emitword(opcode, &relinfo, clobber_reg);
    }
}

/*
 * Increment the current segment by nbytes.
 * Emit a needed amount of zeroes to sfile and rfile.
 * Part of data have already been sent to rfile;
 * length specified by 'done' argument.
 */
void add_space(unsigned nbytes, unsigned fill_data)
{
    unsigned c;

    if (section_has_contents(segm)) {
        /* Emit data and relocation. */
        for (c = 0; c < nbytes; c++) {
            count[segm]++;
            if (fill_data)
                fputc(0, sfile[segm]);
            if (!(count[segm] & 3))
                fputrel(&relabs, rfile[segm]);
        }
    } else
        count[segm] += nbytes;
}

void makeascii()
{
    register int c, nbytes;
    int cval;

    c = getlex(&cval);
    if (c != '"')
        uerror("no .ascii parameter");
    nbytes = 0;
    for (;;) {
        c = getchar();
        switch (c) {
        case EOF:
            uerror("EOF in text string");
        case '"':
            break;
        case '\\':
            c = getchar();
            switch (c) {
            case EOF:
                uerror("EOF in text string");
            case '\n':
                continue;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
                cval = c & 07;
                c = getchar();
                if (c >= '0' && c <= '7') {
                    cval = (cval << 3) | (c & 7);
                    c = getchar();
                    if (c >= '0' && c <= '7') {
                        cval = (cval << 3) | (c & 7);
                    } else
                        ungetc(c, stdin);
                } else
                    ungetc(c, stdin);
                c = cval;
                break;
            case 't':
                c = '\t';
                break;
            case 'b':
                c = '\b';
                break;
            case 'r':
                c = '\r';
                break;
            case 'n':
                c = '\n';
                break;
            case 'f':
                c = '\f';
                break;
            }
        default:
            fputc(c, sfile[segm]);
            nbytes++;
            continue;
        }
        break;
    }
    add_space(nbytes, 0);
}

/*
 * Skip a string from the input file.
 */
void skipstring()
{
    int c, cval;

    c = getlex(&cval);
    if (c != '"')
        uerror("no string parameter");
    for (;;) {
        c = getchar();
        switch (c) {
        case EOF:
            uerror("EOF in text string");
        case '"':
            break;
        case '\\':
            c = getchar();
            switch (c) {
            case EOF:
                uerror("EOF in text string");
            case '\n':
                continue;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
                c = getchar();
                if (c >= '0' && c <= '7') {
                    c = getchar();
                    if (c >= '0' && c <= '7') {
                    } else
                        ungetc(c, stdin);
                } else
                    ungetc(c, stdin);
                break;
            }
        default:
            continue;
        }
        break;
    }
}

/*
 * Set assembler option.
 */
void setoption()
{
    const char *option = name;
    int enable = 1;

    if (option[0] == 'n' && option[1] == 'o') {
        enable = 0;
        option += 2;
    }
    if (!strcmp("reorder", option)) {
        /* reorder mode */
        mode_reorder = enable;
        if (!mode_reorder)
            reorder_flush();
        return;
    }
    if (!strcmp("macro", option)) {
        /* macro mode */
        mode_macro = enable;
        return;
    }
    if (!strcmp("mips16", option)) {
        /* mips16 mode */
        mode_mips16 = enable;
        return;
    }
    if (!strcmp("micromips", option)) {
        /* micromips mode */
        mode_micromips = enable;
        return;
    }
    if (!strcmp("mips3", option) || !strcmp("mips32", option) ||
        !strcmp("mips32r2", option)) {
        if (!enable)
            return;
        set_cpu_vr4300(strcmp("mips3", option) == 0);
        return;
    }
    if (!strcmp("at", option)) {
        /* at mode */
        mode_at = enable;
        return;
    }
    uerror("unknown option %s", option);
}

int
skiptoeol_direct()
{
    int c;

    do {
        c = getchar();
    } while (c != '\n' && c != EOF);
    if (c == '\n') {
        ungetc(c, stdin);
        return (LEOL);
    }
    return (LEOF);
}

/*
 * Align the current segment.
 */
void align(int align_bits)
{
    unsigned nbytes, align_mask, c;

    align_mask = (1 << align_bits) - 1;
    if (valid_section(segm) && (1U << align_bits) > sections[segm].align)
        sections[segm].align = 1U << align_bits;
    nbytes = count[segm] & align_mask;
    if (nbytes == 0)
        return;
    nbytes = align_mask + 1 - nbytes;
    if (section_has_contents(segm)) {
        /* Emit data and relocation. */
        for (c = 0; c < nbytes; c++) {
            count[segm]++;
            fputc(0, sfile[segm]);
            if (!(count[segm] & 3))
                fputrel(&relabs, rfile[segm]);
        }
    } else
        count[segm] += nbytes;
}

void pass1()
{
    register int clex;
    int cval, tval, csegm, nbytes, symidx;
    register unsigned addr;
    unsigned a, bits;

    segm = STEXT;
    prev_segm = STEXT;
    mark_section_touched(segm);
    for (;;) {
        clex = getlex(&cval);
        switch (clex) {
        case LEOF:
        done:
            reorder_flush();
            if (elf_output) {
                int s;
                for (s = 0; s < maxsegm; s++) {
                    unsigned a, bits;

                    if (!valid_section(s))
                        continue;
                    a = sections[s].align;
                    for (bits = 0; (1U << bits) < a; bits++)
                        ;
                    segm = s;
                    align(bits);
                }
            } else {
                segm = STEXT;
                align(text_align_bits);
                /*
                 * a.out stores writable data and read-only data in one data
                 * segment.  Preserve the alignment requested by .rodata at
                 * that otherwise invisible boundary.
                 */
                segm = SDATA;
                a = sections[SSTRNG].align;
                for (bits = 0; (1U << bits) < a; bits++)
                    ;
                align(bits);
                segm = SSTRNG;
                align(2);
                segm = SCTORS;
                align(2);
                segm = SDTORS;
                align(2);
                segm = SBSS;
                align(2);
            }
            return;
        case LEOL:
            continue;
        case ':':
            continue;
        case '.':
            if (getlex(&cval) != '=')
                uerror("bad instruction");
            addr = getexpr(&csegm);
            if (csegm != segm)
                uerror("bad count assignment");
            if (addr < count[segm])
                uerror("negative count increment");
            reorder_flush();
            if (segm == SBSS)
                count[segm] = addr;
            else {
                while (count[segm] < addr) {
                    emitword(0, &relabs, 0);
                }
            }
            break;
        case LNAME:
            cval = lookcmd();
            clex = getlex(&tval);
            if (clex == ':') {
                /* Label. */
                reorder_flush();
                cval = lookname();
                stab[cval].n_value = count[segm];
                stab[cval].n_type &= ~N_TYPE;
                stab[cval].n_type |= section_nlist_type(segm);
                stabsegm[cval] = segm;
                continue;
            } else if (clex == '=') {
                /* Symbol definition. */
                cval = lookname();
                stab[cval].n_value = getexpr(&csegm);
                if (csegm == SEXT)
                    uerror("indirect equivalence");
                stab[cval].n_type &= N_EXT;
                stab[cval].n_type |= section_nlist_type(csegm);
                stabsegm[cval] = csegm;
                break;
            }
            /* Machine instruction. */
            if (cval < 0)
                uerror("bad instruction");
            if (mode_vr4300 && !strcmp(optable[cval].name, "mul")) {
                ungetlex(clex, tval);
                align(2);
                makecmd(0, optable[cval].type & ~FNO_VR4300, emit_mul);
                break;
            }
            if (mode_vr4300 && (optable[cval].type & FNO_VR4300))
                uerror("%s is not supported by VR4300", optable[cval].name);
            if (!mode_vr4300 && (optable[cval].type & FVR4300_ONLY))
                uerror("%s requires MIPS3/VR4300", optable[cval].name);
            ungetlex(clex, tval);
            align(2);
            makecmd(optable[cval].opcode, optable[cval].type, optable[cval].func);
            break;
        case LNUM:
            /* Local label. */
            if (nlabels >= MAXRLAB)
                uerror("too many digital labels");
            reorder_flush();
            labeltab[nlabels].num = intval;
            labeltab[nlabels].value = count[segm];
            ++nlabels;
            clex = getlex(&tval);
            if (clex != ':')
                uerror("bad digital label");
            continue;
        case LTEXT:
            switchsection(STEXT);
            break;
        case LDATA:
            switchsection(SDATA);
            break;
        case LSTRNG:
        case LRDATA:
            switchsection(SSTRNG);
            break;
        case LBSS:
            switchsection(SBSS);
            break;
        case LWORD:
            reorder_flush();
            align(2);
            for (;;) {
                struct reloc relinfo;
                expr_flags = 0;
                getexpr(&cval);
                set_reloc_segment(&relinfo, RBYTE32, cval);
                if (expr_flags & EXPR_GPREL)
                    relinfo.flags |= RGPREL;
                emitword(intval, &relinfo, 0);
                clex = getlex(&cval);
                if (clex != ',') {
                    ungetlex(clex, cval);
                    break;
                }
            }
            break;
        case LBYTE:
            reorder_flush();
            nbytes = 0;
            for (;;) {
                getexpr(&cval);
                fputc(intval, sfile[segm]);
                nbytes++;
                clex = getlex(&cval);
                if (clex != ',') {
                    ungetlex(clex, cval);
                    break;
                }
            }
            add_space(nbytes, 0);
            break;
        case LHALF:
            reorder_flush();
            align(1);
            nbytes = 0;
            for (;;) {
                getexpr(&cval);
#ifdef TARGET_BIG_ENDIAN
                    fputc(intval >> 8, sfile[segm]);
                    fputc(intval, sfile[segm]);
#else
                    fputc(intval, sfile[segm]);
                    fputc(intval >> 8, sfile[segm]);
#endif
                nbytes += 2;
                clex = getlex(&cval);
                if (clex != ',') {
                    ungetlex(clex, cval);
                    break;
                }
            }
            add_space(nbytes, 0);
            break;
        case LSPACE:
            /* .space num */
            getexpr(&cval);
            reorder_flush();
            add_space(intval, 1);
            break;
        case LALIGN:
            /* .align num */
            if (getlex(&cval) != LNUM)
                uerror("bad parameter of .align");
            reorder_flush();
            align(intval);
            break;
        case LASCII:
            reorder_flush();
            makeascii();
            break;
        case LGLOBL:
        case LEXTERN:
            /* .globl/.extern name, ... */
            for (;;) {
                clex = getlex(&cval);
                if (clex != LNAME)
                    uerror("bad parameter of .globl/.extern");
                cval = lookname();
                if (stab[cval].n_type & N_LOC)
                    uerror("local name redefined as global");
                stab[cval].n_type |= N_EXT;
                clex = getlex(&cval);
                if (clex != ',') {
                    ungetlex(clex, cval);
                    break;
                }
            }
            break;
        case LLOCAL:
            /* .local name, ... */
            for (;;) {
                clex = getlex(&cval);
                if (clex != LNAME)
                    uerror("bad parameter of .local");
                cval = lookname();
                if (stab[cval].n_type & N_EXT)
                    uerror("global name redefined as local");
                stab[cval].n_type |= N_LOC;
                clex = getlex(&cval);
                if (clex != ',') {
                    ungetlex(clex, cval);
                    break;
                }
            }
            break;
        case LWEAK:
            /* .weak name */
            for (;;) {
                clex = getlex(&cval);
                if (clex != LNAME)
                    uerror("bad parameter of .weak");
                cval = lookname();
                stab[cval].n_type |= N_WEAK;
                clex = getlex(&cval);
                if (clex != ',') {
                    ungetlex(clex, cval);
                    break;
                }
            }
            break;
        case LEQU:
            /* .equ name,value */
            if (getlex(&cval) != LNAME)
                uerror("bad parameter of .equ");
            cval = lookname();
            if (stab[cval].n_type != N_UNDF && stab[cval].n_type != N_LOC &&
                (stab[cval].n_type & N_TYPE) != N_COMM)
                uerror("name already defined");
            clex = getlex(&tval);
            if (clex != ',')
                uerror("bad value of .equ");
            stab[cval].n_value = getexpr(&csegm);
            if (csegm == SEXT)
                uerror("indirect equivalence");
            stab[cval].n_type &= N_EXT;
            stab[cval].n_type |= section_nlist_type(csegm);
            stabsegm[cval] = csegm;
            break;
        case LCOMM:
        case LLCOMM:
            /* .comm/.lcomm name,len[,alignment] */
            if (getlex(&cval) != LNAME)
                uerror("bad parameter of .comm");
            cval = lookname();
            symidx = cval;
            if (stab[cval].n_type != N_UNDF && stab[cval].n_type != N_LOC &&
                (stab[cval].n_type & N_TYPE) != N_COMM)
                uerror("name already defined");
            if (stab[cval].n_type & N_LOC || clex == LLCOMM)
                stab[cval].n_type = N_COMM;
            else
                stab[cval].n_type = N_EXT | N_COMM;
            clex = getlex(&tval);
            if (clex == ',') {
                getexpr(&tval);
                if (tval != SABS)
                    uerror("bad length of .comm");
            } else {
                ungetlex(clex, cval);
                intval = 1;
            }
            stab[cval].n_value = intval;
            clex = getlex(&cval);
            if (clex != ',') {
                ungetlex(clex, cval);
                break;
            }
            getexpr(&tval);
            if (tval != SABS)
                uerror("bad .comm alignment");
            commalign[symidx] = intval;
            break;
        case LFILE:
            /* .file line filename */
            if (getlex(&cval) != LNUM)
                uerror("bad parameter of .file");
            skipstring();
            break;
        case LIDENT:
            /* .ident string */
            skipstring();
            break;
        case LSECTION:
            /* .section name[,"flags"[,type[,entsize]]] */
        {
            char secname[256];
            char flagsbuf[64];
            unsigned secflags, sectype;
            int has_flags, has_type;

            secflags = 0;
            sectype = SHT_PROGBITS;
            has_flags = 0;
            has_type = 0;
            clex = getlex(&cval);
            if (clex != LNAME && clex != LBSS && clex != LTEXT && clex != LDATA)
                uerror("bad name of .section");
            xstrcpy(secname, name);
            clex = getlex(&cval);
            if (clex != ',') {
                ungetlex(clex, cval);
                setsection(secname, secflags, has_flags, sectype, has_type);
                break;
            }
            clex = getlex(&cval);
            if (clex == '"') {
                read_quoted_string(flagsbuf, sizeof(flagsbuf));
                secflags = section_flags_from_string(flagsbuf);
                has_flags = 1;
            } else if (clex != LNAME)
                uerror("bad type of .section");
            clex = getlex(&cval);
            if (clex != ',') {
                ungetlex(clex, cval);
                setsection(secname, secflags, has_flags, sectype, has_type);
                break;
            }
            if (getlex(&cval) != LSECTYPE)
                uerror("bad type of .section");
            sectype = section_type_from_name(name);
            has_type = 1;
            clex = getlex(&cval);
            if (clex != ',') {
                ungetlex(clex, cval);
                setsection(secname, secflags, has_flags, sectype, has_type);
                break;
            }
            if (getlex(&cval) != LNUM)
                uerror("bad entry size of .section");
            setsection(secname, secflags, has_flags, sectype, has_type);
            break;
        }
        case LPREVIOUS:
            previoussection();
            break;
        case LGNUATTR:
            /* .gnu_attribute num[,num] */
            if (getlex(&cval) != LNUM)
                uerror("bad parameter of .gnu_attribute");
            clex = getlex(&cval);
            if (clex != ',' || getlex(&cval) != LNUM)
                uerror("bad parameter of .gnu_attribute");
            break;
        case LMODULE:
            /* .module name[=value] - accepted and ignored. */
            clex = skiptoeol_direct();
            if (clex == LEOF)
                goto done;
            break;
        case LSET:
            /* .set option */
            if (getlex(&cval) != LNAME)
                uerror("bad parameter of .set");
            setoption();
            break;
        case LENT:
            /* .ent name */
            clex = getlex(&cval);
            if (clex != LNAME)
                uerror("bad parameter of .ent");
            cval = lookname();
            break;
        case LEND:
            /* .end name */
            clex = getlex(&cval);
            if (clex != LNAME)
                uerror("bad parameter of .end");
            cval = lookname();
            break;
        case LNAN:
            /* .nan name */
            clex = getlex(&cval);
            if (clex != LNAME)
                uerror("bad parameter of .nan");
            break;
        case LTYPE:
            /* .type name,type */
            if (getlex(&cval) != LNAME)
                uerror("bad name of .type");
            clex = getlex(&cval);
            if (clex != ',') {
                ungetlex(clex, cval);
                break;
            }
            if (getlex(&cval) != LSYMTYPE)
                uerror("bad type of .type");
            break;
        case LFRAME:
            /* .frame reg,num,reg */
            if (getlex(&cval) != LREG)
                uerror("bad register of .frame");
            clex = getlex(&cval);
            if (clex != ',' || getlex(&cval) != LNUM)
                uerror("bad parameter of .frame");
            clex = getlex(&cval);
            if (clex != ',' || getlex(&cval) != LREG)
                uerror("bad register of .frame");
            break;
        case LMASK:
            /* .mask mask,expr */
            if (getlex(&cval) != LNUM)
                uerror("bad mask of .mask");
            clex = getlex(&cval);
            if (clex != ',')
                uerror("bad parameter of .mask");
            getexpr(&cval);
            if (cval != SABS)
                uerror("bad expression of .mask");
            break;
        case LFMASK:
            /* .fmask mask,expr */
            if (getlex(&cval) != LNUM)
                uerror("bad mask of .fmask");
            clex = getlex(&cval);
            if (clex != ',')
                uerror("bad parameter of .fmask");
            getexpr(&cval);
            if (cval != SABS)
                uerror("bad expression of .fmask");
            break;
        case LSIZE:
            /* .size name,expr */
            if (getlex(&cval) != LNAME)
                uerror("bad name of .size");
            clex = getlex(&cval);
            if (clex != ',') {
                ungetlex(clex, cval);
                break;
            }
            nbytes = getexpr(&csegm);
            if (csegm != SABS)
                uerror("bad value of .size");
            break;
        default:
            uerror("bad syntax");
        }
        clex = getlex(&cval);
        if (clex == LEOF)
            goto done;
        if (clex != LEOL)
            uerror("bad instruction arguments");
    }
}

/*
 * Find the relative label address,
 * by the reference address and the label number.
 * Backward references have negative label numbers.
 */
int findlabel(int addr, int sym)
{
    struct labeltab *p;

    if (sym < 0) {
        /* Backward reference. */
        for (p = labeltab + nlabels - 1; p >= labeltab; --p) {
            if (p->value <= addr && p->num == -sym) {
                return p->value;
            }
        }
        uerror("undefined label %db at address %d", -sym, addr);
    } else {
        /* Forward reference. */
        for (p = labeltab; p < labeltab + nlabels; ++p) {
            if (p->value > addr && p->num == sym) {
                return p->value;
            }
        }
        uerror("undefined label %df at address %d", sym, addr);
    }
    return 0;
}

int is_rebsd_metadata_symbol(struct nlist *sp)
{
    return strcmp(sp->n_name, REBSD_CTORS_SIZE_SYM) == 0 ||
        strcmp(sp->n_name, REBSD_DTORS_SIZE_SYM) == 0;
}

void define_rebsd_metadata_symbol(const char *sym, unsigned value)
{
    int idx;

    if (xstrlen(sym) >= sizeof(name))
        uerror("metadata symbol name too long");
    xstrcpy(name, sym);
    idx = lookname();
    stab[idx].n_value = value;
    stab[idx].n_type = N_ABS;
}

void middle()
{
    register int i, snum, nbytes;

    if (count[SCTORS] || count[SDTORS]) {
        define_rebsd_metadata_symbol(REBSD_CTORS_SIZE_SYM, count[SCTORS]);
        define_rebsd_metadata_symbol(REBSD_DTORS_SIZE_SYM, count[SDTORS]);
    }

    stlength = 0;
    for (snum = 0, i = 0; i < stabfree; i++) {
        switch (stab[i].n_type) {
        case N_UNDF:
            /* Without -u option, undefined symbol is considered external */
            if (uflag)
                uerror("name undefined", stab[i].n_name);
            stab[i].n_type |= N_EXT;
            break;
        case N_COMM:
            /* Allocate a local common block */
            /* Align BSS count. */
            count[SBSS] = align_up(count[SBSS],
                commalign[i] ? commalign[i] : WORDSZ);
            nbytes = stab[i].n_value;
            stab[i].n_value = count[SBSS];
            stab[i].n_type = N_BSS;
            stabsegm[i] = SBSS;
            count[SBSS] += nbytes;
            break;
        }
        if (xflags)
            newindex[i] = snum;

        if (!xflags || (stab[i].n_type & N_EXT) ||
            is_rebsd_metadata_symbol(&stab[i]) ||
            (Xflag && !IS_LOCAL(&stab[i]))) {
            stlength += 2 + WORDSZ + stab[i].n_len;
            snum++;
        }
    }
    stalign = WORDSZ - stlength % WORDSZ;
    stlength += stalign;
    line = 0;
}

void makeheader(int rtsize, int rdsize)
{
    struct exec hdr;

    /* Align BSS size. */
    count[SBSS] = (count[SBSS] + WORDSZ - 1) & ~(WORDSZ - 1);

    hdr.a_midmag = RMAGIC;
    hdr.a_text = count[STEXT];
    hdr.a_data = count[SDATA] + count[SSTRNG] + count[SCTORS] + count[SDTORS];
    hdr.a_bss = count[SBSS];
    hdr.a_reltext = rtsize;
    hdr.a_reldata = rdsize;
    hdr.a_syms = stlength;
    hdr.a_entry = 0;
    fseek(stdout, 0, 0);
    fputhdr(&hdr, stdout);
}

unsigned relocate(unsigned opcode, unsigned offset, struct reloc *relinfo)
{
    switch (relinfo->flags & RFMASK) {
    case RBYTE32: /* 32 bits of byte address */
        opcode += offset;
        break;
    case RBYTE16: /* low 16 bits of byte address */
        offset += opcode & 0xffff;
        opcode &= ~0xffff;
        opcode |= offset & 0xffff;
        break;
    case RHIGH16: /* high 16 bits of byte address */
        offset += (opcode & 0xffff) << 16;
        offset += relinfo->offset;
        opcode &= ~0xffff;
        opcode |= (offset >> 16) & 0xffff;
        relinfo->offset = offset & 0xffff;
        break;
    case RHIGH16S: /* high 16 bits of byte address */
        offset += (opcode & 0xffff) << 16;
        offset += (signed short)relinfo->offset;
        opcode &= ~0xffff;
        opcode |= ((offset + 0x8000) >> 16) & 0xffff;
        relinfo->offset = offset & 0xffff;
        break;
    case RWORD16: /* 16 bits of relative word address */
        uerror("bad relative relocation: opcode %08x, relinfo %02x", opcode, relinfo->flags);
        break;
    case RWORD26: /* 26 bits of word address */
        offset += (opcode & 0x3ffffff) << 2;
        opcode &= ~0x3ffffff;
        opcode |= (offset >> 2) & 0x3ffffff;
        break;
    }
    return (opcode);
}

unsigned makeword(unsigned opcode, struct reloc *relinfo, unsigned offset)
{
    struct nlist *sym;
    unsigned value;

    if (elf_output) {
        if ((relinfo->flags & RSMASK) != REXT)
            return opcode;
        if (relinfo->index < RLAB_OFFSET - RLAB_MAXVAL)
            return opcode;
        value = findlabel(offset, relinfo->index - RLAB_OFFSET);
        if ((relinfo->flags & RFMASK) == RWORD16) {
            offset = value - offset - 4;
            offset += (opcode & 0xffff) << 2;
            opcode &= ~0xffff;
            opcode |= (offset >> 2) & 0xffff;
            relinfo->flags = RABS;
            return opcode;
        }
        relinfo->flags &= RGPREL | RFMASK;
        relinfo->flags |= REXT;
        relinfo->index = SECTION_RELOC_INDEX_BASE + segm;
        return opcode;
    }

    switch (relinfo->flags & RSMASK) {
    case RABS:
        break;
    case RTEXT:
        opcode = relocate(opcode, tbase, relinfo);
        break;
    case RDATA:
        opcode = relocate(opcode, dbase, relinfo);
        break;
    case RSTRNG:
        opcode = relocate(opcode, adbase, relinfo);
        break;
    case RCTORS:
        opcode = relocate(opcode, ctbase, relinfo);
        break;
    case RDTORS:
        opcode = relocate(opcode, dtbase, relinfo);
        break;
    case RBSS:
        opcode = relocate(opcode, bbase, relinfo);
        break;
    case REXT:
        if (relinfo->index >= RLAB_OFFSET - RLAB_MAXVAL) {
            /* Relative label.
             * Change relocation to segment type. */
            sym = 0;
            value = findlabel(offset, relinfo->index - RLAB_OFFSET);
            relinfo->flags &= RGPREL | RFMASK;
            relinfo->flags |= section_reloc_type(segm);
        } else {
            /* Symbol name. */
            sym = &stab[relinfo->index];
            if (sym->n_type == N_EXT + N_UNDF || sym->n_type == N_EXT + N_COMM)
                return opcode;
            value = sym->n_value;
        }

        switch (relinfo->flags & RFMASK) {
        case RWORD16:
            /* Relative word address.
             * Change relocation to absolute. */
            if (sym && stabsegm[relinfo->index] != segm)
                uerror("%s: bad segment for relative relocation, offset %u", sym->n_name, offset);
            offset = value - offset - 4;
            if (segm == SDATA)
                offset -= dbase;
            else if (segm == SSTRNG)
                offset -= adbase;
            else if (segm == SCTORS)
                offset -= ctbase;
            else if (segm == SDTORS)
                offset -= dtbase;
            offset += (opcode & 0xffff) << 2;
            opcode &= ~0xffff;
            opcode |= (offset >> 2) & 0xffff;
            relinfo->flags = RABS;
            return opcode;
        case RHIGH16:
        case RHIGH16S:
            /*
             * relocate() reconstructs the high/low pair using relinfo->offset.
             * Do not fold the addend into value here, or the high relocation
             * gets the low addend twice while the matching low relocation does
             * not.  That mislinks forward references like %hi(s+field).
             */
            break;
        }
        opcode = relocate(opcode, value, relinfo);
        break;
    }
    return opcode;
}

struct strtab {
    char *data;
    unsigned len;
    unsigned cap;
    int heap;
};

struct elf_outsec {
    Elf32_Shdr sh;
    int kind;
    int src;
};

enum {
    ESEC_NONE,
    ESEC_INPUT,
    ESEC_REL,
    ESEC_SYMTAB,
    ESEC_STRTAB,
    ESEC_SHSTRTAB,
};

void
strtab_init(struct strtab *t)
{
    t->cap = 256;
    t->data = malloc(t->cap);
    if (!t->data)
        uerror("out of memory");
    t->heap = 1;
    t->data[0] = 0;
    t->len = 1;
}

void
strtab_init_static(struct strtab *t, char *storage, unsigned cap)
{
    t->data = storage;
    t->cap = cap;
    t->heap = 0;
    t->data[0] = 0;
    t->len = 1;
}

void
strtab_free(struct strtab *t)
{
    if (t->heap)
        free(t->data);
}

unsigned
strtab_add(struct strtab *t, const char *s)
{
    unsigned off, len;
    char *p;
    unsigned i;

    off = t->len;
    len = xstrlen(s) + 1;
    if (t->len + len > t->cap) {
        while (t->len + len > t->cap)
            t->cap *= 2;
        if (t->heap) {
            p = realloc(t->data, t->cap);
        } else {
            p = malloc(t->cap);
            if (p) {
                unsigned j;

                for (j = 0; j < t->len; j++)
                    p[j] = t->data[j];
            }
            t->heap = 1;
        }
        if (!p)
            uerror("out of memory");
        t->data = p;
    }
    for (i = 0; i < len; i++)
        t->data[t->len + i] = s[i];
    t->len += len;
    return off;
}

void
elf_write_bytes(const void *data, unsigned len, FILE *f)
{
    const unsigned char *p;
    unsigned i;

    p = data;
    for (i = 0; i < len; i++)
        putc(p[i], f);
}

void
elf_put16(unsigned value, FILE *f)
{
    aout_put16(value, f);
}

void
elf_put32(unsigned value, FILE *f)
{
    aout_put32(value, f);
}

void
elf_write_ehdr(const Elf32_Ehdr *h, FILE *f)
{
    elf_write_bytes(h->e_ident, sizeof(h->e_ident), f);
    elf_put16(h->e_type, f);
    elf_put16(h->e_machine, f);
    elf_put32(h->e_version, f);
    elf_put32(h->e_entry, f);
    elf_put32(h->e_phoff, f);
    elf_put32(h->e_shoff, f);
    elf_put32(h->e_flags, f);
    elf_put16(h->e_ehsize, f);
    elf_put16(h->e_phentsize, f);
    elf_put16(h->e_phnum, f);
    elf_put16(h->e_shentsize, f);
    elf_put16(h->e_shnum, f);
    elf_put16(h->e_shstrndx, f);
}

void
elf_write_shdr(const Elf32_Shdr *h, FILE *f)
{
    elf_put32(h->sh_name, f);
    elf_put32(h->sh_type, f);
    elf_put32(h->sh_flags, f);
    elf_put32(h->sh_addr, f);
    elf_put32(h->sh_offset, f);
    elf_put32(h->sh_size, f);
    elf_put32(h->sh_link, f);
    elf_put32(h->sh_info, f);
    elf_put32(h->sh_addralign, f);
    elf_put32(h->sh_entsize, f);
}

void
elf_write_sym(const Elf32_Sym *s, FILE *f)
{
    elf_put32(s->st_name, f);
    elf_put32(s->st_value, f);
    elf_put32(s->st_size, f);
    putc(s->st_info, f);
    putc(s->st_other, f);
    elf_put16(s->st_shndx, f);
}

void
elf_write_rel(const Elf32_Rel *r, FILE *f)
{
    elf_put32(r->r_offset, f);
    elf_put32(r->r_info, f);
}

void
emit_padding(unsigned *pos, unsigned target)
{
    while (*pos < target) {
        putchar(0);
        (*pos)++;
    }
}

void
copy_temp_file(FILE *f, unsigned size)
{
    unsigned i;
    int c;

    rewind(f);
    for (i = 0; i < size; i++) {
        c = getc(f);
        if (c == EOF)
            c = 0;
        putchar(c);
    }
}

void
append_elf_sym(Elf32_Sym **tab, unsigned *n, unsigned *cap,
    const Elf32_Sym *sym)
{
    Elf32_Sym *p;

    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 128;
        p = realloc(*tab, *cap * sizeof(**tab));
        if (!p)
            uerror("out of memory");
        *tab = p;
    }
    (*tab)[(*n)++] = *sym;
}

int
elf_section_should_output(int s)
{
    if (!valid_section(s))
        return 0;
    if (s <= SBSS)
        return 1;
    return sections[s].touched || count[s] != 0;
}

unsigned
elf_count_relocs(int s)
{
    unsigned i, n;
    struct reloc relinfo;

    if (!elf_section_should_output(s) || !section_has_contents(s))
        return 0;
    rewind(rfile[s]);
    n = 0;
    for (i = 0; i < count[s]; i += WORDSZ) {
        fgetrel(rfile[s], &relinfo);
        if ((relinfo.flags & RSMASK) != RABS)
            n++;
    }
    return n;
}

int
elf_symbol_bind(int i)
{
    if (stab[i].n_type & N_WEAK)
        return STB_WEAK;
    if (stab[i].n_type & N_EXT)
        return STB_GLOBAL;
    return STB_LOCAL;
}

unsigned
elf_symbol_shndx(int i)
{
    int type, s;

    type = stab[i].n_type & N_TYPE;
    switch (type) {
    case N_UNDF:
        return SHN_UNDEF;
    case N_ABS:
        return SHN_ABS;
    case N_COMM:
        return SHN_COMMON;
    default:
        s = stabsegm[i];
        if (!valid_section(s))
            s = section_from_nlist_type(type);
        if (!valid_section(s) || !sections[s].shndx)
            uerror("symbol %s has no ELF section", stab[i].n_name);
        return sections[s].shndx;
    }
}

void
make_elf_symbol(int i, struct strtab *strtab, Elf32_Sym *sym)
{
    int bind, type;

    memset(sym, 0, sizeof(*sym));
    bind = elf_symbol_bind(i);
    type = stab[i].n_type & N_TYPE;
    sym->st_name = strtab_add(strtab, stab[i].n_name);
    sym->st_info = ELF_ST_INFO(bind, STT_NOTYPE);
    sym->st_shndx = elf_symbol_shndx(i);
    if (type == N_COMM) {
        sym->st_value = commalign[i] ? commalign[i] : WORDSZ;
        sym->st_size = stab[i].n_value;
    } else {
        sym->st_value = stab[i].n_value;
        sym->st_size = 0;
    }
}

unsigned
elf_reloc_type(struct reloc *r)
{
    if (r->flags & RGPREL) {
        if ((r->flags & RFMASK) == RBYTE32)
            return R_MIPS_GPREL32;
        return R_MIPS_GPREL16;
    }
    switch (r->flags & RFMASK) {
    case RBYTE32:
        return R_MIPS_32;
    case RBYTE16:
        return R_MIPS_LO16;
    case RHIGH16:
    case RHIGH16S:
        return R_MIPS_HI16;
    case RWORD16:
        return R_MIPS_PC16;
    case RWORD26:
        return R_MIPS_26;
    default:
        return R_MIPS_NONE;
    }
}

unsigned
elf_reloc_symbol(struct reloc *r, int *symndx)
{
    int source, s;

    source = r->flags & RSMASK;
    if (source == REXT) {
        if (r->index >= SECTION_RELOC_INDEX_BASE) {
            s = r->index - SECTION_RELOC_INDEX_BASE;
            if (!valid_section(s) || !sections[s].symndx)
                uerror("bad ELF section relocation");
            return sections[s].symndx;
        }
        if (r->index >= (unsigned)stabfree || !symndx[r->index])
            uerror("bad ELF symbol relocation");
        return symndx[r->index];
    }
    switch (source) {
    case RTEXT:
        s = STEXT;
        break;
    case RDATA:
        s = SDATA;
        break;
    case RSTRNG:
        s = SSTRNG;
        break;
    case RCTORS:
        s = SCTORS;
        break;
    case RDTORS:
        s = SDTORS;
        break;
    case RBSS:
        s = SBSS;
        break;
    default:
        uerror("bad ELF relocation source");
        return 0;
    }
    if (!sections[s].symndx)
        uerror("bad ELF relocation section");
    return sections[s].symndx;
}

void
elf_write_reloc(unsigned off, struct reloc *relinfo, int *symndx)
{
    Elf32_Rel rel;

    rel.r_offset = off;
    rel.r_info = ELF_R_INFO(elf_reloc_symbol(relinfo, symndx),
        elf_reloc_type(relinfo));
    elf_write_rel(&rel, stdout);
}

static int
elf_reloc_same_source(const struct reloc *a, const struct reloc *b)
{
    if ((a->flags & (RSMASK | RGPREL)) !=
        (b->flags & (RSMASK | RGPREL)))
        return 0;
    if ((a->flags & RSMASK) == REXT)
        return a->index == b->index;
    return 1;
}

void
elf_write_relocs(int s, int *symndx)
{
    struct reloc *rv;
    unsigned *word;
    unsigned char *done;
    unsigned i, h, nword, format;

    nword = (count[s] + WORDSZ - 1) / WORDSZ;
    rv = calloc(nword, sizeof(*rv));
    word = calloc(nword, sizeof(*word));
    done = calloc(nword, sizeof(*done));
    if (!rv || !word || !done)
        uerror("out of memory");

    rewind(rfile[s]);
    rewind(sfile[s]);
    for (i = 0; i < nword; i++) {
        fgetrel(rfile[s], &rv[i]);
        word[i] = fgetword(sfile[s]);
    }

    /*
     * ELF/MIPS REL does not carry an explicit addend.  A HI16 relocation
     * obtains its low half from the following matching LO16 relocation.
     * Instruction order is not sufficient: GCC can put a lui in a branch
     * delay slot after the lw/addiu which consumes it, and several references
     * to one section symbol can carry different addends.  The internal
     * relocation retains the exact low half in reloc.offset, so emit every
     * matching HI immediately before its LO, as GNU as does.
     */
    for (i = 0; i < nword; i++) {
        if ((rv[i].flags & RSMASK) == RABS)
            continue;
        format = rv[i].flags & RFMASK;
        if (format == RHIGH16 || format == RHIGH16S)
            continue;
        if (format == RBYTE16) {
            for (h = 0; h < nword; h++) {
                unsigned hformat = rv[h].flags & RFMASK;

                if (done[h] || (rv[h].flags & RSMASK) == RABS ||
                    (hformat != RHIGH16 && hformat != RHIGH16S))
                    continue;
                if (!elf_reloc_same_source(&rv[h], &rv[i]) ||
                    (rv[h].offset & 0xffff) != (word[i] & 0xffff))
                    continue;
                elf_write_reloc(h * WORDSZ, &rv[h], symndx);
                done[h] = 1;
            }
        }
        elf_write_reloc(i * WORDSZ, &rv[i], symndx);
        done[i] = 1;
    }

    /* Preserve unmatched relocations so the linker can diagnose them. */
    for (i = 0; i < nword; i++) {
        if (done[i] || (rv[i].flags & RSMASK) == RABS)
            continue;
        elf_write_reloc(i * WORDSZ, &rv[i], symndx);
    }
    free(done);
    free(word);
    free(rv);
}

void
makeelf(void)
{
    struct strtab shstrtab, strtab;
    static char shstrtab_storage[MAXSEGM * 320];
    static char strtab_storage[STSIZE * 16];
    struct elf_outsec out[MAXSEGM * 2 + 8];
    unsigned relcounts[MAXSEGM];
    Elf32_Sym *symtab;
    Elf32_Sym sym;
    Elf32_Ehdr ehdr;
    unsigned symcap, nsyms, first_global;
    unsigned nout, symtab_index, strtab_index, shstrtab_index;
    unsigned off, i, pos;
    int symndx[STSIZE];
    int s;

    memset(out, 0, sizeof(out));
    memset(relcounts, 0, sizeof(relcounts));
    memset(symndx, 0, sizeof(symndx));
    strtab_init_static(&shstrtab, shstrtab_storage, sizeof(shstrtab_storage));
    strtab_init_static(&strtab, strtab_storage, sizeof(strtab_storage));

    nout = 1;
    for (s = 0; s < maxsegm; s++) {
        if (!elf_section_should_output(s))
            continue;
        sections[s].shndx = nout;
        out[nout].kind = ESEC_INPUT;
        out[nout].src = s;
        out[nout].sh.sh_name = strtab_add(&shstrtab, sections[s].name);
        out[nout].sh.sh_type = sections[s].type;
        out[nout].sh.sh_flags = sections[s].flags;
        out[nout].sh.sh_size = count[s];
        out[nout].sh.sh_addralign = sections[s].align ? sections[s].align : 1;
        nout++;
    }

    symtab = 0;
    symcap = 0;
    nsyms = 0;
    memset(&sym, 0, sizeof(sym));
    append_elf_sym(&symtab, &nsyms, &symcap, &sym);
    for (s = 0; s < maxsegm; s++) {
        if (!sections[s].shndx)
            continue;
        memset(&sym, 0, sizeof(sym));
        sym.st_info = ELF_ST_INFO(STB_LOCAL, STT_SECTION);
        sym.st_shndx = sections[s].shndx;
        sections[s].symndx = nsyms;
        append_elf_sym(&symtab, &nsyms, &symcap, &sym);
    }
    for (i = 0; i < (unsigned)stabfree; i++) {
        if (elf_symbol_bind(i) != STB_LOCAL)
            continue;
        make_elf_symbol(i, &strtab, &sym);
        symndx[i] = nsyms;
        append_elf_sym(&symtab, &nsyms, &symcap, &sym);
    }
    first_global = nsyms;
    for (i = 0; i < (unsigned)stabfree; i++) {
        if (elf_symbol_bind(i) == STB_LOCAL)
            continue;
        make_elf_symbol(i, &strtab, &sym);
        symndx[i] = nsyms;
        append_elf_sym(&symtab, &nsyms, &symcap, &sym);
    }

    for (s = 0; s < maxsegm; s++) {
        char relname[300];

        relcounts[s] = elf_count_relocs(s);
        if (relcounts[s] == 0)
            continue;
        if (xstrlen(sections[s].name) + 5 >= sizeof(relname))
            uerror("section name too long");
        xstrcpy(relname, ".rel");
        xstrcat(relname, sections[s].name);
        out[nout].kind = ESEC_REL;
        out[nout].src = s;
        out[nout].sh.sh_name = strtab_add(&shstrtab, relname);
        out[nout].sh.sh_type = SHT_REL;
        out[nout].sh.sh_info = sections[s].shndx;
        out[nout].sh.sh_size = relcounts[s] * sizeof(Elf32_Rel);
        out[nout].sh.sh_addralign = WORDSZ;
        out[nout].sh.sh_entsize = sizeof(Elf32_Rel);
        nout++;
    }

    symtab_index = nout;
    out[nout].kind = ESEC_SYMTAB;
    out[nout].sh.sh_name = strtab_add(&shstrtab, ".symtab");
    out[nout].sh.sh_type = SHT_SYMTAB;
    out[nout].sh.sh_size = nsyms * sizeof(Elf32_Sym);
    out[nout].sh.sh_addralign = WORDSZ;
    out[nout].sh.sh_entsize = sizeof(Elf32_Sym);
    out[nout].sh.sh_info = first_global;
    nout++;

    strtab_index = nout;
    out[nout].kind = ESEC_STRTAB;
    out[nout].sh.sh_name = strtab_add(&shstrtab, ".strtab");
    out[nout].sh.sh_type = SHT_STRTAB;
    out[nout].sh.sh_size = strtab.len;
    out[nout].sh.sh_addralign = 1;
    nout++;

    shstrtab_index = nout;
    out[nout].kind = ESEC_SHSTRTAB;
    out[nout].sh.sh_name = strtab_add(&shstrtab, ".shstrtab");
    out[nout].sh.sh_type = SHT_STRTAB;
    out[nout].sh.sh_size = shstrtab.len;
    out[nout].sh.sh_addralign = 1;
    nout++;

    out[symtab_index].sh.sh_link = strtab_index;
    for (i = 1; i < nout; i++) {
        if (out[i].sh.sh_type == SHT_REL)
            out[i].sh.sh_link = symtab_index;
    }

    off = sizeof(Elf32_Ehdr);
    for (i = 1; i < nout; i++) {
        if (out[i].sh.sh_type == SHT_NOBITS) {
            out[i].sh.sh_offset = off;
            continue;
        }
        off = align_up(off, out[i].sh.sh_addralign);
        out[i].sh.sh_offset = off;
        off += out[i].sh.sh_size;
    }
    off = align_up(off, WORDSZ);

    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = ELFMAG0;
    ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2] = ELFMAG2;
    ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4] = ELFCLASS32;
    ehdr.e_ident[EI_DATA] = aout_is_big_endian() ? ELFDATA2MSB : ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = EM_MIPS;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_shoff = off;
    ehdr.e_flags = EF_MIPS_NOREORDER | EF_MIPS_ABI_O32;
    ehdr.e_flags |= mode_vr4300 ? EF_MIPS_ARCH_3 : EF_MIPS_ARCH_32R2;
    ehdr.e_ehsize = sizeof(Elf32_Ehdr);
    ehdr.e_shentsize = sizeof(Elf32_Shdr);
    ehdr.e_shnum = nout;
    ehdr.e_shstrndx = shstrtab_index;

    fseek(stdout, 0, SEEK_SET);
    elf_write_ehdr(&ehdr, stdout);
    pos = sizeof(Elf32_Ehdr);
    for (i = 1; i < nout; i++) {
        if (out[i].sh.sh_type == SHT_NOBITS)
            continue;
        emit_padding(&pos, out[i].sh.sh_offset);
        switch (out[i].kind) {
        case ESEC_INPUT:
            copy_temp_file(sfile[out[i].src], out[i].sh.sh_size);
            break;
        case ESEC_REL:
            elf_write_relocs(out[i].src, symndx);
            break;
        case ESEC_SYMTAB:
            for (s = 0; s < (int)nsyms; s++)
                elf_write_sym(&symtab[s], stdout);
            break;
        case ESEC_STRTAB:
            elf_write_bytes(strtab.data, strtab.len, stdout);
            break;
        case ESEC_SHSTRTAB:
            elf_write_bytes(shstrtab.data, shstrtab.len, stdout);
            break;
        }
        pos += out[i].sh.sh_size;
    }
    emit_padding(&pos, ehdr.e_shoff);
    for (i = 0; i < nout; i++)
        elf_write_shdr(&out[i].sh, stdout);

    free(symtab);
    strtab_free(&strtab);
    strtab_free(&shstrtab);
}

void pass2()
{
    register int i;
    register unsigned h;

    if (elf_output) {
        for (segm = 0; segm < maxsegm; segm++) {
            FILE *sfd, *rfd;

            if (!elf_section_should_output(segm) || !section_has_contents(segm))
                continue;
            sfd = fopen(tfilename, "w+");
            if (!sfd)
                uerror("cannot open %s", tfilename);
            unlink(tfilename);
            rfd = fopen(tfilename, "w+");
            if (!rfd)
                uerror("cannot open %s", tfilename);
            unlink(tfilename);

            rewind(sfile[segm]);
            rewind(rfile[segm]);
            for (h = 0; h < count[segm]; h += WORDSZ) {
                struct reloc relinfo;
                unsigned word = fgetword(sfile[segm]);
                fgetrel(rfile[segm], &relinfo);
                word = makeword(word, &relinfo, h);
                fputword(word, sfd);
                fputrel(&relinfo, rfd);
            }
            fclose(sfile[segm]);
            fclose(rfile[segm]);
            sfile[segm] = sfd;
            rfile[segm] = rfd;
        }
        makeelf();
        return;
    }

    tbase = 0;
    dbase = tbase + count[STEXT];
    adbase = dbase + count[SDATA];
    ctbase = adbase + count[SSTRNG];
    dtbase = ctbase + count[SCTORS];
    bbase = dtbase + count[SDTORS];

    /* Adjust indexes in symbol name */
    for (i = 0; i < stabfree; i++) {
        switch (stab[i].n_type & N_TYPE) {
        case N_UNDF:
        case N_ABS:
            break;
        case N_TEXT:
            stab[i].n_value += tbase;
            break;
        case N_DATA:
            stab[i].n_value += dbase;
            break;
        case N_STRNG:
            stab[i].n_value += adbase;
            stab[i].n_type += N_DATA - N_STRNG;
            break;
        case N_CTORS:
            stab[i].n_value += ctbase;
            break;
        case N_DTORS:
            stab[i].n_value += dtbase;
            break;
        case N_BSS:
            stab[i].n_value += bbase;
            break;
        }
    }
    fseek(stdout, sizeof(struct exec), 0);
    for (segm = STEXT; segm < SBSS; segm++) {
        /* Need to rewrite a relocation file. */
        FILE *rfd = fopen(tfilename, "w+");
        if (!rfd)
            uerror("cannot open %s", tfilename);
        unlink(tfilename);

        rewind(sfile[segm]);
        rewind(rfile[segm]);
        for (h = 0; h < count[segm]; h += WORDSZ) {
            struct reloc relinfo;
            unsigned word = fgetword(sfile[segm]);
            fgetrel(rfile[segm], &relinfo);
            word = makeword(word, &relinfo, h);
            fputword(word, stdout);
            fputrel(&relinfo, rfd);
        }
        fclose(rfile[segm]);
        rfile[segm] = rfd;
    }
}

/*
 * Convert symbol type to relocation type.
 */
int typerel(int t)
{
    switch (t & N_TYPE) {
    case N_ABS:
        return (RABS);
    case N_TEXT:
        return (RTEXT);
    case N_DATA:
        return (RDATA);
    case N_BSS:
        return (RBSS);
    case N_STRNG:
        return (RDATA);
    case N_CTORS:
        return (RCTORS);
    case N_DTORS:
        return (RDTORS);
    case N_UNDF:
    case N_COMM:
    case N_FN:
    default:
        return (0);
    }
}

/*
 * Relocate a relocation info word.
 * Remap symbol indexes.
 * Put string pseudo-section to data section.
 */
void relrel(struct reloc *relinfo)
{
    register unsigned type;

    switch ((int)relinfo->flags & REXT) {
    case RSTRNG:
        relinfo->flags &= ~RSMASK;
        relinfo->flags |= RDATA;
        break;
    case REXT:
        type = stab[relinfo->index].n_type;
        if (type == N_EXT + N_UNDF || type == N_EXT + N_COMM) {
            /* Reindexing */
            if (xflags)
                relinfo->index = newindex[relinfo->index];
        } else {
            relinfo->flags &= ~RSMASK;
            relinfo->flags |= typerel(type);
        }
        break;
    }
}

/*
 * Emit a relocation info for a given segment.
 * Copy it from scratch file to output.
 * Return a size of relocation data in bytes.
 */
unsigned makereloc(int s)
{
    register unsigned i, nbytes;
    struct reloc relinfo;

    if (count[s] <= 0)
        return 0;
    rewind(rfile[s]);
    nbytes = 0;
    for (i = 0; i < count[s]; i += WORDSZ) {
        fgetrel(rfile[s], &relinfo);
        relrel(&relinfo);
        nbytes += fputrel(&relinfo, stdout);
    }
    return nbytes;
}

/*
 * Align the relocation section to an integral number of words.
 */
unsigned alignreloc(unsigned nbytes)
{
    while (nbytes % WORDSZ) {
        putchar(0);
        nbytes++;
    }
    return nbytes;
}

void makesymtab()
{
    register int i;

    for (i = 0; i < stabfree; i++) {
        if (!xflags || (stab[i].n_type & N_EXT) ||
            is_rebsd_metadata_symbol(&stab[i]) ||
            (Xflag && stab[i].n_name[0] != 'L')) {
            fputsym(&stab[i], stdout);
        }
    }
    while (stalign--)
        putchar(0);
}

void usage()
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  as [--elf|--aout] [-gkuvxX] [-O[level]] [-EL|-EB] [-o outfile] [infile]\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o filename     Set output file name, default a.out\n");
    fprintf(stderr, "  -u              Treat undefined names as error\n");
    fprintf(stderr, "  -x              Discard local symbols\n");
    fprintf(stderr, "  -X              Discard locals starting with 'L' or '.'\n");
    fprintf(stderr, "  -EL, -EB        Select output byte order\n");
    fprintf(stderr, "  --elf           Write ELF32 MIPS relocatable object\n");
    fprintf(stderr, "  --aout          Write legacy ReBSD a.out relocatable object\n");
    fprintf(stderr, "  -O2             Safely fill a restricted set of branch delay slots\n");
    fprintf(stderr, "  -mips3, -march=vr4300\n");
    fprintf(stderr, "                  Select VR4300 ISA checks and 8-byte text alignment\n");
    fprintf(stderr, "  -mips32r2, -march=mips32r2\n");
    fprintf(stderr, "                  Select MIPS32R2 mode and 4-byte text alignment\n");
    exit(1);
}

static void target_info(void)
{
    printf("rebsd-as target_big_endian=%d target_vr4300_default=%d elf_default=%d\n",
#ifdef TARGET_BIG_ENDIAN
        1,
#else
        0,
#endif
#ifdef TARGET_VR4300
        1,
#else
        0,
#endif
#ifdef REBSD_TOOLCHAIN_ELF_DEFAULT
        1
#else
        0
#endif
    );
}

int main(int argc, char *argv[])
{
    register int i;
    register char *cp;
    int ofile = 0;
    unsigned rtsize, rdsize;

#ifdef TARGET_BIG_ENDIAN
    aout_set_big_endian(1);
#else
    aout_set_big_endian(0);
#endif
#ifdef TARGET_VR4300
    set_cpu_vr4300(1);
#endif
#ifdef REBSD_TOOLCHAIN_ELF_DEFAULT
    elf_output = 1;
#endif

    /*
     * Parse options.
     */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--target-info") == 0) {
            target_info();
            return 0;
        }
        if (strcmp(argv[i], "--elf") == 0) {
            elf_output = 1;
            continue;
        }
        if (strcmp(argv[i], "--aout") == 0) {
            elf_output = 0;
            continue;
        }
        if (strcmp(argv[i], "-march=vr4300") == 0 ||
            strcmp(argv[i], "-mips3") == 0) {
            set_cpu_vr4300(1);
            continue;
        }
        if (strncmp(argv[i], "-march=mips32", 13) == 0 ||
            strncmp(argv[i], "-mips32", 7) == 0) {
            set_cpu_vr4300(0);
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] == 'O') {
            optimize_level = 1;
            cp = argv[i] + 2;
            if (*cp) {
                optimize_level = 0;
                while (*cp >= '0' && *cp <= '9') {
                    optimize_level = optimize_level * 10 + (*cp - '0');
                    cp++;
                }
                if (*cp)
                    uerror("bad optimization level %s", argv[i]);
            }
            continue;
        }
        switch (argv[i][0]) {
        case '-':
            for (cp = argv[i] + 1; *cp; cp++) {
                switch (*cp) {
                case 'X': /* strip L* and .* locals */
                    Xflag++;
                case 'x': /* strip local symbols */
                    xflags++;
                    break;
                case 'u': /* treat undefines as error */
                    uflag++;
                    break;
                case 'o': /* output file name */
                    if (ofile)
                        uerror("too many -o flags");
                    ofile = 1;
                    if (cp[1]) {
                        /* -ofile */
                        outfile = cp + 1;
                        while (*++cp)
                            ;
                        --cp;
                    } else if (i + 1 < argc)
                        /* -o file */
                        outfile = argv[++i];
                    break;
                case 'v': /* verbose mode */
                    // TODO
                    break;
                case 'g': /* debug mode */
                    // TODO
                    break;
                case 'k': /* PIC compatibility option from PCC */
                    break;
                case 'I': /* include dir */
                    // TODO
                    if (cp[1] == 0) {
                        i++;
                    } else {
                        while (*++cp)
                            ;
                        --cp;
                    }
                    break;
                case '-': /* long option - skip */
                    while (*++cp)
                        ;
                    --cp;
                    break;
                case 'n': /* -no-xyz option - skip */
                    while (*++cp)
                        ;
                    --cp;
                    break;
                case 'm': /* -mips32r2, -mabi=32, -march=vr4300 */
                    if (strncmp(cp, "march=vr4300", 12) == 0)
                        set_cpu_vr4300(1);
                    else if (strncmp(cp, "mips3", 5) == 0)
                        set_cpu_vr4300(1);
                    else if (strncmp(cp, "mips32", 6) == 0 ||
                             strncmp(cp, "march=mips32", 12) == 0)
                        set_cpu_vr4300(0);
                    while (*++cp)
                        ;
                    --cp;
                    break;
                case 'E': /* -EL, -EB - endianness */
                    if (cp[1] == 'L')
                        aout_set_big_endian(0);
                    else if (cp[1] == 'B')
                        aout_set_big_endian(1);
                    else
                        uerror("bad endian option");
                    while (*++cp)
                        ;
                    --cp;
                    break;
                default:
                    fprintf(stderr, "Unknown option: %s\n", cp);
                    usage();
                }
            }
            break;
        default:
            if (infile)
                uerror("too many input files");
            infile = argv[i];
            break;
        }
    }
    if (!infile && isatty(0))
        usage();

    /*
     * Setup input-output.
     */
    if (infile && !freopen(infile, "r", stdin))
        uerror("cannot open %s", infile);
    if (!freopen(outfile, "w", stdout))
        uerror("cannot open %s", outfile);

    startup();                 /* Open temporary files */
    hashinit();                /* Initialize hash tables */
    pass1();                   /* First pass */
    middle();                  /* Prepare symbol table */
    pass2();                   /* Second pass */
    if (elf_output)
        return 0;
    rtsize = makereloc(STEXT); /* Emit relocation info: text */
    rtsize = alignreloc(rtsize);
    rdsize = makereloc(SDATA);    /* data */
    rdsize += makereloc(SSTRNG);  /* rodata */
    rdsize += makereloc(SCTORS);  /* constructors */
    rdsize += makereloc(SDTORS);  /* destructors */
    rdsize = alignreloc(rdsize);
    makesymtab();               /* Emit symbol table */
    makeheader(rtsize, rdsize); /* Write a.out header */
    return 0;
}
