/*	$Id$	 */
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

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "pass1.h"
#include "pass2.h"

#ifdef TARGET_BIG_ENDIAN
int bigendian = 1;
#else
int bigendian = 0;
#endif

#ifdef MIPS_CPU_DEFAULT
int mips_cpu = MIPS_CPU_DEFAULT;
#else
int mips_cpu = 0;
#endif
#ifdef MIPS_FIX4300_DEFAULT
int mips_fix4300 = MIPS_FIX4300_DEFAULT;
#else
int mips_fix4300 = 0;
#endif
static int mips_fix4300_explicit;
int mips_soft_float = MIPS_SOFT_FLOAT_DEFAULT;
int nargregs = MIPS_O32_NARGREGS;

static int funargpushsiz(NODE *p);
static void print_reg64name(FILE *fp, int rval, int hi);
static void adrput_lowpart(FILE *io, NODE *p, TWORD dst);
static void ucmpbr(NODE *p);
static int mips_is_soft_fp64(TWORD t);
static int mips_split_hardfp64_mem(void);
static void mips_hardfp64_load(NODE *p);
static void mips_hardfp64_store(NODE *p);

void
deflab(int label)
{
	printf(LABFMT ":\n", label);
}

static int regoff[32];
static TWORD ftype;
static int mips_frame_adjust;
static int mips_omit_fp;
static int mips_leaf_function;

static int
mips_can_omit_fp(struct interpass_prolog *ipp)
{
	return xomitframe && (ipp->ipp_flags & IF_NEEDFP) == 0;
}

static int
mips_is_leaf(struct interpass_prolog *ipp)
{
	return (ipp->ipp_flags & IF_NOTLEAF) == 0;
}

static void
mips_adjust_frame_ref(CONSZ *off, int *base)
{
	if (mips_omit_fp && *base == FPREG) {
		*off += mips_frame_adjust;
		*base = SP;
	}
}

static void
mips_leaf_ra_slot(const char *op)
{
	CONSZ off;
	int base;

	if (!mips_leaf_function)
		return;

	base = mips_omit_fp ? SP : FP;
	off = (mips_omit_fp ? mips_frame_adjust : 0) + 4;

	if (off >= -32768 && off <= 32767) {
		printf("\t%s %s," CONFMT "(%s)\n", op, rnames[RA], off,
		    rnames[base]);
		return;
	}

	printf("\tli %s," CONFMT "\n", rnames[AT], off);
	printf("\taddu %s,%s,%s\n", rnames[AT], rnames[AT], rnames[base]);
	printf("\t%s %s,0(%s)\n", op, rnames[RA], rnames[AT]);
}

/*
 * calculate stack size and offsets
 */
static int
offcalc(struct interpass_prolog * ipp, int omitfp)
{
	int i, j, addto;

	(void)ipp;
	(void)omitfp;
	memset(regoff, 0, sizeof(regoff));
	addto = p2maxautooff;
	SETOFF(addto, SZINT / SZCHAR);

	for (i = p2env.p_regs[0], j = 0; i; i >>= 1, j++) {
		if (i & 1) {
			addto += SZINT / SZCHAR;
			regoff[j] = addto;
		}
	}

        /* round to 8-byte boundary */
        addto += 7;
        addto &= ~7;

	return addto;
}

/*
 * Print out the prolog assembler.
 */
void
prologue(struct interpass_prolog * ipp)
{
	int addto;
	int i, j;
	int leaf;

	ftype = ipp->ipp_type;
	printf("\t.align 2\n");
	if (ipp->ipp_flags & IF_VISIBLE)
		printf("\t.globl %s\n", ipp->ipp_name);
	printf("\t.ent %s\n", ipp->ipp_name);
	printf("%s:\n", ipp->ipp_name);

	mips_omit_fp = mips_can_omit_fp(ipp);
	leaf = mips_is_leaf(ipp);
	mips_leaf_function = leaf;
	addto = offcalc(ipp, mips_omit_fp);
	mips_frame_adjust = addto;

#ifndef TARGET_NO_ABICALLS
	/* emit PIC only if -fpic or -fPIC set */
	if (kflag > 0) {
		printf("\t.frame %s,%d,%s\n",
		    rnames[FP], ARGINIT/SZCHAR, rnames[RA]);
		printf("\t.set noreorder\n");
		printf("\t.cpload $25\t# pseudo-op to load GOT ptr into $25\n");
		printf("\t.set reorder\n");
	}
#endif

	printf("\t.frame %s,%d,%s\n",
	    rnames[mips_omit_fp ? SP : FP], ARGINIT/SZCHAR, rnames[RA]);
#ifndef TARGET_NO_ABICALLS
	printf("\t.set noreorder\n");
	printf("\t.cpload $25\t# pseudo-op to load GOT ptr into $25\n");
	printf("\t.set reorder\n");
#endif

	printf("\tsubu %s,%s,%d\n", rnames[SP], rnames[SP], ARGINIT/SZCHAR);
#ifndef TARGET_NO_ABICALLS
	/* emit PIC only if -fpic or -fPIC set */
	if (kflag > 0)
		printf("\t.cprestore 8\t# pseudo-op to store GOT ptr at 8(sp)\n");
#endif

	if (!leaf)
		printf("\tsw %s,4(%s)\n", rnames[RA], rnames[SP]);
	if (!mips_omit_fp) {
		printf("\tsw %s,0(%s)\n", rnames[FP], rnames[SP]);
		printf("\tmove %s,%s\n", rnames[FP], rnames[SP]);
	}

#ifdef notyet
	/* profiling */
	if (pflag) {
		printf("\t.set noat\n");
		printf("\tmove %s,%s\t# save current return address\n",
		    rnames[AT], rnames[RA]);
		printf("\tsubu %s,%s,8\t# _mcount pops 2 words from stack\n",
		    rnames[SP], rnames[SP]);
		printf("\tjal %s\n", exname("_mcount"));
		printf("\tnop\n");
		printf("\t.set at\n");
	}
#endif

	if (addto)
		printf("\tsubu %s,%s,%d\n", rnames[SP], rnames[SP], addto);

	for (i = p2env.p_regs[0], j = 0; i; i >>= 1, j++)
		if (i & 1)
			printf("\tsw %s,%d(%s) # save permanent\n",
				rnames[j],
				mips_omit_fp ? addto - regoff[j] : -regoff[j],
				rnames[mips_omit_fp ? SP : FP]);

}

void
eoftn(struct interpass_prolog * ipp)
{
	int i, j;
	int leaf;

	(void) offcalc(ipp, mips_omit_fp);
	leaf = mips_leaf_function;

	if (ipp->ipp_ip.ip_lbl == 0) {
		mips_omit_fp = 0;
		mips_frame_adjust = 0;
		mips_leaf_function = 0;
		return;		/* no code needs to be generated */
	}

	/* return from function code */
	for (i = p2env.p_regs[0], j = 0; i; i >>= 1, j++) {
		if (i & 1)
			printf("\tlw %s,%d(%s)\n",
				rnames[j],
				mips_omit_fp ? mips_frame_adjust - regoff[j] : -regoff[j],
				rnames[mips_omit_fp ? SP : FP]);
	}

	if (mips_omit_fp) {
		if (leaf) {
			if (mips_frame_adjust)
				printf("\taddiu %s,%s,%d\n",
				    rnames[SP], rnames[SP],
				    mips_frame_adjust);
			printf("\tjr %s\n", rnames[RA]);
			printf("\taddiu %s,%s,%d\n", rnames[SP], rnames[SP],
			    ARGINIT/SZCHAR);
		} else if (mips_frame_adjust > 0 && mips_frame_adjust <= 32763) {
			printf("\tlw %s,%d(%s)\n", rnames[RA],
			    mips_frame_adjust + 4, rnames[SP]);
			printf("\taddiu %s,%s,%d\n",
			    rnames[SP], rnames[SP], mips_frame_adjust);
			printf("\tjr %s\n", rnames[RA]);
			printf("\taddiu %s,%s,%d\n", rnames[SP], rnames[SP],
			    ARGINIT/SZCHAR);
		} else {
			if (mips_frame_adjust)
				printf("\taddiu %s,%s,%d\n",
				    rnames[SP], rnames[SP], mips_frame_adjust);
			printf("\tlw %s,4(%s)\n", rnames[RA], rnames[SP]);
			printf("\taddiu %s,%s,%d\n", rnames[SP], rnames[SP],
			    ARGINIT/SZCHAR);
			printf("\tjr %s\n", rnames[RA]);
			printf("\tnop\n");
		}
	} else {
		printf("\taddiu %s,%s,%d\n", rnames[SP], rnames[FP],
		    ARGINIT/SZCHAR);
		if (!leaf)
			printf("\tlw %s,%d(%s)\n", rnames[RA],
			    4-ARGINIT/SZCHAR, rnames[SP]);
		printf("\tlw %s,%d(%s)\n", rnames[FP], 0-ARGINIT/SZCHAR,
		    rnames[SP]);
		printf("\tjr %s\n", rnames[RA]);
		printf("\tnop\n");
	}

	mips_omit_fp = 0;
	mips_frame_adjust = 0;
	mips_leaf_function = 0;

#ifdef USE_GAS
	printf("\t.end %s\n", ipp->ipp_name);
	printf("\t.size %s,.-%s\n", ipp->ipp_name, ipp->ipp_name);
#endif
}

/*
 * add/sub/...
 *
 * Param given:
 */
void
hopcode(int f, int o)
{
	char *str;

	switch (o) {
	case EQ:
		str = "beqz";	/* pseudo-op */
		break;
	case NE:
		str = "bnez";	/* pseudo-op */
		break;
	case ULE:
	case LE:
		str = "blez";
		break;
	case ULT:
	case LT:
		str = "bltz";
		break;
	case UGE:
	case GE:
		str = "bgez";
		break;
	case UGT:
	case GT:
		str = "bgtz";
		break;
	case PLUS:
		str = "add";
		break;
	case MINUS:
		str = "sub";
		break;
	case AND:
		str = "and";
		break;
	case OR:
		str = "or";
		break;
	case ER:
		str = "xor";
		break;
	default:
		comperr("hopcode2: %d", o);
		str = 0;	/* XXX gcc */
	}

	printf("%s%c", str, f);
}

char *
rnames[] = {
#ifdef USE_GAS
	/* gnu assembler */
	"$zero", "$at", "$2", "$3", "$4", "$5", "$6", "$7",
	"$8", "$9", "$10", "$11", "$12", "$13", "$14", "$15",
	"$16", "$17", "$18", "$19", "$20", "$21", "$22", "$23",
	"$24", "$25",
	"$kt0", "$kt1", "$gp", "$sp", "$fp", "$ra",
	"$2!!$3!!",
	"$4!!$5!!", "$5!!$6!!", "$6!!$7!!", "$7!!$8!!",
	"$8!!$9!!", "$9!!$10!", "$10!$11!", "$11!$12!",
	"$12!$13!", "$13!$14!", "$14!$15!", "$15!$24!", "$24!$25!",
	"$16!$17!", "$17!$18!", "$18!$19!", "$19!$20!",
	"$20!$21!", "$21!$22!", "$22!$23!",
#else
	/* mips assembler */
	 "$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
	"$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
	"$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
	"$t8", "$t9",
	"$k0", "$k1", "$gp", "$sp", "$fp", "$ra",
	"$v0!$v1!",
	"$a0!$a1!", "$a1!$a2!", "$a2!$a3!", "$a3!$t0!",
	"$t0!$t1!", "$t1!$t2!", "$t2!$t3!", "$t3!$t4!",
	"$t4!$t5!", "$t5!$t6!", "$t6!$t7!", "$t7!$t8!", "$t8!$t9!",
	"$s0!$s1!", "$s1!$s2!", "$s2!$s3!", "$s3!$s4!",
	"$s4!$s5!", "$s5!$s6!", "$s6!$s7!",
#endif
	"$f0!$f1!", "$f2!$f3!", "$f4!$f5!", "$f6!$f7!",
	"$f8!$f9!", "$f10$f11", "$f12$f13", "$f14$f15",
	"$f16$f17", "$f18$f19", "$f20$f21", "$f22$f23",
	"$f24$f25", "$f26$f27", "$f28$f29", "$f30$f31",
};

char *
rnames_n32[] = {
	/* mips assembler */
	"$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
	"$a4", "$a5", "$a6", "$a7", "$t0", "$t1", "$t2", "$t3",
	"$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
	"$t8", "$t9",
	"$k0", "$k1", "$gp", "$sp", "$fp", "$ra",
	"$v0!$v1!",
	"$a0!$a1!", "$a1!$a2!", "$a2!$a3!", "$a3!$a4!",
	"$a4!$a5!", "$a5!$a6!", "$a6!$a7!", "$a7!$t0!",
	"$t0!$t1!", "$t1!$t2!", "$t2!$t3!", "$t3!$t8!", "$t8!$t9!",
	"$s0!$s1!", "$s1!$s2!", "$s2!$s3!", "$s3!$s4!",
	"$s4!$s5!", "$s5!$s6!", "$s6!$s7!",
	"$f0!$f1!", "$f2!$f3!", "$f4!$f5!", "$f6!$f7!",
	"$f8!$f9!", "$f10$f11", "$f12$f13", "$f14$f15",
	"$f16$f17", "$f18$f19", "$f20$f21", "$f22$f23",
	"$f24$f25", "$f26$f27", "$f28$f29", "$f30$f31",
};

int
tlen(NODE *p)
{
	switch (p->n_type) {
	case CHAR:
	case UCHAR:
		return (1);

	case SHORT:
	case USHORT:
		return (SZSHORT / SZCHAR);

	case DOUBLE:
		return (SZDOUBLE / SZCHAR);

	case INT:
	case UNSIGNED:
	case LONG:
	case ULONG:
		return (SZINT / SZCHAR);

	case LONGLONG:
	case ULONGLONG:
		return SZLONGLONG / SZCHAR;

	default:
		if (!ISPTR(p->n_type))
			comperr("tlen type %d not pointer");
		return SZPOINT(p->n_type) / SZCHAR;
	}
}


/*
 * Push a structure on stack as argument.
 */
static void
starg(NODE *p)
{
	int sz = attr_find(p->n_ap, ATTR_P2STRUCT)->iarg(0);
	//assert(p->n_rval == A1);
	printf("\tsubu %s,%s,%d\n", rnames[SP], rnames[SP], sz);
	/* A0 = dest, A1 = src, A2 = len */
	printf("\tmove %s,%s\n", rnames[A0], rnames[SP]);
	printf("\tli %s,%d\t# structure size\n", rnames[A2], sz);
	mips_leaf_ra_slot("sw");
	printf("\tjal %s\t# structure copy\n", exname("memcpy"));
	printf("\tsubu %s,%s,16\n", rnames[SP], rnames[SP]);
	printf("\taddiu %s,%s,16\n", rnames[SP], rnames[SP]);
	mips_leaf_ra_slot("lw");
}

static void
load_oreg_addr(int dst, NODE *p, const char *comment)
{
	CONSZ off = getlval(p);
	int base = p->n_rval;

	mips_adjust_frame_ref(&off, &base);

	if (off >= -32768 && off <= 32767) {
		printf("\taddiu %s,%s," CONFMT "\t# %s\n",
		    rnames[dst], rnames[base], off, comment);
		return;
	}

	if (base == AT) {
		printf("\tmove %s,%s\n", rnames[dst], rnames[base]);
		printf("\tli %s," CONFMT "\n", rnames[AT], off);
		printf("\taddu %s,%s,%s\t# %s\n",
		    rnames[dst], rnames[dst], rnames[AT], comment);
	} else {
		printf("\tli %s," CONFMT "\n", rnames[AT], off);
		printf("\taddu %s,%s,%s\t# %s\n",
		    rnames[dst], rnames[base], rnames[AT], comment);
	}
}

/*
 * Structure assignment.
 */
static void
stasg(NODE *p)
{
	assert(p->n_right->n_rval == A1);
	/* A0 = dest, A1 = src, A2 = len */
	printf("\tli %s,%d\t# structure size\n", rnames[A2],
	    attr_find(p->n_ap, ATTR_P2STRUCT)->iarg(0));
	if (p->n_left->n_op == OREG) {
		load_oreg_addr(A0, p->n_left, "dest address");
	} else if (p->n_left->n_op == NAME) {
		printf("\tla %s,", rnames[A0]);
		adrput(stdout, p->n_left);
		printf("\n");
	}
	mips_leaf_ra_slot("sw");
	printf("\tjal %s\t# structure copy\n", exname("memcpy"));
	printf("\tsubu %s,%s,16\n", rnames[SP], rnames[SP]);
	printf("\taddiu %s,%s,16\n", rnames[SP], rnames[SP]);
	mips_leaf_ra_slot("lw");
}

static void
shiftop(NODE *p)
{
	NODE *r = p->n_right;
	TWORD ty = p->n_type;

	if (p->n_op == LS && r->n_op == ICON && getlval(r) < 32) {
		expand(p, INBREG, "\tsrl A1,AL,");
		printf(CONFMT "\t# 64-bit left-shift\n", 32 - getlval(r));
		expand(p, INBREG, "\tsll U1,UL,AR\n");
		expand(p, INBREG, "\tor U1,U1,A1\n");
		expand(p, INBREG, "\tsll A1,AL,AR\n");
	} else if (p->n_op == LS && r->n_op == ICON && getlval(r) < 64) {
		expand(p, INBREG, "\tli A1,0\t# 64-bit left-shift\n");
		expand(p, INBREG, "\tsll U1,AL,");
		printf(CONFMT "\n", getlval(r) - 32);
	} else if (p->n_op == LS && r->n_op == ICON) {
		expand(p, INBREG, "\tli A1,0\t# 64-bit left-shift\n");
		expand(p, INBREG, "\tli U1,0\n");
	} else if (p->n_op == RS && r->n_op == ICON && getlval(r) < 32) {
		expand(p, INBREG, "\tsll U1,UL,");
		printf(CONFMT "\t# 64-bit right-shift\n", 32 - getlval(r));
		expand(p, INBREG, "\tsrl A1,AL,AR\n");
		expand(p, INBREG, "\tor A1,A1,U1\n");
		if (ty == LONGLONG)
			expand(p, INBREG, "\tsra U1,UL,AR\n");
		else
			expand(p, INBREG, "\tsrl U1,UL,AR\n");
	} else if (p->n_op == RS && r->n_op == ICON && getlval(r) < 64) {
		if (ty == LONGLONG) {
			expand(p, INBREG, "\tsra U1,UL,31\t# 64-bit right-shift\n");
			expand(p, INBREG, "\tsra A1,UL,");
		}else {
			expand(p, INBREG, "\tli U1,0\t# 64-bit right-shift\n");
			expand(p, INBREG, "\tsrl A1,UL,");
		}
		printf(CONFMT "\n", getlval(r) - 32);
	} else if (p->n_op == LS && r->n_op == ICON) {
		expand(p, INBREG, "\tli A1,0\t# 64-bit right-shift\n");
		expand(p, INBREG, "\tli U1,0\n");
	} else {
		comperr("shiftop");
	}
}

/*
 * http://gcc.gnu.org/onlinedocs/gccint/Soft-float-library-routines.html#Soft-float-library-routines
 */
static void
fpemulop(NODE *p)
{
	NODE *l = p->n_left;
	char *ch = NULL;

	if (p->n_op == PLUS && p->n_type == FLOAT) ch = "addsf3";
	else if (p->n_op == PLUS && p->n_type == DOUBLE) ch = "adddf3";
	else if (p->n_op == PLUS && p->n_type == LDOUBLE) ch = "adddf3";

	else if (p->n_op == MINUS && p->n_type == FLOAT) ch = "subsf3";
	else if (p->n_op == MINUS && p->n_type == DOUBLE) ch = "subdf3";
	else if (p->n_op == MINUS && p->n_type == LDOUBLE) ch = "subdf3";

	else if (p->n_op == MUL && p->n_type == FLOAT) ch = "mulsf3";
	else if (p->n_op == MUL && p->n_type == DOUBLE) ch = "muldf3";
	else if (p->n_op == MUL && p->n_type == LDOUBLE) ch = "muldf3";

	else if (p->n_op == DIV && p->n_type == FLOAT) ch = "divsf3";
	else if (p->n_op == DIV && p->n_type == DOUBLE) ch = "divdf3";
	else if (p->n_op == DIV && p->n_type == LDOUBLE) ch = "divdf3";

	else if (p->n_op == UMINUS && p->n_type == FLOAT) ch = "negsf2";
	else if (p->n_op == UMINUS && p->n_type == DOUBLE) ch = "negdf2";
	else if (p->n_op == UMINUS && p->n_type == LDOUBLE) ch = "negdf2";

	else if (p->n_op == EQ && l->n_type == FLOAT) ch = "eqsf2";
	else if (p->n_op == EQ && l->n_type == DOUBLE) ch = "eqdf2";
	else if (p->n_op == EQ && l->n_type == LDOUBLE) ch = "eqdf2";

	else if (p->n_op == NE && l->n_type == FLOAT) ch = "nesf2";
	else if (p->n_op == NE && l->n_type == DOUBLE) ch = "nedf2";
	else if (p->n_op == NE && l->n_type == LDOUBLE) ch = "nedf2";

	else if (p->n_op == GE && l->n_type == FLOAT) ch = "gesf2";
	else if (p->n_op == GE && l->n_type == DOUBLE) ch = "gedf2";
	else if (p->n_op == GE && l->n_type == LDOUBLE) ch = "gedf2";

	else if (p->n_op == LE && l->n_type == FLOAT) ch = "lesf2";
	else if (p->n_op == LE && l->n_type == DOUBLE) ch = "ledf2";
	else if (p->n_op == LE && l->n_type == LDOUBLE) ch = "ledf2";

	else if (p->n_op == GT && l->n_type == FLOAT) ch = "gtsf2";
	else if (p->n_op == GT && l->n_type == DOUBLE) ch = "gtdf2";
	else if (p->n_op == GT && l->n_type == LDOUBLE) ch = "gtdf2";

	else if (p->n_op == LT && l->n_type == FLOAT) ch = "ltsf2";
	else if (p->n_op == LT && l->n_type == DOUBLE) ch = "ltdf2";
	else if (p->n_op == LT && l->n_type == LDOUBLE) ch = "ltdf2";

	else if (p->n_op == SCONV && p->n_type == FLOAT) {
		if (l->n_type == DOUBLE) ch = "truncdfsf2";
		else if (l->n_type == LDOUBLE) ch = "truncdfsf2";
		else if (l->n_type == ULONGLONG) ch = "floatunsdisf";
		else if (l->n_type == LONGLONG) ch = "floatdisf";
		else if (l->n_type == LONG) ch = "floatsisf";
		else if (l->n_type == ULONG) ch = "floatunsisf";
		else if (l->n_type == INT) ch = "floatsisf";
		else if (l->n_type == UNSIGNED) ch = "floatunsisf";
	} else if (p->n_op == SCONV && p->n_type == DOUBLE) {
		if (l->n_type == FLOAT) ch = "extendsfdf2";
		else if (l->n_type == LDOUBLE) ch = NULL;
		else if (l->n_type == ULONGLONG) ch = "floatunsdidf";
		else if (l->n_type == LONGLONG) ch = "floatdidf";
		else if (l->n_type == LONG) ch = "floatsidf";
		else if (l->n_type == ULONG) ch = "floatunsidf";
		else if (l->n_type == INT) ch = "floatsidf";
		else if (l->n_type == UNSIGNED) ch = "floatunsidf";
	} else if (p->n_op == SCONV && p->n_type == LDOUBLE) {
		if (l->n_type == FLOAT) ch = "extendsfdf2";
		else if (l->n_type == DOUBLE) ch = NULL;
		else if (l->n_type == ULONGLONG) ch = "floatunsdidf";
		else if (l->n_type == LONGLONG) ch = "floatdidf";
		else if (l->n_type == LONG) ch = "floatsidf";
		else if (l->n_type == ULONG) ch = "floatunsidf";
		else if (l->n_type == INT) ch = "floatsidf";
		else if (l->n_type == UNSIGNED) ch = "floatunsidf";
	} else if (p->n_op == SCONV && p->n_type == ULONGLONG) {
		if (l->n_type == FLOAT) ch = "fixunssfdi";
		else if (l->n_type == DOUBLE) ch = "fixunsdfdi";
		else if (l->n_type == LDOUBLE) ch = "fixunsdfdi";
	} else if (p->n_op == SCONV && p->n_type == LONGLONG) {
		if (l->n_type == FLOAT) ch = "fixsfdi";
		else if (l->n_type == DOUBLE) ch = "fixdfdi";
		else if (l->n_type == LDOUBLE) ch = "fixdfdi";
	} else if (p->n_op == SCONV && p->n_type == LONG) {
		if (l->n_type == FLOAT) ch = "fixsfsi";
		else if (l->n_type == DOUBLE) ch = "fixdfsi";
		else if (l->n_type == LDOUBLE) ch = "fixdfsi";
	} else if (p->n_op == SCONV && p->n_type == ULONG) {
		if (l->n_type == FLOAT) ch = "fixunssfsi";
		else if (l->n_type == DOUBLE) ch = "fixunsdfsi";
		else if (l->n_type == LDOUBLE) ch = "fixunsdfsi";
	} else if (p->n_op == SCONV && p->n_type == INT) {
		if (l->n_type == FLOAT) ch = "fixsfsi";
		else if (l->n_type == DOUBLE) ch = "fixdfsi";
		else if (l->n_type == LDOUBLE) ch = "fixdfsi";
	} else if (p->n_op == SCONV && p->n_type == UNSIGNED) {
		if (l->n_type == FLOAT) ch = "fixunssfsi";
		else if (l->n_type == DOUBLE) ch = "fixunsdfsi";
		else if (l->n_type == LDOUBLE) ch = "fixunsdfsi";
	}

	if (ch == NULL) comperr("ZF: op=0x%x (%d)\n", p->n_op, p->n_op);

	if (p->n_op == SCONV && !mips_soft_float) {
#ifdef MIPS_HARDFLOAT_O32_ABI
		if (l->n_type == FLOAT) {
			printf("\tmov.s ");
			print_reg64name(stdout, F12, 0);
			printf(",");
			adrput(stdout, l);
			printf("\n");
		} else if (l->n_type == DOUBLE || l->n_type == LDOUBLE) {
			printf("\tmov.d ");
			print_reg64name(stdout, F12, 0);
			printf(",");
			adrput(stdout, l);
			printf("\n");
		}
#else
		if (l->n_type == FLOAT) {
			printf("\tmfc1 %s,", rnames[A0]);
			adrput(stdout, l);
			printf("\n\tnop\n");
		}  else if (l->n_type == DOUBLE || l->n_type == LDOUBLE) {
			printf("\tmfc1 %s,", rnames[A1]);
			upput(l, 0);
			printf("\n\tnop\n");
			printf("\tmfc1 %s,", rnames[A0]);
			adrput(stdout, l);
			printf("\n\tnop\n");
		}
#endif
	}

	mips_leaf_ra_slot("sw");
	printf("\tjal __%s\t# softfloat operation\n", exname(ch));
	printf("\tsubu %s,%s,16\n", rnames[SP], rnames[SP]);
	printf("\taddiu %s,%s,16\n", rnames[SP], rnames[SP]);
	mips_leaf_ra_slot("lw");

	if (p->n_op >= EQ && p->n_op <= GT) {
		switch (p->n_op) {
		case EQ:
			printf("\tbeqz %s,", rnames[V0]);
			break;
		case NE:
			printf("\tbnez %s,", rnames[V0]);
			break;
		case LT:
			printf("\tbltz %s,", rnames[V0]);
			break;
		case LE:
			printf("\tblez %s,", rnames[V0]);
			break;
		case GT:
			printf("\tbgtz %s,", rnames[V0]);
			break;
		case GE:
			printf("\tbgez %s,", rnames[V0]);
			break;
		default:
			comperr("fpemulop branch");
		}
		printf(LABFMT "\n", p->n_label);
		printf("\tnop\n");
	}
}

/*
 * http://gcc.gnu.org/onlinedocs/gccint/Integer-library-routines.html#Integer-library-routines
 */
static void
emulop(NODE *p)
{
	char *ch = NULL;

	if (p->n_op == LS && DEUNSIGN(p->n_type) == LONGLONG) ch = "ashldi3";
	else if (p->n_op == LS && (DEUNSIGN(p->n_type) == LONG ||
	    DEUNSIGN(p->n_type) == INT))
		ch = "ashlsi3";

	else if (p->n_op == RS && p->n_type == ULONGLONG) ch = "lshrdi3";
	else if (p->n_op == RS && (p->n_type == ULONG || p->n_type == INT))
		ch = "lshrsi3";

	else if (p->n_op == RS && p->n_type == LONGLONG) ch = "ashrdi3";
	else if (p->n_op == RS && (p->n_type == LONG || p->n_type == INT))
		ch = "ashrsi3";
	
	else if (p->n_op == DIV && p->n_type == LONGLONG) ch = "divdi3";
	else if (p->n_op == DIV && (p->n_type == LONG || p->n_type == INT))
		ch = "divsi3";

	else if (p->n_op == DIV && p->n_type == ULONGLONG) ch = "udivdi3";
	else if (p->n_op == DIV && (p->n_type == ULONG ||
	    p->n_type == UNSIGNED))
		ch = "udivsi3";

	else if (p->n_op == MOD && p->n_type == LONGLONG) ch = "moddi3";
	else if (p->n_op == MOD && (p->n_type == LONG || p->n_type == INT))
		ch = "modsi3";

	else if (p->n_op == MOD && p->n_type == ULONGLONG) ch = "umoddi3";
	else if (p->n_op == MOD && (p->n_type == ULONG ||
	    p->n_type == UNSIGNED))
		ch = "umodsi3";

	else if (p->n_op == MUL && p->n_type == LONGLONG) ch = "muldi3";
	else if (p->n_op == MUL && (p->n_type == LONG || p->n_type == INT))
		ch = "mulsi3";

	else if (p->n_op == UMINUS && p->n_type == LONGLONG) ch = "negdi2";
	else if (p->n_op == UMINUS && p->n_type == LONG) ch = "negsi2";

	else ch = 0, comperr("ZE");
	mips_leaf_ra_slot("sw");
	printf("\tjal __%s\t# emulated operation\n", exname(ch));
	printf("\tsubu %s,%s,16\n", rnames[SP], rnames[SP]);
	printf("\taddiu %s,%s,16\n", rnames[SP], rnames[SP]);
	mips_leaf_ra_slot("lw");
}

/*
 * Emit code to compare two longlong numbers.
 */
static void
twollbr(NODE *p, const char *op, const char *l, const char *r, int lab)
{
	printf("\t%s ", op);
	expand(p, 0, l);
	printf(",");
	expand(p, 0, r);
	printf("," LABFMT "\n", lab);
	printf("\tnop\n");
}

static void
twollslt(NODE *p, int unsig, const char *l, const char *r)
{
	printf("\t%s ", unsig ? "sltu" : "slt");
	expand(p, 0, "A1");
	printf(",");
	expand(p, 0, l);
	printf(",");
	expand(p, 0, r);
	printf("\n");
}

static void
twollbrtmp(NODE *p, const char *op, int lab)
{
	printf("\t%s ", op);
	expand(p, 0, "A1");
	printf("," LABFMT "\n", lab);
	printf("\tnop\n");
}

static void
twollbrtmp_slt(NODE *p, const char *op, int lab, int unsig,
    const char *l, const char *r)
{
	printf("\t%s ", op);
	expand(p, 0, "A1");
	printf("," LABFMT "\n", lab);
	twollslt(p, unsig, l, r);
}

static void
twolljump(int lab)
{
	printf("\tj " LABFMT "\n", lab);
	printf("\tnop\n");
}

static void
twollcomp(NODE *p)
{
	int o = p->n_op;
	int s = getlab2();
	int e = p->n_label;
	int unsig = o >= ULE;

	switch (o) {
	case EQ:
		twollbr(p, "bne", "UL", "UR", s);
		twollbr(p, "beq", "AL", "AR", e);
		break;
	case NE:
		twollbr(p, "bne", "UL", "UR", e);
		twollbr(p, "bne", "AL", "AR", e);
		break;
	case LT:
	case ULT:
		twollslt(p, unsig, "UL", "UR");
		twollbrtmp_slt(p, "bnez", e, unsig, "UR", "UL");
		twollbrtmp_slt(p, "bnez", s, 1, "AL", "AR");
		twollbrtmp(p, "bnez", e);
		break;
	case LE:
	case ULE:
		twollslt(p, unsig, "UL", "UR");
		twollbrtmp_slt(p, "bnez", e, unsig, "UR", "UL");
		twollbrtmp_slt(p, "bnez", s, 1, "AR", "AL");
		twollbrtmp(p, "bnez", s);
		twolljump(e);
		break;
	case GT:
	case UGT:
		twollslt(p, unsig, "UR", "UL");
		twollbrtmp_slt(p, "bnez", e, unsig, "UL", "UR");
		twollbrtmp_slt(p, "bnez", s, 1, "AR", "AL");
		twollbrtmp(p, "bnez", e);
		break;
	case GE:
	case UGE:
		twollslt(p, unsig, "UL", "UR");
		twollbrtmp_slt(p, "bnez", s, unsig, "UR", "UL");
		twollbrtmp_slt(p, "bnez", e, 1, "AL", "AR");
		twollbrtmp(p, "bnez", s);
		twolljump(e);
		break;
	default:
		comperr("twollcomp bad op %d", o);
	}
	deflab(s);
}

static void
ucmpbr(NODE *p)
{
	NODE *r = p->n_right;
	const char *br;
	int swap;

	if (r->n_op == ICON && r->n_name[0] == '\0' && getlval(r) == 0) {
		switch (p->n_op) {
		case ULT:
			return;
		case UGE:
			printf("\tbeq %s,%s," LABFMT "\n",
			    rnames[ZERO], rnames[ZERO], p->n_label);
			printf("\tnop\n");
			return;
		case ULE:
			expand(p, 0, "\tbeqz AL,LC\n\tnop\n");
			return;
		case UGT:
			expand(p, 0, "\tbnez AL,LC\n\tnop\n");
			return;
		default:
			comperr("ucmpbr bad zero op %d", p->n_op);
		}
	}

	swap = p->n_op == UGT || p->n_op == ULE;
	br = (p->n_op == ULT || p->n_op == UGT) ? "bnez" : "beqz";

	if (r->n_op == ICON && r->n_name[0] == '\0') {
		printf("\tli %s,", rnames[AT]);
		conput(stdout, r);
		printf("\n");
		if (swap)
			expand(p, 0, "\tsltu A1,$at,AL\n");
		else
			expand(p, 0, "\tsltu A1,AL,$at\n");
	} else if (swap) {
		expand(p, 0, "\tsltu A1,AR,AL\n");
	} else {
		expand(p, 0, "\tsltu A1,AL,AR\n");
	}
	printf("\t%s ", br);
	expand(p, 0, "A1");
	printf("," LABFMT "\n", p->n_label);
	printf("\tnop\n");
}

static void
zero_left_cmpbr(NODE *p)
{
	const char *br;

	switch (p->n_op) {
	case EQ:
	case UGE:
		br = "beqz";
		break;
	case NE:
	case ULT:
		br = "bnez";
		break;
	case LT:
		br = "bgtz";
		break;
	case LE:
		br = "bgez";
		break;
	case GT:
		br = "bltz";
		break;
	case GE:
		br = "blez";
		break;
	case ULE:
		printf("\tbeq %s,%s," LABFMT "\n",
		    rnames[ZERO], rnames[ZERO], p->n_label);
		printf("\tnop\n");
		return;
	case UGT:
		return;
	default:
		comperr("zero_left_cmpbr bad op %d", p->n_op);
	}
	printf("\t%s ", br);
	expand(p, 0, "AR");
	printf("," LABFMT "\n", p->n_label);
	printf("\tnop\n");
}

static void
fpcmpops(NODE *p)
{
	NODE *l = p->n_left;

	switch (p->n_op) {
	case EQ:
		if (l->n_type == FLOAT)
			expand(p, 0, "\tc.eq.s AL,AR\n");
		else
			expand(p, 0, "\tc.eq.d AL,AR\n");
		expand(p, 0, "\tnop\n\tbc1t LC\n");
		break;
	case NE:
		if (l->n_type == FLOAT)
			expand(p, 0, "\tc.eq.s AL,AR\n");
		else
			expand(p, 0, "\tc.eq.d AL,AR\n");
		expand(p, 0, "\tnop\n\tbc1f LC\n");
		break;
	case LT:
		if (l->n_type == FLOAT)
			expand(p, 0, "\tc.lt.s AL,AR\n");
		else
			expand(p, 0, "\tc.lt.d AL,AR\n");
		expand(p, 0, "\tnop\n\tbc1t LC\n");
		break;
	case GE:
		if (l->n_type == FLOAT)
			expand(p, 0, "\tc.lt.s AL,AR\n");
		else
			expand(p, 0, "\tc.lt.d AL,AR\n");
		expand(p, 0, "\tnop\n\tbc1f LC\n");
		break;
	case LE:
		if (l->n_type == FLOAT)
			expand(p, 0, "\tc.le.s AL,AR\n");
		else
			expand(p, 0, "\tc.le.d AL,AR\n");
		expand(p, 0, "\tnop\n\tbc1t LC\n");
		break;
	case GT:
		if (l->n_type == FLOAT)
			expand(p, 0, "\tc.le.s AL,AR\n");
		else
			expand(p, 0, "\tc.le.d AL,AR\n");
		expand(p, 0, "\tnop\n\tbc1f LC\n");
		break;
	}
	printf("\tnop\n");
}

static void
addsubcon(NODE *p, int sub, int unsig)
{
	NODE *r = p->n_right;
	CONSZ val, imm;
	const char *op;

	if (r->n_op != ICON)
		comperr("addsubcon non-ICON");

	val = getlval(r);
	if (r->n_name[0] == '\0') {
		imm = sub ? -val : val;
		if (imm >= -32768 && imm <= 32767) {
			printf("\taddiu ");
			expand(p, 0, "A1,AL,");
			printf(CONFMT "\n", imm);
			return;
		}
	}

	if (r->n_name[0] != '\0') {
		printf("\tla %s,", rnames[AT]);
		adrput(stdout, r);
		printf("\n");
	} else {
		printf("\tli %s,", rnames[AT]);
		conput(stdout, r);
		printf("\n");
	}

	op = sub ? "subu" : "addu";
	printf("\t%s ", op);
	expand(p, 0, "A1,AL,");
	printf("%s\n", rnames[AT]);
}

static int
mips_con_log2(CONSZ val)
{
	int shift = 0;

	while (val > 1) {
		val >>= 1;
		shift++;
	}
	return shift;
}

static void
mulpow2con(NODE *p)
{
	int shift = mips_con_log2(getlval(p->n_right));

	expand(p, 0, "\tsll A1,AL,");
	printf("%d\t# multiply by power-of-two constant\n", shift);
}

static int
mips_is_pow2u(unsigned int val)
{
	return val != 0 && (val & (val - 1)) == 0;
}

static void
mips_shiftadd_mul_plan(CONSZ val, int *shift, int *postshift, int *add)
{
	unsigned int odd;

	*postshift = 0;
	odd = (unsigned int)val;
	while ((odd & 1) == 0) {
		odd >>= 1;
		(*postshift)++;
	}

	if (mips_is_pow2u(odd - 1)) {
		*shift = mips_con_log2((CONSZ)(odd - 1));
		*add = 1;
		return;
	}
	if (mips_is_pow2u(odd + 1)) {
		*shift = mips_con_log2((CONSZ)(odd + 1));
		*add = 0;
		return;
	}

	comperr("mips_shiftadd_mul_plan");
}

static int
mips_shiftadd_mul_ok(CONSZ val)
{
	unsigned int odd;

	if (val <= 2 || val > 0x7fffffff)
		return 0;
	odd = (unsigned int)val;
	if (mips_is_pow2u(odd))
		return 0;

	while ((odd & 1) == 0)
		odd >>= 1;
	return mips_is_pow2u(odd - 1) || mips_is_pow2u(odd + 1);
}

static void
mulshiftaddcon(NODE *p)
{
	int add, postshift, shift;

	mips_shiftadd_mul_plan(getlval(p->n_right), &shift, &postshift, &add);
	printf("\tmove %s,", rnames[AT]);
	expand(p, 0, "AL\t# preserve shift-add multiplicand\n");
	printf("\tsll ");
	expand(p, 0, "A1,");
	printf("%s,%d\n", rnames[AT], shift);
	if (add)
		expand(p, 0, "\taddu A1,A1,");
	else
		expand(p, 0, "\tsubu A1,A1,");
	printf("%s\t# multiply by shift-add constant\n", rnames[AT]);
	if (postshift != 0) {
		expand(p, 0, "\tsll A1,A1,");
		printf("%d\t# multiply by shift-add constant\n", postshift);
	}
}

static void
udivpow2con(NODE *p)
{
	int shift = mips_con_log2(getlval(p->n_right));

	expand(p, 0, "\tsrl A1,AL,");
	printf("%d\t# unsigned division by power-of-two constant\n", shift);
}

static void
urempow2con(NODE *p)
{
	int shift = mips_con_log2(getlval(p->n_right));
	CONSZ mask = getlval(p->n_right) - 1;

	if (shift <= 16) {
		expand(p, 0, "\tandi A1,AL,");
		printf(CONFMT "\t# unsigned modulo by power-of-two constant\n",
		    mask);
	} else {
		expand(p, 0, "\tsll A1,AL,");
		printf("%d\t# unsigned modulo by power-of-two constant\n",
		    32 - shift);
		expand(p, 0, "\tsrl A1,A1,");
		printf("%d\n", 32 - shift);
	}
}

static void
sdivpow2con(NODE *p)
{
	int shift = mips_con_log2(getlval(p->n_right));

	if (shift == 1) {
		printf("\tsrl %s,", rnames[AT]);
		expand(p, 0, "AL");
		printf(",31\t# signed division by 2 bias\n");
	} else {
		printf("\tsra %s,", rnames[AT]);
		expand(p, 0, "AL");
		printf(",31\t# signed division by power-of-two bias\n");
		printf("\tsrl %s,%s,%d\n", rnames[AT], rnames[AT],
		    32 - shift);
	}
	expand(p, 0, "\taddu A1,AL,$at\n");
	expand(p, 0, "\tsra A1,A1,");
	printf("%d\n", shift);
}

static void
srempow2con(NODE *p)
{
	int shift = mips_con_log2(getlval(p->n_right));
	CONSZ mask = getlval(p->n_right) - 1;

	printf("\tsra %s,", rnames[AT]);
	expand(p, 0, "AL");
	printf(",31\t# signed modulo by power-of-two bias\n");
	printf("\tsrl %s,%s,%d\n", rnames[AT], rnames[AT], 32 - shift);
	expand(p, 0, "\taddu A1,AL,$at\n");
	if (shift <= 16) {
		expand(p, 0, "\tandi A1,A1,");
		printf(CONFMT "\n", mask);
	} else {
		expand(p, 0, "\tsll A1,A1,");
		printf("%d\n", 32 - shift);
		expand(p, 0, "\tsrl A1,A1,");
		printf("%d\n", 32 - shift);
	}
	expand(p, 0, "\tsubu A1,A1,$at\n");
}

void
zzzcode(NODE * p, int c)
{
	int sz;

	switch (c) {

	case 'A':	/* signed add with constant */
		addsubcon(p, 0, 0);
		break;

	case 'B':	/* unsigned/pointer add with constant */
		addsubcon(p, 0, 1);
		break;

	case 'C':	/* remove arguments from stack after subroutine call */
		sz = p->n_qual > 16 ? p->n_qual : 16;
		printf("\taddiu %s,%s,%d\n", rnames[SP], rnames[SP], sz);
		break;

	case 'D':	/* long long comparison */
		twollcomp(p);
		break;

	case 'E':	/* emit emulated ops */
		emulop(p);
		break;

	case 'F':	/* emit emulate floating point ops */
		fpemulop(p);
		break;

	case 'G':	/* emit hardware floating-point compare op */
		fpcmpops(p);
		break;

	case 'H':	/* structure argument */
		starg(p);
		break;

	case 'I':		/* high part of init constant */
		if (p->n_name[0] != '\0')
			comperr("named highword");
		printf(CONFMT, (getlval(p) >> 32) & 0xffffffff);
		break;

	case 'J':		/* low-order memory part for narrowing conversions */
		if (p->n_op != SCONV)
			comperr("ZJ non-SCONV");
		adrput_lowpart(stdout, p->n_left, p->n_type);
		break;

	case 'K':		/* unsigned comparison branch */
		ucmpbr(p);
		break;

	case 'L':		/* load constant, using la for named constants */
		if (p->n_op != ICON)
			comperr("ZL non-ICON");
		if (p->n_name[0] != '\0')
			expand(p, 0, "\tla A1,AL\t# load named constant to reg\n");
		else
			expand(p, 0, "\tli A1,AL\t# load constant to reg\n");
		break;

	case 'M':	/* signed subtract with constant */
		addsubcon(p, 1, 0);
		break;

	case 'N':	/* unsigned/pointer subtract with constant */
		addsubcon(p, 1, 1);
		break;

	case 'O': /* 64-bit left and right shift operators */
		shiftop(p);
		break;

	case 'P':	/* multiply by positive power-of-two constant */
		mulpow2con(p);
		break;

	case 'Q':		/* emit struct assign */
		stasg(p);
		break;

	case 'R':		/* hard-float double load */
		mips_hardfp64_load(p);
		break;

	case 'S':		/* hard-float double store */
		mips_hardfp64_store(p);
		break;

	case 'T':		/* signed division by power-of-two constant */
		sdivpow2con(p);
		break;

	case 'U':		/* unsigned division by power-of-two constant */
		udivpow2con(p);
		break;

	case 'V':		/* unsigned modulo by power-of-two constant */
		urempow2con(p);
		break;

	case 'W':		/* signed modulo by power-of-two constant */
		srempow2con(p);
		break;

	case 'X':		/* branch for zero-left comparison */
		zero_left_cmpbr(p);
		break;

	case 'Y':		/* multiply by shift-add constant */
		mulshiftaddcon(p);
		break;

	case 'a':		/* save $ra around a leaf-only helper call */
		mips_leaf_ra_slot("sw");
		break;

	case 'b':		/* restore $ra after a leaf-only helper call */
		mips_leaf_ra_slot("lw");
		break;

	default:
		comperr("zzzcode %c", c);
	}
}

/* ARGSUSED */
int
rewfld(NODE * p)
{
	return (1);
}

int
fldexpand(NODE *p, int cookie, char **cp)
{
        CONSZ val;

        if (p->n_op == ASSIGN)
                p = p->n_left;
        switch (**cp) {
        case 'S':
                printf("%d", UPKFSZ(p->n_rval));
                break;
        case 'H':
                printf("%d", UPKFOFF(p->n_rval));
                break;
        case 'M':
        case 'N':
                val = (CONSZ)1 << UPKFSZ(p->n_rval);
                --val;
                val <<= UPKFOFF(p->n_rval);
                printf("0x%llx", (**cp == 'M' ? val : ~val)  & 0xffffffff);
                break;
        default:
                comperr("fldexpand");
        }
        return 1;
}

/*
 * Does the bitfield shape match?
 */
int
flshape(NODE * p)
{
	int o = p->n_op;

	if (o == OREG || o == REG || o == NAME)
		return SRDIR;	/* Direct match */
	if (o == UMUL && shumul(p->n_left, SOREG))
		return SROREG;	/* Convert into oreg */
	return SRREG;		/* put it into a register */
}

/* INTEMP shapes must not contain any temporary registers */
/* XXX should this go away now? */
int
shtemp(NODE * p)
{
	return 0;
#if 0
	int r;

	if (p->n_op == STARG)
		p = p->n_left;

	switch (p->n_op) {
	case REG:
		return (!istreg(p->n_rval));

	case OREG:
		r = p->n_rval;
		if (R2TEST(r)) {
			if (istreg(R2UPK1(r)))
				return (0);
			r = R2UPK2(r);
		}
		return (!istreg(r));

	case UMUL:
		p = p->n_left;
		return (p->n_op != UMUL && shtemp(p));
	}

	if (optype(p->n_op) != LTYPE)
		return (0);
	return (1);
#endif
}

void
adrcon(CONSZ val)
{
	printf(CONFMT, val);
}

void
conput(FILE *fp, NODE *p)
{
	CONSZ val;
	U_CONSZ uval;

	switch (p->n_op) {
	case ICON:
		val = getlval(p);
		if (p->n_name[0] != '\0') {
			fprintf(fp, "%s", p->n_name);
			if (val)
				fprintf(fp, "+" CONFMT, val);
		} else {
			uval = (U_CONSZ)val & 0xffffffffULL;
			if (uval <= (U_CONSZ)MAX_INT)
				fprintf(fp, CONFMT, (CONSZ)uval);
			else
				fprintf(fp, "0x%llx", uval);
		}
		return;

	default:
		comperr("illegal conput");
	}
}

/* ARGSUSED */
void
insput(NODE * p)
{
	comperr("insput");
}

/*
 * Print lower or upper name of 64-bit register.
 */
static void
print_reg64name(FILE *fp, int rval, int hi)
{
        int off;
	char *regname = rnames[rval];

#ifdef TARGET_BIG_ENDIAN
	if (GCLASS(rval) == CLASSB)
		hi = !hi;
#endif

        off = 4 * (hi != 0);
        fprintf(fp, "%c%c",
                 regname[off],
                 regname[off + 1]);
        if (regname[off + 2] != '!')
                fputc(regname[off + 2], fp);
        if (regname[off + 3] != '!')
                fputc(regname[off + 3], fp);
}

/*
 * Write out the upper address, like the upper register of a 2-register
 * reference, or the next memory location.
 */
void
upput(NODE * p, int size)
{

	size /= SZCHAR;
	switch (p->n_op) {
	case REG:
		if (GCLASS(p->n_rval) == CLASSB || GCLASS(p->n_rval) == CLASSC)
			print_reg64name(stdout, p->n_rval, 1);
		else
			printf("%s", rnames[p->n_rval]);
		break;

	case NAME:
	case OREG:
		setlval(p, getlval(p) + size);
		adrput(stdout, p);
		setlval(p, getlval(p) - size);
		break;
	case ICON:
		printf(CONFMT, getlval(p) >> 32);
		break;
	default:
		comperr("upput bad op %d size %d", p->n_op, size);
	}
}

static int
mips_type_size(TWORD t)
{
	switch (DEUNSIGN(t)) {
	case CHAR:
		return 1;
	case SHORT:
		return 2;
	case INT:
	case LONG:
		return 4;
	case LONGLONG:
		return 8;
	default:
		return 0;
	}
}

static void
adrput_lowpart(FILE *io, NODE *p, TWORD dst)
{
	CONSZ off;
	int srcsz = 0, dstsz = 0;

	if (p->n_op == FLD)
		p = p->n_left;

	off = 0;
#ifdef TARGET_BIG_ENDIAN
	srcsz = mips_type_size(p->n_type);
	dstsz = mips_type_size(dst);
	if (srcsz > dstsz && dstsz != 0)
		off = srcsz - dstsz;
#else
	(void)srcsz;
	(void)dstsz;
#endif

	switch (p->n_op) {
	case NAME:
	case OREG:
		setlval(p, getlval(p) + off);
		adrput(io, p);
		setlval(p, getlval(p) - off);
		break;
	default:
		adrput(io, p);
		break;
	}
}

void
adrput(FILE * io, NODE * p)
{
	CONSZ off;
	int base;

	/* output an address, with offsets, from p */

	if (p->n_op == FLD)
		p = p->n_left;

	switch (p->n_op) {

	case NAME:
		if (p->n_name[0] != '\0')
			fputs(p->n_name, io);
		if (getlval(p) != 0)
			fprintf(io, "+" CONFMT, getlval(p));
		return;

	case OREG:
		off = getlval(p);
		base = p->n_rval;
		mips_adjust_frame_ref(&off, &base);
		fprintf(io, "%d(%s)", (int)off, rnames[base]);
		return;

	case ICON:
		/* addressable value of the constant */
		conput(io, p);
		return;

	case REG:
		if (GCLASS(p->n_rval) == CLASSB || GCLASS(p->n_rval) == CLASSC)
			print_reg64name(io, p->n_rval, 0);
		else
			fputs(rnames[p->n_rval], io);
		return;

	default:
		comperr("illegal address, op %d, node %p", p->n_op, p);
		return;

	}
}

static int
mips_frame_ref_offset(NODE *p, CONSZ *offp)
{
	NODE *l, *r;

	if (p == NIL)
		return 0;

	if (p->n_op == REG && p->n_rval == FPREG) {
		*offp = 0;
		return 1;
	}

	if (p->n_op != PLUS && p->n_op != MINUS)
		return 0;

	l = p->n_left;
	r = p->n_right;
	if (l == NIL || r == NIL || l->n_op != REG || l->n_rval != FPREG ||
	    r->n_op != ICON || r->n_name[0] != '\0')
		return 0;

	*offp = getlval(r);
	if (p->n_op == MINUS)
		*offp = -*offp;
	return 1;
}

static int
mips_rewrite_frame_umul(NODE *p)
{
	NODE *old;
	CONSZ off;

	if (p->n_op != UMUL || !mips_frame_ref_offset(p->n_left, &off))
		return 0;

	old = p->n_left;
	p->n_op = OREG;
	p->n_name = "";
	setlval(p, off + mips_frame_adjust);
	p->n_rval = SP;
	p->n_regw = NULL;
	p->n_su = 0;
	tfree(old);
	return 1;
}

static void
mips_rewrite_frame_ref(NODE *p)
{
	NODE *l, *r;
	CONSZ off;
	int base, opty;

	if (!mips_omit_fp || p == NIL)
		return;

	if (mips_rewrite_frame_umul(p))
		return;

	if (p->n_op == OREG) {
		off = getlval(p);
		base = p->n_rval;
		mips_adjust_frame_ref(&off, &base);
		setlval(p, off);
		p->n_rval = base;
		return;
	}

	if (mips_frame_ref_offset(p, &off)) {
		if (p->n_op == REG) {
			if (mips_frame_adjust == 0) {
				p->n_rval = SP;
				return;
			}
			p->n_op = PLUS;
			p->n_left = mklnode(REG, 0, SP, p->n_type);
			p->n_right = mklnode(ICON, mips_frame_adjust, 0, INT);
			p->n_regw = NULL;
			p->n_su = 0;
			return;
		}
		l = p->n_left;
		r = p->n_right;
		setlval(r, off + mips_frame_adjust);
		l->n_rval = SP;
		if (p->n_op == MINUS)
			p->n_op = PLUS;
		return;
	}

	opty = optype(p->n_op);
	if (opty != LTYPE)
		mips_rewrite_frame_ref(p->n_left);
	if (opty == BITYPE)
		mips_rewrite_frame_ref(p->n_right);
}

static void
mips_find_need_fp(NODE *p, void *arg)
{
	int *needfp = arg;
	CONSZ off;

	if (*needfp)
		return;

	if (p->n_op == ASSIGN && p->n_left->n_op == REG &&
	    regno(p->n_left) == FPREG) {
		*needfp = 1;
		return;
	}

	/*
	 * FUNARG nodes decrement $sp while outgoing stack arguments are
	 * prepared.  PCC's MIPS call templates may still reference locals
	 * during that window, so such functions need a stable $fp base.
	 */
	if (p->n_op == FUNARG) {
		*needfp = 1;
		return;
	}

	/*
	 * Late omit-FP rewriting can safely retarget already addressable
	 * memory references, but raw frame-address values need instruction
	 * selection to materialize them.  Keep $fp for those functions rather
	 * than creating post-regalloc address expressions such as
	 * "move $reg,off($sp)".
	 */
	if (mips_frame_ref_offset(p, &off)) {
		*needfp = 1;
		return;
	}

	/*
	 * The current MIPS call templates temporarily decrement $sp while
	 * preparing each call.  Keep a stable $fp in non-leaf functions until
	 * outgoing call space is modeled as part of the fixed frame.
	 */
	if (callop(p->n_op)) {
		*needfp = 1;
		return;
	}
}

static int
mips_ipole_needs_fp(struct interpass *ipole)
{
	struct interpass *ip;
	int needfp = 0;

	DLIST_FOREACH(ip, ipole, qelem) {
		if (ip->type != IP_NODE)
			continue;
		walkf(ip->ip_node, mips_find_need_fp, &needfp);
		if (needfp)
			break;
	}
	return needfp;
}

/* printf conditional and unconditional branches */
void
cbgen(int o, int lab)
{
}

void
myreader(struct interpass * ipole)
{
}

void
myoptim_pre(struct interpass *ipole)
{
	struct interpass_prolog *ipp;

	ipp = p2env.ipp;
	if (mips_can_omit_fp(ipp) && mips_ipole_needs_fp(ipole))
		ipp->ipp_flags |= IF_NEEDFP;
}

#if 0
/*
 *  Calculate the stack size for arguments
 */
static int stacksize;

static void
calcstacksize(NODE *p, void *arg)
{
	int sz;

	printf("op=%d\n", p->n_op);

	if (p->n_op != CALL && p->n_op != STCALL)
		return;

	sz = argsiz(p->n_right);
	if (sz > stacksize)
		stacksize = sz;

#ifdef PCC_DEBUG
	if (x2debug)
		printf("stacksize: %d\n", stacksize);
#endif
}
#endif

/*
 * Remove some PCONVs after OREGs are created.
 */
static void
pconv2(NODE * p, void *arg)
{
	NODE *q;

	if (p->n_op == PLUS) {
		if (p->n_type == (PTR | SHORT) || p->n_type == (PTR | USHORT)) {
			if (p->n_right->n_op != ICON)
				return;
			if (p->n_left->n_op != PCONV)
				return;
			if (p->n_left->n_left->n_op != OREG)
				return;
			q = p->n_left->n_left;
			nfree(p->n_left);
			p->n_left = q;
			/*
		         * This will be converted to another OREG later.
		         */
		}
	}
}

void
mycanon(NODE * p)
{
	walkf(p, pconv2, 0);
}

void
myoptim(struct interpass * ipole)
{
	struct interpass *ip;
	struct interpass_prolog *ipp;

#ifdef PCC_DEBUG
	if (x2debug)
		printf("myoptim:\n");
#endif

	ipp = p2env.ipp;
	mips_omit_fp = mips_can_omit_fp(ipp);
	if (mips_omit_fp && mips_ipole_needs_fp(ipole)) {
		ipp->ipp_flags |= IF_NEEDFP;
		mips_omit_fp = 0;
	}
	if (!p2regalloc_done) {
		mips_omit_fp = 0;
		return;
	}
	if (!mips_omit_fp)
		return;
	mips_frame_adjust = offcalc(ipp, 1);

#if 0
	stacksize = 0;
#endif

	DLIST_FOREACH(ip, ipole, qelem) {
		if (ip->type != IP_NODE)
			continue;
		mips_rewrite_frame_ref(ip->ip_node);
#if 0
		walkf(ip->ip_node, calcstacksize, 0);
#endif
	}
	mips_omit_fp = 0;
}

/*
 * Move data between registers.  While basic registers aren't a problem,
 * we have to handle the special case of overlapping composite registers.
 */
static int
mips_is_soft_fp64(TWORD t)
{
	return mips_soft_float && (t == DOUBLE || t == LDOUBLE);
}

static int
mips_split_hardfp64_mem(void)
{
#ifdef MIPS_ALIGN64
	return !mips_soft_float && MIPS_ALIGN64 <= SZINT;
#else
	return 0;
#endif
}

static int
mips_hardfp64_mem_aligned(NODE *p, int side)
{
	NODE *mem;
	CONSZ off;
	int base;

	mem = getlr(p, side);
	if (mem->n_op == FLD)
		mem = mem->n_left;
	if (mem->n_op != OREG)
		return 0;

	off = getlval(mem);
	base = mem->n_rval;
	mips_adjust_frame_ref(&off, &base);
	if (base != FPREG && base != SP)
		return 0;
	return off % (SZDOUBLE / SZCHAR) == 0;
}

static void
mips_hardfp64_load(NODE *p)
{
	if (!mips_split_hardfp64_mem()) {
		expand(p, 0, "\tl.d A1,AL\t# load double floating-point reg\n"
		    "\tnop\n");
		return;
	}
	if (mips_hardfp64_mem_aligned(p, 'L')) {
		expand(p, 0, "\tldc1 A1,AL\t# aligned double load\n"
		    "\tnop\n");
		return;
	}
#ifdef TARGET_BIG_ENDIAN
	expand(p, 0, "\tlwc1 U1,AL\t# split double load\n"
	    "\tlwc1 A1,UL\n"
	    "\tnop\n");
#else
	expand(p, 0, "\tlwc1 A1,AL\t# split double load\n"
	    "\tlwc1 U1,UL\n"
	    "\tnop\n");
#endif
}

static void
mips_hardfp64_store(NODE *p)
{
	if (!mips_split_hardfp64_mem()) {
		expand(p, 0,
		    "\ts.d AR,AL\t\t# store double floating-point reg\n");
		return;
	}
	if (mips_hardfp64_mem_aligned(p, 'L')) {
		expand(p, 0,
		    "\tsdc1 AR,AL\t\t# aligned double store\n");
		return;
	}
#ifdef TARGET_BIG_ENDIAN
	expand(p, 0, "\tswc1 UR,AL\t\t# split double store\n"
	    "\tswc1 AR,UL\n");
#else
	expand(p, 0, "\tswc1 AR,AL\t\t# split double store\n"
	    "\tswc1 UR,UL\n");
#endif
}

void
rmove(int s, int d, TWORD t)
{
	switch (t) {
	case LONGLONG:
	case ULONGLONG:
	case DOUBLE:
	case LDOUBLE:
		if ((t == DOUBLE || t == LDOUBLE) && !mips_is_soft_fp64(t))
			goto hardfp;
		{
		int low_first = (s == d + 1);
#ifdef TARGET_BIG_ENDIAN
		if (GCLASS(s) == CLASSB && GCLASS(d) == CLASSB)
			low_first = (d == s + 1);
#endif
                if (low_first) {
                        /* dh = sl, copy low word first */
                        printf("\tmove ");
			print_reg64name(stdout, d, 0);
			printf(",");
			print_reg64name(stdout, s, 0);
			printf("\t# 64-bit rmove\n");
                        printf("\tmove ");
			print_reg64name(stdout, d, 1); 
			printf(",");
			print_reg64name(stdout, s, 1);
			printf("\n");
                } else {
                        /* copy high word first */
                        printf("\tmove ");
			print_reg64name(stdout, d, 1);
			printf(",");
			print_reg64name(stdout, s, 1);
			printf(" # 64-bit rmove\n");
                        printf("\tmove ");
			print_reg64name(stdout, d, 0);
			printf(",");
			print_reg64name(stdout, s, 0);
			printf("\n");
                }
		}
                break;
	case FLOAT:
		if (mips_soft_float) {
			printf("\tmove %s,%s\t# soft-float rmove\n",
			    rnames[d], rnames[s]);
			break;
		}
hardfp:
		if (t == FLOAT)
			printf("\tmov.s ");
		else
			printf("\tmov.d ");
		print_reg64name(stdout, d, 0);
		printf(",");
		print_reg64name(stdout, s, 0);
		printf("\t# float/double rmove\n");
                break;
        default:
                printf("\tmove %s,%s\t# default rmove\n", rnames[d], rnames[s]);
        }
}


/*
 * For class c, find worst-case displacement of the number of
 * registers in the array r[] indexed by class.
 *
 * On MIPS, we have:
 *
 * 32 32-bit registers (8 reserved)
 * 26 64-bit pseudo registers (1 unavailable)
 * 16 floating-point register pairs
 */
int
COLORMAP(int c, int *r)
{
	int num = 0;

        switch (c) {
        case CLASSA:
                num += r[CLASSA];
                num += 2*r[CLASSB];
                return num < 24;
        case CLASSB:
                num += 2*r[CLASSB];
                num += r[CLASSA];
                return num < 25;
	case CLASSC:
		num += r[CLASSC];
		return num < 6;
        }
	comperr("COLORMAP");
        return 0; /* XXX gcc */
}

/*
 * Return a class suitable for a specific type.
 */
int
gclass(TWORD t)
{
	if (t == LONGLONG || t == ULONGLONG)
		return CLASSB;
	if (t == FLOAT)
		return mips_soft_float ? CLASSA : CLASSC;
	if (t == DOUBLE || t == LDOUBLE)
		return mips_soft_float ? CLASSB : CLASSC;
	if (t > FLOAT && t <= LDOUBLE)
		return CLASSC;
	return CLASSA;
}

/*
 * Calculate argument sizes.
 */
void
lastcall(NODE *p)
{
	int pad, pushsz, sz;

#ifdef PCC_DEBUG
	if (x2debug)
		printf("lastcall:\n");
#endif

	p->n_qual = 0;
	if (p->n_op != CALL && p->n_op != FORTCALL && p->n_op != STCALL)
		return;

	pushsz = funargpushsiz(p->n_right);
	pad = (pushsz & 7) != 0 ? 4 : 0;
	sz = 4*nargregs + pushsz + pad;
	if (pad)
		printf("\tsubu %s,%s,%d\t# align stack\n",
		    rnames[SP], rnames[SP], pad);

	p->n_qual = sz; /* XXX */
}

static int
funargpushsiz(NODE *p)
{
	TWORD t;
	int sz;

	if (p == NIL)
		return 0;
	if (p->n_op == CM)
		return funargpushsiz(p->n_left) + funargpushsiz(p->n_right);
	if (p->n_op != FUNARG)
		return 0;

	t = p->n_type;
	if (t == STRTY || t == UNIONTY)
		sz = attr_find(p->n_ap, ATTR_P2STRUCT)->iarg(0);
	else if (t < LONGLONG || t > BTMASK)
		sz = 4;
	else if (DEUNSIGN(t) == LONGLONG)
		sz = 8;
	else if (t == DOUBLE || t == LDOUBLE)
		sz = 8;
	else
		sz = 4;

	return (sz + 3) & ~3;
}

/*
 * Special shapes.
 */
int
special(NODE *p, int shape)
{
	int o = p->n_op;
	CONSZ val;

	if (o != ICON || p->n_name[0] != 0)
		return SRNOPE;

	val = getlval(p);
	switch(shape) {
	case SPCON:
		if ((val & ~0xffff) == 0)
			return SRDIR;
		break;
	case SPOW2CON:
		if (val > 1 && val <= 0x7fffffff &&
		    (val & (val - 1)) == 0)
			return SRDIR;
		break;
	case SSHADDCON:
		if (mips_shiftadd_mul_ok(val))
			return SRDIR;
		break;
	}

	return SRNOPE;
}

/*
 * Target-dependent command-line options.
 */
void
mflags(char *str)
{
	if (strcasecmp(str, "big-endian") == 0) {
#if defined(os_rebsd) && !defined(TARGET_BIG_ENDIAN)
		fprintf(stderr, "big-endian mode is not supported by this target\n");
		exit(1);
#endif
		bigendian = 1;
	} else if (strcasecmp(str, "little-endian") == 0) {
#if defined(os_rebsd) && defined(TARGET_BIG_ENDIAN)
		fprintf(stderr,
		    "little-endian mode is not supported by big-endian mips-rebsd\n");
		exit(1);
#endif
		bigendian = 0;
#ifdef MIPS_CPU_DEFAULT
	} else if (strcasecmp(str, "arch=vr4300") == 0 ||
	    strcasecmp(str, "ips3") == 0) {
		mips_cpu = MIPS_CPU_VR4300;
		if (!mips_fix4300_explicit)
			mips_fix4300 = 1;
	} else if (strcasecmp(str, "arch=mips32r2") == 0 ||
	    strcasecmp(str, "ips32r2") == 0 ||
	    strcasecmp(str, "arch=mips32") == 0) {
		mips_cpu = MIPS_CPU_MIPS32R2;
		if (!mips_fix4300_explicit)
			mips_fix4300 = 0;
	} else if (strcasecmp(str, "fix4300") == 0) {
		mips_fix4300 = 1;
		mips_fix4300_explicit = 1;
	} else if (strcasecmp(str, "no-fix4300") == 0) {
		mips_fix4300 = 0;
		mips_fix4300_explicit = 1;
#endif
	} else if (strcasecmp(str, "hard-float") == 0) {
		mips_soft_float = 0;
	} else if (strcasecmp(str, "soft-float") == 0) {
		mips_soft_float = 1;
	} else {
		fprintf(stderr, "unknown m option '%s'\n", str);
		exit(1);
	}

#if 0
	 else if (strcasecmp(str, "ips2")) {
	} else if (strcasecmp(str, "ips2")) {
	} else if (strcasecmp(str, "ips3")) {
	} else if (strcasecmp(str, "ips4")) {
	} else if (strcasecmp(str, "hard-float")) {
	} else if (strcasecmp(str, "soft-float")) {
	} else if (strcasecmp(str, "abi=32")) {
		nargregs = MIPS_O32_NARGREGS;
	} else if (strcasecmp(str, "abi=n32")) {
		nargregs = MIPS_N32_NARGREGS;
	} else if (strcasecmp(str, "abi=64")) {
		nargregs = MIPS_N32_NARGREGS;
	}
#endif
}

int
features(int mask)
{
	if (mask == 0)
		return 1;
	if ((mask & FEATURE_HARDFLOAT) && mips_soft_float)
		return 0;
	if ((mask & FEATURE_SOFTFLOAT) && !mips_soft_float)
		return 0;
	if ((mask & FEATURE_MIPS32R2) && mips_cpu != MIPS_CPU_MIPS32R2)
		return 0;
	if ((mask & FEATURE_VR4300) && mips_cpu != MIPS_CPU_VR4300)
		return 0;
	if ((mask & FEATURE_FIX4300) && !MIPS_FIX4300_ACTIVE)
		return 0;
	if ((mask & FEATURE_NOFIX4300) && MIPS_FIX4300_ACTIVE)
		return 0;
	return (mask & ~(FEATURE_HARDFLOAT|FEATURE_SOFTFLOAT|
	    FEATURE_MIPS32R2|FEATURE_FIX4300|FEATURE_NOFIX4300|
	    FEATURE_VR4300)) == 0;
}
/*
 * Do something target-dependent for xasm arguments.
 * Supposed to find target-specific constraints and rewrite them.
 */
int
myxasm(struct interpass *ip, NODE *p)
{
	int cw;
	char *c;
	CONSZ v;

	(void)ip;
	cw = xasmcode(p->n_name);
	switch (XASMVAL(cw)) {
	case 'K':	/* unsigned 16-bit immediate */
		if (p->n_left->n_op != ICON) {
			uerror("constant required");
			return 1;
		}
		v = getlval(p->n_left);
		if (v < 0 || v > 0xffff) {
			uerror("impossible constraint");
			return 1;
		}
		p->n_name = tmpstrdup(p->n_name);
		c = strchr(p->n_name, 'K');
		*c = 'i';
		return 0;
	}
	return 0;
}
