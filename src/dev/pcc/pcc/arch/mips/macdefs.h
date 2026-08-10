/*	$Id$	*/
/*
 * Copyright (c) 2003 Anders Magnusson (ragge@ludd.luth.se).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * MIPS port by Jan Enoksson (janeno-1@student.ltu.se) and
 * Simon Olsson (simols-1@student.ltu.se) 2005.
 */

/*
 * Machine-dependent defines for both passes.
 */

#if defined(os_netbsd) || defined(os_litebsd)
#define USE_GAS
#endif

#define MIPS_CPU_VR4300		1
#define MIPS_CPU_MIPS32R2	2

enum mips_isa {
	MIPS_ISA_I,
	MIPS_ISA_II,
	MIPS_ISA_III,
	MIPS_ISA_MIPS32R1,
	MIPS_ISA_MIPS32R2
};

enum mips_tune {
	MIPS_TUNE_GENERIC,
	MIPS_TUNE_VR4300,
	MIPS_TUNE_R4000,
	MIPS_TUNE_24KC,
	MIPS_TUNE_34KC,
	MIPS_TUNE_74KC,
	MIPS_TUNE_JZ4780
};

#define MIPS_CAP_MUL3			0x00000001U
#define MIPS_CAP_ROTR			0x00000002U
#define MIPS_CAP_CLZ			0x00000004U
#define MIPS_CAP_SEB_SEH		0x00000008U
#define MIPS_CAP_WSBH			0x00000010U
#define MIPS_CAP_EXT_INS		0x00000020U
#define MIPS_CAP_MOVN_MOVZ		0x00000040U
#define MIPS_CAP_64BIT_GPR		0x00000080U
#define MIPS_CAP_HILO			0x00000100U
#define MIPS_CAP_INT_LOAD_INTERLOCK	0x00000200U
#define MIPS_CAP_FP_LOAD_INTERLOCK	0x00000400U
#define MIPS_CAP_BRANCH_DELAY		0x00000800U
#define MIPS_CAP_VR4300_FMUL_ERRATUM	0x00001000U

#define MIPS_CAPS_BASE	(MIPS_CAP_HILO | MIPS_CAP_BRANCH_DELAY)
#define MIPS_CAPS_MIPS3	(MIPS_CAPS_BASE | MIPS_CAP_64BIT_GPR)
#define MIPS_CAPS_MIPS32R2	(MIPS_CAPS_BASE | MIPS_CAP_MUL3 | \
	MIPS_CAP_ROTR | MIPS_CAP_CLZ | MIPS_CAP_SEB_SEH | MIPS_CAP_WSBH | \
	MIPS_CAP_EXT_INS | MIPS_CAP_MOVN_MOVZ | \
	MIPS_CAP_INT_LOAD_INTERLOCK | MIPS_CAP_FP_LOAD_INTERLOCK)

struct mips_costs {
	unsigned int_mult;
	unsigned int_div;
	unsigned int64_mult;
	unsigned int64_div;
	unsigned fp_add;
	unsigned fp_mul_s;
	unsigned fp_mul_d;
	unsigned fp_div_s;
	unsigned fp_div_d;
	unsigned int_load_use;
	unsigned fp_load_use;
	unsigned branch_delay;
};

/* Generic entries are relative costs; VR4300 entries are documented cycles. */
#define MIPS_COSTS_GENERIC_MIPS3 \
	{ 10, 40, 20, 72, 4, 8, 12, 36, 68, 1, 1, 1 }
#define MIPS_COSTS_VR4300 \
	{ 5, 37, 8, 69, 3, 5, 8, 29, 58, 1, 1, 1 }
#define MIPS_COSTS_GENERIC_MIPS32R2 \
	{ 5, 35, 8, 70, 4, 5, 8, 20, 35, 1, 1, 1 }

struct mips_target {
	enum mips_isa isa;
	enum mips_tune tune;
	unsigned capabilities;
	struct mips_costs costs;
	int little_endian;
	int hard_float;
	int fix_vr4300;
};

#if defined(os_rebsd)
#define MIPS_HARDFLOAT_O32_ABI 1
#define TARGET_NO_ABICALLS
#define TARGET_NO_REORDER
#define TARGET_OMIT_FRAME_POINTER_AT_O2() \
	(mips_target.isa == MIPS_ISA_MIPS32R2 || \
	 mips_target.tune == MIPS_TUNE_VR4300)
#ifndef MIPS_CPU_DEFAULT
#define MIPS_CPU_DEFAULT	MIPS_CPU_VR4300
#endif
#ifndef MIPS_SOFT_FLOAT_DEFAULT
#define MIPS_SOFT_FLOAT_DEFAULT	0
#endif
#ifndef MIPS_FIX4300_DEFAULT
#define MIPS_FIX4300_DEFAULT	(MIPS_CPU_DEFAULT == MIPS_CPU_VR4300)
#endif
#endif
#ifndef MIPS_SOFT_FLOAT_DEFAULT
#define MIPS_SOFT_FLOAT_DEFAULT	0
#endif
#ifndef MIPS_FIX4300_DEFAULT
#define MIPS_FIX4300_DEFAULT	0
#endif

#if defined(MIPS_CPU_DEFAULT) && MIPS_CPU_DEFAULT == MIPS_CPU_MIPS32R2
#define MIPS_DEFAULT_ISA	MIPS_ISA_MIPS32R2
#define MIPS_DEFAULT_TUNE	MIPS_TUNE_GENERIC
#define MIPS_DEFAULT_CAPS	MIPS_CAPS_MIPS32R2
#define MIPS_DEFAULT_COSTS	MIPS_COSTS_GENERIC_MIPS32R2
#elif defined(MIPS_CPU_DEFAULT)
#define MIPS_DEFAULT_ISA	MIPS_ISA_III
#define MIPS_DEFAULT_TUNE	MIPS_TUNE_VR4300
#define MIPS_DEFAULT_CAPS	(MIPS_CAPS_MIPS3 | \
	MIPS_CAP_INT_LOAD_INTERLOCK | MIPS_CAP_FP_LOAD_INTERLOCK | \
	MIPS_CAP_VR4300_FMUL_ERRATUM)
#define MIPS_DEFAULT_COSTS	MIPS_COSTS_VR4300
#else
#define MIPS_DEFAULT_ISA	MIPS_ISA_I
#define MIPS_DEFAULT_TUNE	MIPS_TUNE_GENERIC
#define MIPS_DEFAULT_CAPS	MIPS_CAPS_BASE
#define MIPS_DEFAULT_COSTS	MIPS_COSTS_GENERIC_MIPS3
#endif

#ifdef TARGET_BIG_ENDIAN
#define MIPS_DEFAULT_LITTLE_ENDIAN	0
#else
#define MIPS_DEFAULT_LITTLE_ENDIAN	1
#endif

#define MIPS_TARGET_INITIALIZER { MIPS_DEFAULT_ISA, MIPS_DEFAULT_TUNE, \
	MIPS_DEFAULT_CAPS, MIPS_DEFAULT_COSTS, MIPS_DEFAULT_LITTLE_ENDIAN, \
	!MIPS_SOFT_FLOAT_DEFAULT, MIPS_FIX4300_DEFAULT }

#if defined(os_rebsd)
#define MIPS_DATA_ALIGN64	64
/* MIPS o32 gives 64-bit integer and floating-point objects 8-byte alignment. */
#define MIPS_INT64_ARG_ALIGN	MIPS_DATA_ALIGN64
#define MIPS_FP64_ARG_ALIGN	64
#define MIPS_FIX4300_ACTIVE	((mips_target.capabilities & \
	MIPS_CAP_VR4300_FMUL_ERRATUM) != 0 && mips_target.fix_vr4300)
#endif
#ifndef MIPS_DATA_ALIGN64
#define MIPS_DATA_ALIGN64	64
#endif
#ifndef MIPS_INT64_ARG_ALIGN
#define MIPS_INT64_ARG_ALIGN	64
#endif
#ifndef MIPS_FP64_ARG_ALIGN
#define MIPS_FP64_ARG_ALIGN	64
#endif
#ifndef MIPS_FIX4300_ACTIVE
#define MIPS_FIX4300_ACTIVE	0
#endif

/*
 * Convert (multi-)character constant to integer.
 * Assume: If only one value; store at left side (char size), otherwise 
 * treat it as an integer.
 */
#define makecc(val,i)	lastcon = (lastcon<<8)|((val<<24)>>24);

#define ARGINIT		(16*8)	/* # bits above fp where arguments start */
#define AUTOINIT	(0)	/* # bits below fp where automatics start */

/*
 * Storage space requirements
 */
#define SZCHAR		8
#define SZBOOL		32
#define SZINT		32
#define SZFLOAT		32
#define SZDOUBLE	64
#define SZLDOUBLE	64
#define SZLONG		32
#define SZSHORT		16
#define SZLONGLONG	64
#define SZPOINT(t)	32

/*
 * Alignment constraints
 */
#define ALCHAR		8
#define ALBOOL		32
#define ALINT		32
#define ALFLOAT		32
#if defined(os_rebsd)
#define ALDOUBLE	MIPS_DATA_ALIGN64
#define ALLDOUBLE	MIPS_DATA_ALIGN64
#else
#define ALDOUBLE	64
#define ALLDOUBLE	64
#endif
#define ALLONG		32
#if defined(os_rebsd)
#define ALLONGLONG	MIPS_DATA_ALIGN64
#else
#define ALLONGLONG	64
#endif
#define ALSHORT		16
#define ALPOINT		32
#if defined(os_rebsd)
#define ALSTRUCT	MIPS_DATA_ALIGN64
#else
#define ALSTRUCT	64
#endif
#define ALSTACK		32 

/*
 * Min/max values.
 */
#define	MIN_CHAR	-128
#define	MAX_CHAR	127
#define	MAX_UCHAR	255
#define	MIN_SHORT	-32768
#define	MAX_SHORT	32767
#define	MAX_USHORT	65535
#define	MIN_INT		(-0x7fffffff-1)
#define	MAX_INT		0x7fffffff
#define	MAX_UNSIGNED	0xffffffffU
#define	MIN_LONG	MIN_INT
#define	MAX_LONG	0x7fffffffL
#define	MAX_ULONG	0xffffffffUL
#define	MIN_LONGLONG	(-0x7fffffffffffffffLL-1)
#define	MAX_LONGLONG	0x7fffffffffffffffLL
#define	MAX_ULONGLONG	0xffffffffffffffffULL

#undef	CHAR_UNSIGNED
#define BOOL_TYPE	INT

/*
 * Use large-enough types.
 */
typedef	long long CONSZ;
typedef	unsigned long long U_CONSZ;
#if defined(os_rebsd)
typedef long OFFSZ;
#else
typedef long long OFFSZ;
#endif

#define CONFMT	"%lld"		/* format for printing constants */
#ifdef USE_GAS
#define LABFMT	"$L%d"		/* format for printing labels */
#define	STABLBL	"$LL%d"		/* format for stab (debugging) labels */
#else
#define LABFMT	"L%d"		/* format for printing labels */
#define	STABLBL	"LL%d"		/* format for stab (debugging) labels */
#endif

#define STACK_DOWN 		/* stack grows negatively for automatics */

#undef	FIELDOPS		/* no bit-field instructions */
#ifdef TARGET_BIG_ENDIAN
#define TARGET_ENDIAN TARGET_BE
#else
#define TARGET_ENDIAN TARGET_LE
#endif
#define	MYALIGN

/* Definitions mostly used in pass2 */

#define BYTEOFF(x)	((x)&03)

#define	szty(t)		(((t) == DOUBLE || (t) == LDOUBLE || \
	DEUNSIGN(t) == LONGLONG) ? 2 : 1)

/*
 * Register names.  These must match rnames[] and rstatus[] in local2.c.
 */
#define ZERO	0
#define AT	1
#define V0	2
#define V1	3
#define A0	4
#define A1	5
#define A2	6
#define A3	7
#define A4	8
#define A5	9
#define A6	10
#define A7	11
#if defined(MIPS_N32) || defined(MIPS_N64)
#define T0	12
#define T1	13
#define	T2	14
#define	T3	15
#else
#define	T0	8
#define	T1	9
#define	T2	10
#define	T3	11
#endif
#define	T4	12
#define	T5	13
#define	T6	14
#define	T7	15
#define S0	16
#define S1	17
#define S2	18
#define S3	19
#define S4	20
#define S5	21
#define S6	22
#define S7	23
#define T8	24
#define T9	25
#define K0	26
#define K1	27
#define GP	28
#define SP	29
#define FP	30
#define RA	31

#define V0V1	32
#define A0A1	33
#define A1A2	34
#define A2A3	35

/* we just use o32 naming here, but it works ok for n32/n64 */
#define A3T0	36
#define T0T1	37
#define T1T2	38
#define T2T3	39
#define T3T4	40
#define T4T5	41
#define T5T6	42
#define T6T7	43
#define T7T8	44

#define T8T9	45
#define S0S1	46
#define S1S2	47
#define S2S3	48
#define S3S4	49
#define S4S5	50
#define S5S6	51
#define S6S7	52

#define F0	53
#define F2	54
#define F4	55
#define F6	56
#define F8	57
#define F10	58
#define F12	59
#define F14	60
#define F16	61
#define F18	62
#define F20	63
/* and the rest for later */
#define F22	64
#define F24	65
#define F26	66
#define F28	67
#define F30	68

#define MAXREGS 64
#define NUMCLASS 3

#define RETREG(x)	(mips_soft_float && (x) == FLOAT ? V0 : \
			    mips_soft_float && ((x) == DOUBLE || \
			    (x) == LDOUBLE || DEUNSIGN(x) == LONGLONG) ? \
			    V0V1 : \
			    DEUNSIGN(x) == LONGLONG ? V0V1 : \
			    (x) == DOUBLE || (x) == LDOUBLE || (x) == FLOAT ? \
			    F0 : V0)
#define FPREG	FP	/* frame pointer */

#define MIPS_N32_NARGREGS	8
#define MIPS_O32_NARGREGS	4

#define RSTATUS \
	0, 0,								\
	SAREG|TEMPREG, SAREG|TEMPREG, 					\
	SAREG|TEMPREG, SAREG|TEMPREG, SAREG|TEMPREG, SAREG|TEMPREG,	\
	SAREG|TEMPREG, SAREG|TEMPREG, SAREG|TEMPREG, SAREG|TEMPREG,	\
	SAREG|TEMPREG, SAREG|TEMPREG, SAREG|TEMPREG, SAREG|TEMPREG,	\
	SAREG|PERMREG, SAREG|PERMREG, SAREG|PERMREG, SAREG|PERMREG,	\
	SAREG|PERMREG, SAREG|PERMREG, SAREG|PERMREG, SAREG|PERMREG,	\
	SAREG|TEMPREG, SAREG|TEMPREG, 					\
	0, 0,								\
	0, 0, 0, 0,							\
	\
	SBREG|TEMPREG,							\
	SBREG|TEMPREG, SBREG|TEMPREG, SBREG|TEMPREG,			\
 	SBREG|TEMPREG,							\
	SBREG|TEMPREG, SBREG|TEMPREG, SBREG|TEMPREG,			\
	SBREG|TEMPREG, SBREG|TEMPREG,					\
	SBREG|TEMPREG, SBREG|TEMPREG, SBREG|TEMPREG, SBREG|TEMPREG,	\
	SBREG, SBREG, SBREG, SBREG,					\
	SBREG, SBREG, SBREG, 						\
	SCREG|TEMPREG, SCREG|TEMPREG, SCREG|TEMPREG, SCREG|TEMPREG,	\
	SCREG|TEMPREG, SCREG|TEMPREG, SCREG|TEMPREG, SCREG|TEMPREG,	\
	SCREG|TEMPREG, SCREG|TEMPREG, 0,					\

#define ROVERLAP \
	{ -1 },				/* $zero */			\
	{ -1 },				/* $at */			\
	{ V0V1, -1 },			/* $v0 */			\
	{ V0V1, -1 },			/* $v1 */			\
	{ A0A1, -1 },			/* $a0 */			\
	{ A0A1, A1A2, -1 },		/* $a1 */			\
	{ A1A2, A2A3, -1 },		/* $a2 */			\
	{ A2A3, A3T0, -1 },		/* $a3 */			\
	{ A3T0, T0T1, -1 },		/* $t0 */			\
	{ T0T1, T1T2, -1 },		/* $t1 */			\
	{ T1T2, T2T3, -1 },		/* $t2 */			\
	{ T2T3, T3T4, -1 },		/* $t3 */			\
	{ T3T4, T4T5, -1 },		/* $t4 */			\
	{ T4T5, T5T6, -1 },		/* $t5 */			\
	{ T5T6, T6T7, -1 },		/* $t6 */			\
	{ T6T7, T7T8, -1 },		/* $t7 */			\
	\
	{ S0S1, -1 },			/* $s0 */			\
	{ S0S1, S1S2, -1 },		/* $s1 */			\
	{ S1S2, S2S3, -1 },		/* $s2 */			\
	{ S2S3, S3S4, -1 },		/* $s3 */			\
	{ S3S4, S4S5, -1 },		/* $s4 */			\
	{ S4S5, S5S6, -1 },		/* $s5 */			\
	{ S5S6, S6S7, -1 },		/* $s6 */			\
	{ S6S7, -1 },			/* $s7 */			\
	\
	{ T7T8, T8T9, -1 },		/* $t8 */			\
	{ T8T9, -1 },			/* $t9 */			\
	\
	{ -1 },				/* $k0 */			\
	{ -1 },				/* $k1 */			\
	{ -1 },				/* $gp */			\
	{ -1 },				/* $sp */			\
	{ -1 },				/* $fp */			\
	{ -1 },				/* $ra */			\
	\
	{ V0, V1, -1 },			/* $v0:$v1 */			\
	\
	{ A0, A1, A1A2, -1 },		/* $a0:$a1 */			\
	{ A1, A2, A0A1, A2A3, -1 },	/* $a1:$a2 */			\
	{ A2, A3, A1A2, A3T0, -1 },	/* $a2:$a3 */			\
	{ A3, T0, A2A3, T0T1, -1 },	/* $a3:$t0 */			\
	{ T0, T1, A3T0, T1T2, -1 },	/* $t0:$t1 */			\
	{ T1, T2, T0T1, T2T3, -1 },	/* $t1:$t2 */			\
	{ T2, T3, T1T2, T3T4, -1 },	/* $t2:$t3 */			\
	{ T3, T4, T2T3, T4T5, -1 },	/* $t3:$t4 */			\
	{ T4, T5, T3T4, T5T6, -1 },	/* $t4:$t5 */			\
	{ T5, T6, T4T5, T6T7, -1 },	/* $t5:$t6 */			\
	{ T6, T7, T5T6, T7T8, -1 },	/* $t6:$t7 */			\
	{ T7, T8, T6T7, T8T9, -1 },	/* $t7:$t8 */			\
	{ T8, T9, T7T8, -1 },		/* $t8:$t9 */			\
	\
	{ S0, S1, S1S2, -1 },		/* $s0:$s1 */			\
	{ S1, S2, S0S1, S2S3, -1 },					\
	{ S2, S3, S1S2, S3S4, -1 },					\
	{ S3, S4, S2S3, S4S5, -1 },					\
	{ S4, S5, S3S4, S5S6, -1 },					\
	{ S5, S6, S4S5, S6S7, -1 },					\
	{ S6, S7, S5S6, -1 },						\
	\
	{ -1 }, { -1 }, { -1 }, { -1 },					\
	{ -1 }, { -1 }, { -1 }, { -1 },					\
	{ -1 }, { -1 }, { -1 }, 					\

#define GCLASS(x)	(x < 32 ? CLASSA : (x < 53 ? CLASSB : CLASSC))
#define TARGET_OPTSTATS_FPR_CLASS(c)	((c) == CLASSC)
#define TARGET_SSA_STRENGTH_REDUCE_MUL()	\
	((mips_target.capabilities & MIPS_CAP_MUL3) == 0)
#define TARGET_SSA_STRENGTH_REDUCE_MASKED_CONST_MUL()	\
	(mips_target.isa == MIPS_ISA_MIPS32R2 || \
	 mips_target.tune == MIPS_TUNE_VR4300)
#define TARGET_SSA_STRENGTH_REDUCE_ADDRESS()	\
	(mips_target.isa == MIPS_ISA_MIPS32R2 || \
	 mips_target.tune == MIPS_TUNE_VR4300)
#define TARGET_SSA_STRENGTH_REDUCE_ADDRESS_TYPE(t)	\
	(ISPTR(t) && ((BTYPE(DECREF(t)) != FLOAT && \
	 BTYPE(DECREF(t)) != DOUBLE && BTYPE(DECREF(t)) != LDOUBLE) || \
	 (!mips_soft_float && mips_target.tune == MIPS_TUNE_VR4300)))
#define TARGET_SSA_STRENGTH_REDUCE_SINGLE_ADDRESS(t)	\
	(ISPTR(t) && !mips_soft_float && \
	 mips_target.tune == MIPS_TUNE_VR4300 && \
	 (BTYPE(DECREF(t)) == FLOAT || BTYPE(DECREF(t)) == DOUBLE || \
	 BTYPE(DECREF(t)) == LDOUBLE))
#define TARGET_SSA_STRENGTH_REDUCE_SINGLE_SYMBOL_ADDRESS(t)	\
	TARGET_SSA_STRENGTH_REDUCE_ADDRESS_TYPE(t)
#define TARGET_SSA_CSE_CONST_SHIFT()	\
	(!mips_soft_float && (mips_target.isa == MIPS_ISA_MIPS32R2 || \
	 mips_target.tune == MIPS_TUNE_VR4300))
#define TARGET_SSA_LOWER_COUNTED_LOOP()	\
	(mips_target.isa == MIPS_ISA_MIPS32R2 || \
	 mips_target.tune == MIPS_TUNE_VR4300)
#define PCLASS(p)	(1 << gclass((p)->n_type))
#define DECRA(x,y)	(((x) >> (y*6)) & 63)   /* decode encoded regs */
#define ENCRA(x,y)	((x) << (6+y*6))        /* encode regs in int */
#define ENCRD(x)	(x)			/* Encode dest reg in n_reg */

int COLORMAP(int c, int *r);
int features(int f);

extern int bigendian;
extern struct mips_target mips_target;
extern int mips_soft_float;
extern int nargregs;

void mips_target_set_isa(struct mips_target *, enum mips_isa);
void mips_target_set_tune(struct mips_target *, enum mips_tune);
const char *mips_target_error(const struct mips_target *);

#define FEATURE_HARDFLOAT	0x00010000
#define FEATURE_SOFTFLOAT	0x00020000
#define FEATURE_MIPS32R2	0x00040000
#define FEATURE_FIX4300		0x00080000
#define FEATURE_NOFIX4300	0x00100000
#define FEATURE_VR4300		0x00200000

#define TARGET_PARTIAL_STATIC_SPECIALIZATION() \
	(!mips_soft_float && (mips_target.isa == MIPS_ISA_MIPS32R2 || \
	 mips_target.tune == MIPS_TUNE_VR4300))

#define SPCON           (MAXSPECIAL+1)  /* positive constant */
#define SPOW2CON        (MAXSPECIAL+2)  /* positive power-of-two constant */
#define SSHADDCON       (MAXSPECIAL+3)  /* cheap shift-add multiply constant */
#define SPUDIVCON       (MAXSPECIAL+4)  /* unsigned magic division constant */
#define SPSDIVCON       (MAXSPECIAL+5)  /* signed magic division constant */
#define SPUNUSEDSPECARG (MAXSPECIAL+6)  /* specialized literal call argument */
#define SPARGREG        (MAXSPECIAL+7)  /* o32 integer argument register */

#define TARGET_STDARGS
#ifndef LANG_CXX
#define TARGET_FABS mips_builtin_fabs
#define MIPS_FABS_BUILTINS						\
	{ "fabs", mips_builtin_fabs, 0, 1, fmaxt, DOUBLE },
#else
#define MIPS_FABS_BUILTINS
#endif
#define TARGET_BUILTINS							\
	{ "__builtin_stdarg_start", mips_builtin_stdarg_start,	       \
						0, 2, 0, VOID },	\
	{ "__builtin_va_start", mips_builtin_stdarg_start,	       \
						0, 2, 0, VOID },	\
	{ "__builtin_va_arg", mips_builtin_va_arg, BTNORVAL|BTNOPROTO, \
							2, 0, 0 },	\
	{ "__builtin_va_end", mips_builtin_va_end, 0, 1, 0, VOID },    \
	{ "__builtin_va_copy", mips_builtin_va_copy, 0, 2, 0, VOID }, \
	MIPS_FABS_BUILTINS

#ifdef LANG_CXX
#define P1ND struct node
#else
#define P1ND struct p1node
#endif
struct node;
struct bitable;
P1ND *mips_builtin_stdarg_start(const struct bitable *, P1ND *a);
P1ND *mips_builtin_va_arg(const struct bitable *, P1ND *a);
P1ND *mips_builtin_va_end(const struct bitable *, P1ND *a);
P1ND *mips_builtin_va_copy(const struct bitable *, P1ND *a);
#ifndef LANG_CXX
P1ND *mips_builtin_fabs(const struct bitable *, P1ND *a);
#endif
#undef P1ND

void mips_xasm_targarg(char *, void *, int);
#define XASM_TARGARG(w, ary) \
	(w[1] == 'H' ? w++, mips_xasm_targarg(w, ary, n), 1 : 0)

/* floating point definitions */
#define USE_IEEEFP_32
#define FLT_PREFIX	IEEEFP_32
#define USE_IEEEFP_64
#define DBL_PREFIX	IEEEFP_64
#define LDBL_PREFIX	IEEEFP_64
#define DEFAULT_FPI_DEFS { &fpi_binary32, &fpi_binary64, &fpi_binary64 }
