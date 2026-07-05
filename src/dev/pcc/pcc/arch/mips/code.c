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

#include <assert.h>
#include "pass1.h"

#ifndef LANG_CXX
#undef NIL
#define NIL NULL
#define NODE P1ND
#define nfree p1nfree
#define ccopy p1tcopy
#define tfree p1tfree
#define	sap sss
#define	n_ap pss
#define	n_type ptype
#undef n_df
#define n_df pdf
typedef struct ssdesc mips_ap_t;
#define MIPS_STACKTEMP_FLAGS SSTMT
#define MIPS_NODE_QUAL(p) ((p)->pqual)
#define MIPS_SYM_AP(sp) ((sp)->sss)
#define MIPS_SET_SYM_AP(sp, ap) ((sp)->sss = (ap))
#define MIPS_TCOPY(p) p1tcopy(p)
#else
typedef struct attr mips_ap_t;
#define MIPS_STACKTEMP_FLAGS STEMP
#define MIPS_NODE_QUAL(p) ((p)->n_qual)
#define MIPS_SYM_AP(sp) ((sp)->sap)
#define MIPS_SET_SYM_AP(sp, ap) ((sp)->sap = (ap))
#define MIPS_TCOPY(p) tcopy(p)
#endif

#ifdef MIPS_HARDFLOAT_O32_ABI
static int
mips_fp_arg_type(TWORD t)
{
	return t == FLOAT || t == DOUBLE || t == LDOUBLE;
}

static int
mips_next_fp_argreg(int *fpregp)
{
	int fpreg = *fpregp;

	if (fpreg > F14)
		return -1;
	*fpregp = fpreg == F12 ? F14 : F16;
	return fpreg;
}

static void
mips_advance_arg_slots(TWORD t, int *regp)
{
	int reg = *regp;

	if (t == DOUBLE || t == LDOUBLE) {
		++reg;
		reg &= ~1;
		reg += 2;
	} else {
		++reg;
	}
	*regp = reg;
}
#endif

/*
 * Print out assembler segment name.
 */
void
setseg(int seg, char *name)
{
	switch (seg) {
	case PROG: name = ".text"; break;
	case DATA:
	case LDATA: name = ".data"; break;
	case STRNG:
	case RDATA: name = ".section .rodata"; break;
	case UDATA: break;
	case PICLDATA:
	case PICDATA: name = ".section .data.rel.rw,\"aw\",@progbits"; break;
	case PICRDATA: name = ".section .data.rel.ro,\"aw\",@progbits"; break;
	case TLSDATA: name = ".section .tdata,\"awT\",@progbits"; break;
	case TLSUDATA: name = ".section .tbss,\"awT\",@nobits"; break;
	case CTORS: name = ".section\t.ctors,\"aw\",@progbits"; break;
	case DTORS: name = ".section\t.dtors,\"aw\",@progbits"; break;
	case NMSEG: 
		printf("\t.section %s,\"a%c\",@progbits\n", name,
		    cftnsp ? 'x' : 'w');
		return;
	}
	printf("\t%s\n", name);
}

/*
 * Define everything needed to print out some data (or text).
 * This means segment, alignment, visibility, etc.
 */
void
defloc(struct symtab *sp)
{
	char *n;

	if (ISFTN(sp->stype))
		return; /* XXX until fixed */

	n = getexname(sp);

	if (sp->sclass == EXTDEF)
		printf("	.globl %s\n", n);
	if (sp->slevel == 0) {
#ifdef USE_GAS
		printf("\t.type %s,@%s\n", n,
		    ISFTN(sp->stype) ? "function" : "object");
		if (!ISFTN(sp->stype))
			printf("\t.size %s," CONFMT "\n", n,
			    tsize(sp->stype, sp->sdf, MIPS_SYM_AP(sp)));
#endif
		printf("%s:\n", n);
	} else
		printf(LABFMT ":\n", sp->soffset);
}


/*
 * cause the alignment to become a multiple of n
 */
void
defalign(int n)
{
	n = ispow2(n / SZCHAR);
	if (n == -1)
		cerror("defalign: n != 2^i");
	printf("\t.p2align %d\n", n);
}

static int rvnr;

/*
 * code for the end of a function
 * deals with struct return here
 */
void
efcode(void)
{
	NODE *p, *q;
	int tempnr;
	int ty;

	if (cftnsp->stype != STRTY+FTN && cftnsp->stype != UNIONTY+FTN)
		return;

	ty = cftnsp->stype - FTN;

	q = block(REG, NIL, NIL, INCREF(ty), 0, MIPS_SYM_AP(cftnsp));
	q->n_rval = V0;
	p = tempnode(0, INCREF(ty), 0, MIPS_SYM_AP(cftnsp));
	tempnr = regno(p);
	p = buildtree(ASSIGN, p, q);
	ecomp(p);

	q = tempnode(tempnr, INCREF(ty), 0, MIPS_SYM_AP(cftnsp));
	q = buildtree(UMUL, q, NIL);

	p = tempnode(rvnr, INCREF(ty), 0, MIPS_SYM_AP(cftnsp));
	p = buildtree(UMUL, p, NIL);

	p = buildtree(ASSIGN, p, q);
	ecomp(p);

	q = tempnode(rvnr, INCREF(ty), 0, MIPS_SYM_AP(cftnsp));
	p = block(REG, NIL, NIL, INCREF(ty), 0, MIPS_SYM_AP(cftnsp));
	p->n_rval = V0;
	p = buildtree(ASSIGN, p, q);
	ecomp(p);
}

/* Put a symbol in a temporary
 * used by bfcode() and its helpers */
static void
putintemp(struct symtab *sym)
{
	NODE *p;
	p = tempnode(0, sym->stype, sym->sdf, MIPS_SYM_AP(sym));
	p = buildtree(ASSIGN, p, nametree(sym));
	sym->soffset = regno(p->n_left);
	sym->sflags |= STNODE;
	ecomp(p);
}

static NODE *
mips_symview(struct symtab *sp, TWORD t)
{
	NODE *p;

	p = nametree(sp);
	p->n_type = t;
	p->n_df = NULL;
	p->n_ap = NULL;
	MIPS_NODE_QUAL(p) = 0;
	return p;
}

/* setup the hidden pointer to struct return parameter
 * used by bfcode() */
static void
param_retptr(void)
{
	NODE *p, *q;

	p = tempnode(0, PTR+STRTY, 0, MIPS_SYM_AP(cftnsp));
	rvnr = regno(p);
	q = block(REG, NIL, NIL, PTR+STRTY, 0, MIPS_SYM_AP(cftnsp));
	q->n_rval = A0;
	p = buildtree(ASSIGN, p, q);
	ecomp(p);
}

static void
param_shift_stret_slots(struct symtab **sp, int cnt)
{
	int extra, i, off;

	extra = SZINT;

	for (i = 0; i < cnt; i++) {
		if (sp[i] == NULL || sp[i]->sclass != PARAM ||
		    sp[i]->stype == VOID || sp[i]->soffset == NOOFFSET)
			continue;
		off = sp[i]->soffset + extra;
		if ((DEUNSIGN(sp[i]->stype) == LONGLONG ||
		    sp[i]->stype == DOUBLE || sp[i]->stype == LDOUBLE) &&
		    (off & (2 * SZINT - 1)) != 0) {
			off += SZINT;
			extra += SZINT;
		}
		sp[i]->soffset = off;
	}
}

static int
param_stack_subword_delta(TWORD t)
{
#ifdef TARGET_BIG_ENDIAN
	if (ISPTR(t) || ISARY(t) || ISFTN(t))
		return 0;

	switch (BTYPE(t)) {
	case CHAR:
	case UCHAR:
	case BOOL:
		return SZINT - SZCHAR;
	case SHORT:
	case USHORT:
		return SZINT - SZSHORT;
	default:
		break;
	}
#else
	(void)t;
#endif
	return 0;
}

static void
param_adjust_stack_subwords(struct symtab **sp, int cnt)
{
	int delta, first_stack, i;

	first_stack = ARGINIT + nargregs * SZINT;

	for (i = 0; i < cnt; i++) {
		if (sp[i] == NULL || sp[i]->sclass != PARAM ||
		    sp[i]->soffset == NOOFFSET || sp[i]->soffset < first_stack)
			continue;
		delta = param_stack_subword_delta(sp[i]->stype);
		if (delta != 0)
			sp[i]->soffset += delta;
	}
}

/* setup struct parameter
 * push the registers out to memory
 * used by bfcode() */
static void
param_struct(struct symtab *sym, int *regp)
{
	int reg = *regp;
	NODE *p, *q;
	int navail;
	int sz;
	int off;
	int num;
	int i;

	navail = nargregs - (reg - A0);
	sz = tsize(sym->stype, sym->sdf, MIPS_SYM_AP(sym)) / SZINT;
	off = ARGINIT/SZINT + (reg - A0);
	num = sz > navail ? navail : sz;
	for (i = 0; i < num; i++) {
		q = block(REG, NIL, NIL, INT, 0, 0);
		q->n_rval = reg++;
		p = block(REG, NIL, NIL, INT, 0, 0);
		p->n_rval = FP;
		p = block(PLUS, p, bcon(4*off++), INT, 0, 0);
		p = block(UMUL, p, NIL, INT, 0, 0);
		p = buildtree(ASSIGN, p, q);
		ecomp(p);
	}

	*regp = reg;
}

/* setup a 64-bit parameter (double/ldouble/longlong)
 * used by bfcode() */
static void
param_64bit(struct symtab *sym, int *regp, int dotemps)
{
	int reg = *regp;
	NODE *p, *q;
	int navail;

	/* alignment */
	++reg;
	reg &= ~1;

	navail = nargregs - (reg - A0);

	if (navail < 2) {
		/* would have appeared half in registers/half
		 * on the stack, but alignment ensures it
		 * appears on the stack */
		if (dotemps)
			putintemp(sym);
		*regp = reg;
		return;
	}

	q = block(REG, NIL, NIL, sym->stype, sym->sdf, MIPS_SYM_AP(sym));
	q->n_rval = A0A1 + (reg - A0);
	if (dotemps) {
		p = tempnode(0, sym->stype, sym->sdf, MIPS_SYM_AP(sym));
		sym->soffset = regno(p);
		sym->sflags |= STNODE;
	} else {
		p = nametree(sym);
	}
	p = buildtree(ASSIGN, p, q);
	ecomp(p);
	*regp = reg + 2;
}

/* setup a 32-bit param on the stack
 * used by bfcode() */
static void
param_32bit(struct symtab *sym, int *regp, int dotemps)
{
	NODE *p, *q;

	q = block(REG, NIL, NIL, sym->stype, sym->sdf, MIPS_SYM_AP(sym));
	q->n_rval = (*regp)++;
	if (dotemps) {
		p = tempnode(0, sym->stype, sym->sdf, MIPS_SYM_AP(sym));
		sym->soffset = regno(p);
		sym->sflags |= STNODE;
	} else {
		p = nametree(sym);
	}
	p = buildtree(ASSIGN, p, q);
	ecomp(p);
}

#ifdef MIPS_HARDFLOAT_O32_ABI
static void
param_fpabi(struct symtab *sym, int *regp, int *fpregp, int dotemps)
{
	NODE *p, *q;
	int fpreg;

	fpreg = mips_next_fp_argreg(fpregp);
	if (fpreg < 0)
		cerror("param_fpabi");

	q = block(REG, NIL, NIL, sym->stype, sym->sdf, MIPS_SYM_AP(sym));
	q->n_rval = fpreg;
	if (dotemps) {
		p = tempnode(0, sym->stype, sym->sdf, MIPS_SYM_AP(sym));
		sym->soffset = regno(p);
		sym->sflags |= STNODE;
	} else {
		p = nametree(sym);
	}
	p = buildtree(ASSIGN, p, q);
	ecomp(p);
	mips_advance_arg_slots(sym->stype, regp);
}
#endif

/*
 * XXX This is a hack.  We cannot have (l)doubles in more than one
 * register class.  So we bounce them in and out of temps to
 * move them in and out of the right registers.
 */
static void
param_double(struct symtab *sym, int *regp, int dotemps)
{
	int reg = *regp;
	NODE *p, *q;
	int navail;

	/* alignment */
	++reg;
	reg &= ~1;

	navail = nargregs - (reg - A0);

	if (navail < 2) {
		/* would have appeared half in registers/half
		 * on the stack, but alignment ensures it
		 * appears on the stack */
		if (dotemps)
			putintemp(sym);
		*regp = reg;
		return;
	}

	q = block(REG, NIL, NIL, LONGLONG, 0, 0);
	q->n_rval = A0A1 + (reg - A0);
	p = mips_symview(sym, LONGLONG);
	p = buildtree(ASSIGN, p, q);
	ecomp(p);
	*regp = reg + 2;
	(void)dotemps;
}

/*
 * XXX This is a hack.  We cannot have floats in more than one
 * register class.  So we bounce them in and out of temps to
 * move them in and out of the right registers.
 */
static void
param_float(struct symtab *sym, int *regp, int dotemps)
{
	NODE *p, *q;

	q = block(REG, NIL, NIL, INT, 0, 0);
	q->n_rval = (*regp)++;
	p = mips_symview(sym, INT);
	p = buildtree(ASSIGN, p, q);
	ecomp(p);
	(void)dotemps;
}

/*
 * code for the beginning of a function; a is an array of
 * indices in symtab for the arguments; n is the number
 */
void
bfcode(struct symtab **sp, int cnt)
{
	int lastreg = A0 + nargregs - 1;
	int saveallargs = 0;
	int i, reg;
#ifdef MIPS_HARDFLOAT_O32_ABI
	int fp_leading, fpreg;
#endif

	/*
	 * Detect if this function has ellipses and save all
	 * argument register onto stack.
	 */
	if (cftnsp->sdf->dlst)
		saveallargs = pr_hasell(cftnsp->sdf->dlst);

	reg = A0;
#ifdef MIPS_HARDFLOAT_O32_ABI
	fp_leading = !mips_soft_float && !oldstyle && !saveallargs;
	fpreg = F12;
#endif

	/* assign hidden return structure to temporary */
	if (cftnsp->stype == STRTY+FTN || cftnsp->stype == UNIONTY+FTN) {
		param_shift_stret_slots(sp, cnt);
		param_retptr();
		++reg;
#ifdef MIPS_HARDFLOAT_O32_ABI
		fp_leading = 0;
#endif
	}
	param_adjust_stack_subwords(sp, cnt);

        /* recalculate the arg offset and create TEMP moves */
        for (i = 0; i < cnt; i++) {

#ifdef MIPS_HARDFLOAT_O32_ABI
		if (fp_leading && mips_fp_arg_type(sp[i]->stype)) {
			if (fpreg <= F14) {
				param_fpabi(sp[i], &reg, &fpreg,
				    xtemps && !saveallargs);
				continue;
			}
			fp_leading = 0;
		} else if (fp_leading) {
			fp_leading = 0;
		}
#endif
		if ((reg > lastreg) && !xtemps)
			break;
		else if (reg > lastreg) 
			putintemp(sp[i]);
		else if (sp[i]->stype == STRTY || sp[i]->stype == UNIONTY)
			param_struct(sp[i], &reg);
		else if (DEUNSIGN(sp[i]->stype) == LONGLONG)
			param_64bit(sp[i], &reg, xtemps && !saveallargs);
		else if (sp[i]->stype == DOUBLE || sp[i]->stype == LDOUBLE)
			param_double(sp[i], &reg, xtemps && !saveallargs);
		else if (sp[i]->stype == FLOAT)
			param_float(sp[i], &reg, xtemps && !saveallargs);
		else
			param_32bit(sp[i], &reg, xtemps && !saveallargs);
	}

	/* if saveallargs, save the rest of the args onto the stack */
	if (!saveallargs)
		return;
	while (reg <= lastreg) {
		NODE *p, *q;
		int off = ARGINIT/SZINT + (reg - A0);
		q = block(REG, NIL, NIL, INT, 0, 0);
		q->n_rval = reg++;
		p = block(REG, NIL, NIL, INT, 0, 0);
		p->n_rval = FP;
		p = block(PLUS, p, bcon(4*off), INT, 0, 0);
		p = block(UMUL, p, NIL, INT, 0, 0);
		p = buildtree(ASSIGN, p, q);
		ecomp(p);
	}

}


/* called just before final exit */
/* flag is 1 if errors, 0 if none */
void
ejobcode(int flag)
{
}

void
bjobcode(void)
{
#ifdef TARGET_NO_REORDER
	printf("\t.set noreorder\n");
#endif
#ifndef TARGET_NO_ABICALLS
	printf("\t.section .mdebug.abi32\n");
	printf("\t.previous\n");

	/* only if -fpic or -fPIC */
	if (kflag > 0)
		printf("\t.abicalls\n");
#endif
}

#ifdef notdef
/*
 * Print character t at position i in one string, until t == -1.
 * Locctr & label is already defined.
 */
void
bycode(int t, int i)
{
	static int lastoctal = 0;

	/* put byte i+1 in a string */

	if (t < 0) {
		if (i != 0)
			puts("\\000\"");
	} else {
		if (i == 0)
			printf("\t.ascii \"");
		if (t == 0)
			return;
		else if (t == '\\' || t == '"') {
			lastoctal = 0;
			putchar('\\');
			putchar(t);
		} else if (t == 011) {
			printf("\\t");
		} else if (t == 012) {
			printf("\\n");
		} else if (t < 040 || t >= 0177) {
			lastoctal++;
			printf("\\%o",t);
		} else if (lastoctal && '0' <= t && t <= '9') {
			lastoctal = 0;
			printf("\"\n\t.ascii \"%c", t);
		} else {	
			lastoctal = 0;
			putchar(t);
		}
	}
}
#endif

/* fix up type of field p */
void
fldty(struct symtab *p)
{
}

/*
 * XXX - fix genswitch.
 */
int
mygenswitch(int num, TWORD type, struct swents **p, int n)
{
	return 0;
}


/* setup call stack with a structure */
/* called from moveargs() */
static struct symtab *
mips_stacktemp(TWORD t, union dimfun *df, mips_ap_t *ap)
{
	struct symtab *sp;

	sp = getsymtab("0mipsarg", MIPS_STACKTEMP_FLAGS);
	sp->stype = t;
	sp->squal = 0;
	sp->sdf = df;
	MIPS_SET_SYM_AP(sp, ap);
	sp->sclass = AUTO;
	sp->soffset = NOOFFSET;
	sp->sflags = 0;
	oalloc(sp, &autooff);
	return sp;
}

static NODE *
mips_stackview(struct symtab *sp, TWORD t)
{
	return mips_symview(sp, t);
}

static NODE *
cmappend(NODE *q, NODE *r)
{
	NODE *p;

	if (q == NIL)
		return r;
	if (q->n_op != CM)
		return block(CM, r, q, INT, 0, 0);
	for (p = q; p->n_left->n_op == CM; p = p->n_left)
		;
	p->n_left = block(CM, r, p->n_left, INT, 0, 0);
	return q;
}

static NODE *
mips_struct_word(NODE *p, int off)
{
	p->n_type = PTR+INT;
	p->n_df = NULL;
	p->n_ap = NULL;
	MIPS_NODE_QUAL(p) = 0;
	if (off != 0)
		p = block(PLUS, p, bcon(off), PTR+INT, 0, 0);
	return buildtree(UMUL, p, NIL);
}

static NODE *
movearg_struct(NODE *p, NODE *prefix, int *regp)
{
	int sreg = *regp;
	int reg = sreg;
	NODE *l, *q, *t, *r;
	struct symtab *addrsp;
	int navail;
	int num;
        int sz;
	int ty;
	int i;
	int direct;

	navail = nargregs - (reg - A0);
	if (navail < 0)
		navail = 0;
	sz = tsize(p->n_type, p->n_df, p->n_ap) / SZINT;
	num = sz > navail ? navail : sz;

	l = p->n_left;
	nfree(p);
	ty = l->n_type;
	direct = 0;
	addrsp = NULL;
	q = prefix;
	if (!direct) {
		addrsp = mips_stacktemp(l->n_type, l->n_df, l->n_ap);
		t = mips_stackview(addrsp, l->n_type);
		l = buildtree(ASSIGN, t, l);
		q = cmappend(q, l);
	}

	/* copy structure into registers */
	for (i = 0; i < num; i++) {
		t = direct ? MIPS_TCOPY(l) : mips_stackview(addrsp, ty);
		t = mips_struct_word(t, 4*i);

		r = block(REG, NIL, NIL, INT, 0, 0);
		r->n_rval = reg++;

               	r = buildtree(ASSIGN, r, t);
		q = cmappend(q, r);
	}
	/*
	 * Push the stack part in reverse word order.  The normal FUNARG
	 * expansion decrements $sp before each store, so the last emitted
	 * word becomes the first stack slot visible to the callee.
	 */
	for (i = sz - 1; i >= num; i--) {
		t = direct ? MIPS_TCOPY(l) : mips_stackview(addrsp, ty);
		t = mips_struct_word(t, 4*i);
		r = block(FUNARG, t, NIL, t->n_type, t->n_df, t->n_ap);
		q = cmappend(q, r);
	}

	if (direct)
		tfree(l);
	*regp = sreg + sz;
	return q;
}

/* setup call stack with 64-bit argument */
/* called from moveargs() */
static NODE *
movearg_64bit(NODE *p, int *regp, NODE **padp)
{
	int reg = *regp;
	int oreg = reg;
	NODE *q;
	int lastarg;

	*padp = NIL;

	/* alignment */
	++reg;
	reg &= ~1;

	lastarg = A0 + nargregs - 1;
	if (reg > lastarg) {
		q = block(FUNARG, p, NIL, p->n_type, p->n_df, p->n_ap);
		if (oreg > lastarg && oreg != reg)
			*padp = block(FUNARG, bcon(0), NIL, INT, 0, 0);
		*regp = reg + 2;
		return q;
	}

	q = block(REG, NIL, NIL, p->n_type, p->n_df, p->n_ap);
	q->n_rval = A0A1 + (reg - A0);
	q = buildtree(ASSIGN, q, p);

	*regp = reg + 2;
	return q;
}

/* setup call stack with 32-bit argument */
/* called from moveargs() */
static NODE *
movearg_32bit(NODE *p, int *regp)
{
	int reg = *regp;
	NODE *q;
	int lastarg;

	lastarg = A0 + nargregs - 1;
	if (reg > lastarg) {
		*regp = reg + 1;
		return block(FUNARG, p, NIL, p->n_type, p->n_df, p->n_ap);
	}
	q = block(REG, NIL, NIL, p->n_type, p->n_df, p->n_ap);
	q->n_rval = reg++;
	q = buildtree(ASSIGN, q, p);

	*regp = reg;
	return q;
}

#ifdef MIPS_HARDFLOAT_O32_ABI
static NODE *
movearg_fpabi(NODE *p, int *regp, int *fpregp)
{
	NODE *q;
	int fpreg;

	fpreg = mips_next_fp_argreg(fpregp);
	if (fpreg < 0)
		cerror("movearg_fpabi");

	q = block(REG, NIL, NIL, p->n_type, p->n_df, p->n_ap);
	q->n_rval = fpreg;
	q = buildtree(ASSIGN, q, p);

	mips_advance_arg_slots(p->n_type, regp);
	return q;
}

static int
call_fpabi(NODE *p)
{
	NODE *l;

	if (p->n_op == UCALL)
		return 0;

	l = p->n_left;
	if (l->n_df == NULL || l->n_df->dlst == 0)
		return 0;
	return pr_hasell(l->n_df->dlst) == 0;
}
#endif

static NODE *
moveargs(NODE *p, int *regp
#ifdef MIPS_HARDFLOAT_O32_ABI
    , int *fpregp, int *fp_leadingp
#endif
    )
{
        NODE *r, **rp;

        if (p->n_op == CM) {
                p->n_left = moveargs(p->n_left, regp
#ifdef MIPS_HARDFLOAT_O32_ABI
		    , fpregp, fp_leadingp
#endif
		    );
                r = p->n_right;
		rp = &p->n_right;
        } else {
		r = p;
		rp = &p;
	}

#ifdef MIPS_HARDFLOAT_O32_ABI
	if (*fp_leadingp && mips_fp_arg_type(r->n_type)) {
		if (*fpregp <= F14) {
			*rp = movearg_fpabi(r, regp, fpregp);
			return p;
		}
		*fp_leadingp = 0;
	} else if (*fp_leadingp) {
		*fp_leadingp = 0;
	}
#endif

	if (r->n_op == STARG) {
		if (p->n_op == CM) {
			NODE *l = p->n_left;
			nfree(p);
			return movearg_struct(r, l, regp);
		}
		return movearg_struct(r, NIL, regp);
	} else if (DEUNSIGN(r->n_type) == LONGLONG) {
		NODE *pad;

		*rp = movearg_64bit(r, regp, &pad);
		if (pad != NIL) {
			if (p->n_op == CM)
				p->n_left = block(CM, p->n_left, pad, INT, 0, 0);
			else
				p = block(CM, pad, *rp, INT, 0, 0);
		}
	} else if (r->n_type == DOUBLE || r->n_type == LDOUBLE) {
		/*
		 * Varargs pass FP values through integer argument slots.  Keep
		 * the bit bounce in memory so xtemps does not assign one TEMP
		 * to both FP and integer register classes.
		 */
		struct symtab *sp = mips_stacktemp(r->n_type, r->n_df, r->n_ap);
		NODE *t1 = mips_stackview(sp, LONGLONG);
		NODE *t2 = mips_stackview(sp, r->n_type);
		NODE *pad;
		t1 = movearg_64bit(t1, regp, &pad);
		r = block(ASSIGN, t2, r, r->n_type, r->n_df, r->n_ap);
		if (p->n_op == CM) {
			NODE *l = p->n_left;
			if (pad != NIL)
				l = block(CM, l, pad, INT, 0, 0);
			p->n_left = buildtree(CM, l, t1);
			p->n_right = r;
		} else {
			if (pad != NIL)
				t1 = block(CM, pad, t1, INT, 0, 0);
			p = buildtree(CM, t1, r);
		}
	} else if (r->n_type == FLOAT) {
		/*
		 * Same memory bounce as double, but through a single integer
		 * argument slot.
		 */
		struct symtab *sp = mips_stacktemp(r->n_type, r->n_df, r->n_ap);
		NODE *t1 = mips_stackview(sp, INT);
		NODE *t2 = mips_stackview(sp, r->n_type);
		t1 = movearg_32bit(t1, regp);
		r = block(ASSIGN, t2, r, r->n_type, r->n_df, r->n_ap);
		if (p->n_op == CM) {
			p->n_left = buildtree(CM, p->n_left, t1);
			p->n_right = r;
		} else {
			p = buildtree(CM, t1, r);
		}
	} else {
		*rp = movearg_32bit(r, regp);
	}

	return p;
}

/*
 * Called with a function call with arguments as argument.
 * This is done early in buildtree() and only done once.
 */
NODE *
funcode(NODE *p)
{
	int regnum = A0;
	NODE *l, *r, *t, *q;
	int ty;
#ifdef MIPS_HARDFLOAT_O32_ABI
	int fpreg = F12;
	int fp_leading;
#endif

	l = p->n_left;
	r = p->n_right;
#ifdef MIPS_HARDFLOAT_O32_ABI
	fp_leading = !mips_soft_float && call_fpabi(p);
#endif

	/*
	 * if returning a structure, make the first argument
	 * a hidden pointer to return structure.
	 */
	ty = DECREF(l->n_type);
	if (ty == STRTY+FTN || ty == UNIONTY+FTN) {
		ty = DECREF(l->n_type) - FTN;
		q = cstknode(ty, l->n_df, l->n_ap);
		q = buildtree(ADDROF, q, NIL);
		if (p->n_op == UCALL) {
			p->n_op = CALL;
			p->n_right = q;
		} else if (r->n_op != CM) {
			p->n_right = block(CM, q, r, INCREF(ty),
			    l->n_df, l->n_ap);
		} else {
			for (t = r; t->n_left->n_op == CM; t = t->n_left)
				;
			t->n_left = block(CM, q, t->n_left, INCREF(ty),
			    l->n_df, l->n_ap);
		}
#ifdef MIPS_HARDFLOAT_O32_ABI
		fp_leading = 0;
#endif
	}

	p->n_right = moveargs(p->n_right, &regnum
#ifdef MIPS_HARDFLOAT_O32_ABI
	    , &fpreg, &fp_leading
#endif
	    );

	return p;
}

NODE *
builtin_cfa(const struct bitable *bt, NODE *a)
{
	uerror("missing builtin_cfa");
	return bcon(0);
}

NODE *
builtin_frame_address(const struct bitable *bt, NODE *a)
{
	uerror("missing builtin_frame_address");
	return bcon(0);
}

NODE *
builtin_return_address(const struct bitable *bt, NODE *a)
{
	uerror("missing builtin_return_address");
	return bcon(0);
}
